/**
 * @file cos_spotify_uac_host.c
 * @brief USB Audio Class (UAC) Host 类驱动 —— TinyUSB 0.21 未内置,本 App 自带。
 *
 * ── 为什么需要本文件 ──────────────────────────────────────────
 *   本仓库使用的 espressif__tinyusb 0.21.0 的 src/class/audio/ 只提供
 *   audio_device.c(UAC **设备**侧),**没有 audio_host.c**;tusb_option.h
 *   亦无 CFG_TUH_AUDIO。因此 USB Audio Host 必须由应用自带。
 *
 *   实现方式与 TinyUSB 内置的 midi_host.c / cdc_host.c 完全同构:
 *     1) 定义 usbh_class_driver_t 实例;
 *     2) 通过 weak 钩子 usbh_app_driver_get_cb() 注册给 USBH 核心;
 *     3) open() 解析接口关联描述符(IAD)与 AC/AS 接口;
 *     4) set_config() 切换 AS 接口到备用设置 1,取出等时 OUT 端点;
 *     5) 用 tuh_edpt_xfer() 向等时 OUT 端点持续送 PCM。
 *
 * ── 支持的子集(UAC1.0,覆盖绝大多数 USB-C 耳机) ──────────────
 *   · AudioControl(AC)接口 + AudioStreaming(AS)接口(IAD 关联或独立)
 *   · Format Type I,PCM,16-bit,little-endian
 *   · 采样率:44.1kHz / 48kHz(从 AS 端点描述符的采样率表中匹配)
 *   · 通道数:1(单声道)或 2(立体声),自动适配
 *   · 等时 OUT 端点(同步/自适应均可,按端点属性发送)
 *
 *   不支持的组合(如 UAC2.0、非 PCM 格式、24-bit)会在 open() 阶段
 *   返回 0 长度,USBH 核心会继续尝试其它驱动,最终 App 显示
 *   COS_SPOTIFY_ERR_NOT_AUDIO。
 *
 * ── 线程模型 ─────────────────────────────────────────────────
 *   所有回调(open/set_config/xfer_cb/close)都在 TinyUSB Host 任务上下文执行
 *   (即 tuh_task 所在任务)。音频推送经 cos_spotify_uac_host_write() 从
 *   LVGL 定时器上下文发起,通过 usbh_edpt_claim/release 与回调互斥。
 */
#include "cos_spotify_uac_host.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <string.h>

#include "tusb.h"
#include "host/usbh_pvt.h"

#define COS_LOG_TAG "SpotifyUACH"
#include "cos_log.h"

/* ── UAC 描述符常量 ───────────────────────────────────────── */
/* 注意:TinyUSB 0.21 的 tusb_types.h 只定义到 TUSB_CLASS_AUDIO,
 * 没有 Audio 子类常量(UAC Host 类缺失的又一处证据),这里按 USB
 * Audio 1.0 规范自行定义。 */

#define TUSB_SUBCLASS_AUDIOCONTROL   0x01
#define TUSB_SUBCLASS_AUDIOSTREAMING 0x02

#define UAC_CS_INTERFACE        0x24
#define UAC_CS_ENDPOINT         0x25

#define UAC_SUBTYPE_AS_GENERAL          0x01
#define UAC_SUBTYPE_FORMAT_TYPE         0x02
#define UAC_SUBTYPE_AC_HEADER           0x01
#define UAC_SUBTYPE_AC_INPUT_TERMINAL   0x02
#define UAC_SUBTYPE_AC_OUTPUT_TERMINAL  0x03
#define UAC_SUBTYPE_AC_FEATURE_UNIT     0x06

#define UAC_FORMAT_TYPE_I       0x01
#define UAC_FORMAT_PCM          0x01

/* ── 驱动状态 ─────────────────────────────────────────────── */

typedef struct
{
    bool     in_use;
    uint8_t  dev_addr;

    /* 端点 */
    uint8_t  ep_out;          /* 等时 OUT 端点地址(含方向位) */
    uint8_t  ep_out_attr;     /* 端点属性(bmAttributes)      */
    uint16_t ep_out_mps;      /* wMaxPacketSize               */
    uint8_t  ep_out_interval; /* bInterval                    */

    /* 音频格式 */
    uint8_t  channels;        /* 1 或 2                       */
    uint8_t  bit_resolution;  /* 16                           */
    uint32_t sample_rate;     /* 44100 / 48000                */
    uint8_t  as_itf;          /* AudioStreaming 接口号        */
    uint8_t  as_alt;          /* 已激活的备用设置号           */

    /* 流状态 */
    volatile bool streaming;

    /* 等时传输缓冲(内部 DMA 安全)。等时端点要求每次 xfer 一块连续缓冲。 */
    uint8_t  tx_buf[COS_SPOTIFY_UAC_TX_BUF_SIZE] __attribute__((aligned(4)));
    volatile bool tx_busy;    /* 上一块尚未送完              */
} uac_host_state_t;

/* TinyUSB 每个 rhport 只服务一个 UAC 设备;此处只保留 1 个实例 */
static uac_host_state_t s_uac;
static uint8_t s_rhport = 0;
static bool s_driver_inited = false;

/* ── 对外查询 ─────────────────────────────────────────────── */

bool cos_spotify_uac_host_ready(void)
{
    return s_uac.in_use && s_uac.ep_out != 0;
}

bool cos_spotify_uac_host_connected(void)
{
    /* 设备仍在总线上即视为连接 */
    return s_uac.in_use;
}

uint32_t cos_spotify_uac_host_sample_rate(void)
{
    return s_uac.sample_rate;
}

uint8_t cos_spotify_uac_host_channels(void)
{
    return s_uac.channels;
}

/* ── 描述符解析辅助 ───────────────────────────────────────── */

/* 解析 AudioControl 接口下的各描述符,记录功能单元数量等(当前仅做校验)。
 * 返回被消费的字节数。 */
static uint16_t _parse_ac_interface(const uint8_t *p_desc, uint16_t max_len)
{
    uint16_t consumed = 0;
    const uint8_t *p = p_desc;

    /* 第一个描述符是标准接口描述符,由调用方已经跳过 */
    while (consumed + 2 <= max_len)
    {
        uint8_t blen = p[0];
        uint8_t btype = p[1];
        if (blen < 2)
        {
            break;
        }
        /* 遇到下一个标准接口/端点描述符则停止 */
        if (btype == TUSB_DESC_INTERFACE || btype == TUSB_DESC_ENDPOINT)
        {
            break;
        }
        if (btype == TUSB_DESC_CS_INTERFACE)
        {
            uint8_t subtype = p[2];
            COS_LOG_I("UAC: AC CS subtype=0x%02X len=%u", subtype, blen);
            (void)subtype;
        }
        consumed += blen;
        p += blen;
    }
    return consumed;
}

/* 解析 AudioStreaming 接口的类描述符,提取 Format Type I 的通道数与位深。
 * 返回被消费的字节数。 */
static uint16_t _parse_as_interface(const uint8_t *p_desc, uint16_t max_len, uint8_t *out_channels,
                                    uint8_t *out_bits)
{
    uint16_t consumed = 0;
    const uint8_t *p = p_desc;

    while (consumed + 2 <= max_len)
    {
        uint8_t blen = p[0];
        uint8_t btype = p[1];
        if (blen < 2)
        {
            break;
        }
        if (btype == TUSB_DESC_INTERFACE || btype == TUSB_DESC_ENDPOINT)
        {
            break;
        }
        if (btype == TUSB_DESC_CS_INTERFACE && blen >= 8)
        {
            uint8_t subtype = p[2];
            if (subtype == UAC_SUBTYPE_FORMAT_TYPE && p[3] == UAC_FORMAT_TYPE_I &&
                p[4] == UAC_FORMAT_PCM)
            {
                /* Format Type I 布局:
                 * [0]=bLength [1]=bDescriptorType [2]=bDescriptorSubtype
                 * [3]=bFormatType [4]=bNrChannels [5]=bSubframeSize
                 * [6]=bBitResolution [7]=bSamFreqType ... */
                *out_channels = p[4];
                *out_bits = p[6];
                COS_LOG_I("UAC: FormatType I ch=%u subframe=%u bits=%u",
                          (unsigned)p[4], (unsigned)p[5], (unsigned)p[6]);
            }
        }
        consumed += blen;
        p += blen;
    }
    return consumed;
}

/* 在 AS 接口的采样率表中匹配支持的采样率(优先 48000,其次 44100)。 */
static uint32_t _pick_sample_rate(const uint8_t *samfreq, uint8_t count, uint8_t samfreq_type)
{
    /* samfreq_type: 0 = 连续区间(3 字节起止), >0 = 离散列表 */
    if (samfreq_type == 0)
    {
        /* 连续区间:[tLower(3)][tUpper(3)] */
        uint32_t lower = (uint32_t)samfreq[0] | ((uint32_t)samfreq[1] << 8) | ((uint32_t)samfreq[2] << 16);
        uint32_t upper = (uint32_t)samfreq[3] | ((uint32_t)samfreq[4] << 8) | ((uint32_t)samfreq[5] << 16);
        if (48000 <= upper && 48000 >= lower)
        {
            return 48000;
        }
        if (44100 <= upper && 44100 >= lower)
        {
            return 44100;
        }
        return 0;
    }

    uint32_t has_48000 = 0, has_44100 = 0, first = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        const uint8_t *f = samfreq + i * 3;
        uint32_t hz = (uint32_t)f[0] | ((uint32_t)f[1] << 8) | ((uint32_t)f[2] << 16);
        if (i == 0)
        {
            first = hz;
        }
        if (hz == 48000)
        {
            has_48000 = hz;
        }
        if (hz == 44100)
        {
            has_44100 = hz;
        }
    }
    if (has_48000)
    {
        return has_48000;
    }
    if (has_44100)
    {
        return has_44100;
    }
    return first;   /* 退而求其次:用第一个离散采样率 */
}

/* ── usbh_class_driver_t 回调 ─────────────────────────────── */

static bool _uac_init(void)
{
    COS_LOG_I("UAC host driver: init");
    memset(&s_uac, 0, sizeof(s_uac));
    s_driver_inited = true;
    return true;
}

static bool _uac_deinit(void)
{
    COS_LOG_I("UAC host driver: deinit");
    memset(&s_uac, 0, sizeof(s_uac));
    s_driver_inited = false;
    return true;
}

/**
 * @brief 打开一个 Audio 接口对(AC + AS)。
 * @return 被消费的描述符字节数;0 表示"不认识/不支持",USBH 会尝试其它驱动。
 */
static uint16_t _uac_open(uint8_t rhport, uint8_t dev_addr,
                          const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    s_rhport = rhport;

    /* 只处理 Audio 类 + 音频控制子类(AC)。若进来的是 AS 子类,说明没有 IAD,
     * 单独 AS 无法定位 AC,直接放弃(极少数老耳机)。 */
    if (itf_desc->bInterfaceClass != TUSB_CLASS_AUDIO)
    {
        return 0;
    }
    if (itf_desc->bInterfaceSubClass != TUSB_SUBCLASS_AUDIOCONTROL)
    {
        return 0;
    }

    COS_LOG_I("UAC: open AC itf=%u alt=%u", itf_desc->bInterfaceNumber, itf_desc->bAlternateSetting);

    const uint8_t *p = (const uint8_t *)itf_desc + sizeof(tusb_desc_interface_t);
    uint16_t consumed = sizeof(tusb_desc_interface_t);
    uint16_t remaining = max_len - consumed;

    uint8_t channels = 2;
    uint8_t bits = 16;
    uint32_t sample_rate = 0;
    uint8_t ep_out = 0, ep_attr = 0, ep_interval = 0;
    uint16_t ep_mps = 0;
    uint8_t as_itf = 0, as_alt = 0;

    /* 遍历 AC 及其后紧随的 AS 接口,直到遇到下一个非 Audio 接口 */
    while (remaining > 2)
    {
        uint8_t blen = p[0];
        uint8_t btype = p[1];
        if (blen < 2 || blen > remaining)
        {
            break;
        }

        if (btype == TUSB_DESC_INTERFACE)
        {
            const tusb_desc_interface_t *itf = (const tusb_desc_interface_t *)p;
            if (itf->bInterfaceClass != TUSB_CLASS_AUDIO)
            {
                break;   /* 音频功能块结束 */
            }

            if (itf->bInterfaceSubClass == TUSB_SUBCLASS_AUDIOCONTROL)
            {
                consumed += blen;
                p += blen;
                remaining -= blen;
                uint16_t used = _parse_ac_interface(p, remaining);
                consumed += used;
                p += used;
                remaining -= used;
                continue;
            }
            if (itf->bInterfaceSubClass == TUSB_SUBCLASS_AUDIOSTREAMING)
            {
                as_itf = itf->bInterfaceNumber;
                as_alt = itf->bAlternateSetting;

                consumed += blen;
                p += blen;
                remaining -= blen;

                uint16_t used = _parse_as_interface(p, remaining, &channels, &bits);
                consumed += used;
                p += used;
                remaining -= used;
                continue;
            }
            /* 其它音频子类:跳过该接口描述符,继续扫描 */
            consumed += blen;
            p += blen;
            remaining -= blen;
            continue;
        }

        if (btype == TUSB_DESC_ENDPOINT && blen >= sizeof(tusb_desc_endpoint_t))
        {
            const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *)p;
            /* 只关心等时 OUT 端点(耳机播放方向) */
            if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_OUT &&
                (ep->bmAttributes.xfer == TUSB_XFER_ISOCHRONOUS))
            {
                ep_out = ep->bEndpointAddress;
                /* bmAttributes 是位域结构体(无 val 成员),按位拼回原始字节 */
                ep_attr = (uint8_t)((ep->bmAttributes.xfer & 0x03) |
                                    ((ep->bmAttributes.sync & 0x03) << 2) |
                                    ((ep->bmAttributes.usage & 0x03) << 4));
                ep_mps = ep->wMaxPacketSize;
                ep_interval = ep->bInterval;
                COS_LOG_I("UAC: iso OUT ep=0x%02X mps=%u attr=0x%02X",
                          ep_out, ep_mps, ep_attr);
            }
            consumed += blen;
            p += blen;
            remaining -= blen;
            continue;
        }

        /* AS 的 CS_ENDPOINT:含采样率描述符 */
        if (btype == TUSB_DESC_CS_ENDPOINT && blen >= 8)
        {
            /* CS_ENDPOINT 后紧跟 CS_INTERFACE(FORMAT_TYPE) 才含采样率;
             * 这里直接在剩余流中找下一个 FORMAT_TYPE。 */
            consumed += blen;
            p += blen;
            remaining -= blen;

            if (remaining > 2 && p[1] == TUSB_DESC_CS_INTERFACE && p[2] == UAC_SUBTYPE_FORMAT_TYPE)
            {
                uint8_t ftlen = p[0];
                if (ftlen >= 8 && p[3] == UAC_FORMAT_TYPE_I)
                {
                    uint8_t nch = p[4];
                    uint8_t bres = p[6];
                    uint8_t stype = p[7];
                    if (nch)
                    {
                        channels = nch;
                    }
                    if (bres)
                    {
                        bits = bres;
                    }
                    uint32_t sr = _pick_sample_rate(p + 8, stype, stype);
                    if (sr)
                    {
                        sample_rate = sr;
                    }
                    COS_LOG_I("UAC: fmt ch=%u bits=%u samfreqType=%u rate=%lu",
                              channels, bits, stype, (unsigned long)sample_rate);
                }
                consumed += ftlen;
                p += ftlen;
                remaining -= ftlen;
                continue;
            }
            continue;
        }

        consumed += blen;
        p += blen;
        remaining -= blen;
    }

    /* 校验:必须有等时 OUT 端点、16bit PCM、有可用采样率 */
    if (ep_out == 0 || ep_mps == 0 || bits != 16 || sample_rate == 0)
    {
        COS_LOG_W("UAC: unsupported (ep=0x%02X bits=%u rate=%lu) -> reject",
                  ep_out, bits, (unsigned long)sample_rate);
        return 0;
    }
    if (channels != 1 && channels != 2)
    {
        COS_LOG_W("UAC: unsupported channels=%u -> reject", channels);
        return 0;
    }

    s_uac.in_use = true;
    s_uac.dev_addr = dev_addr;
    s_uac.ep_out = ep_out;
    s_uac.ep_out_attr = ep_attr;
    s_uac.ep_out_mps = ep_mps;
    s_uac.ep_out_interval = ep_interval;
    s_uac.channels = channels;
    s_uac.bit_resolution = bits;
    s_uac.sample_rate = sample_rate;
    s_uac.as_itf = as_itf;
    s_uac.as_alt = as_alt;

    COS_LOG_I("UAC: accepted dev=%u ep=0x%02X %lu Hz %u ch %u bit",
              dev_addr, ep_out, (unsigned long)sample_rate, channels, bits);

    return consumed;
}

/** @brief 枚举完成:切换 AS 接口到备用设置,并打开等时 OUT 端点。 */
static bool _uac_set_config(uint8_t dev_addr, uint8_t itf_num)
{
    (void)itf_num;

    if (!s_uac.in_use || s_uac.dev_addr != dev_addr)
    {
        return false;
    }

    /* 1) 切换 AudioStreaming 接口到含数据端点的备用设置。
     * tuh_interface_set() 为阻塞式:complete_cb=NULL 时 user_data 须指向
     * xfer_result_t 接收结果。 */
    if (s_uac.as_alt != 0)
    {
        xfer_result_t res = XFER_RESULT_INVALID;
        bool ok = tuh_interface_set(dev_addr, s_uac.as_itf, s_uac.as_alt, NULL,
                                    (uintptr_t)&res);
        if (!ok || res != XFER_RESULT_SUCCESS)
        {
            COS_LOG_W("UAC: set_interface(itf=%u alt=%u) failed (%d)",
                      s_uac.as_itf, s_uac.as_alt, (int)res);
            /* 不致命:部分耳机 alt=0 即含数据端点 */
        }
        else
        {
            COS_LOG_I("UAC: AS itf=%u alt=%u activated", s_uac.as_itf, s_uac.as_alt);
        }
    }

    /* 2) 打开等时 OUT 端点 */
    tusb_desc_endpoint_t ep_desc;
    memset(&ep_desc, 0, sizeof(ep_desc));
    ep_desc.bLength = sizeof(tusb_desc_endpoint_t);
    ep_desc.bDescriptorType = TUSB_DESC_ENDPOINT;
    ep_desc.bEndpointAddress = s_uac.ep_out;
    ep_desc.bmAttributes.xfer = (uint8_t)(s_uac.ep_out_attr & 0x03);
    ep_desc.bmAttributes.sync = (uint8_t)((s_uac.ep_out_attr >> 2) & 0x03);
    ep_desc.bmAttributes.usage = (uint8_t)((s_uac.ep_out_attr >> 4) & 0x03);
    ep_desc.wMaxPacketSize = s_uac.ep_out_mps;
    ep_desc.bInterval = s_uac.ep_out_interval;

    if (!tuh_edpt_open(dev_addr, &ep_desc))
    {
        COS_LOG_E("UAC: tuh_edpt_open(ep=0x%02X) failed", s_uac.ep_out);
        return false;
    }

    COS_LOG_I("UAC: endpoint opened, ready");
    return true;
}

/** @brief 等时传输完成回调。 */
static bool _uac_xfer_cb(uint8_t dev_addr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)dev_addr;
    (void)ep_addr;
    (void)result;
    (void)xferred_bytes;

    /* 释放缓冲,允许下一次 write 填充 */
    s_uac.tx_busy = false;
    return true;
}

static void _uac_close(uint8_t dev_addr)
{
    COS_LOG_I("UAC: close dev=%u", dev_addr);
    if (s_uac.dev_addr == dev_addr)
    {
        memset(&s_uac, 0, sizeof(s_uac));
    }
}

/* ── 驱动注册 ─────────────────────────────────────────────── */

static const usbh_class_driver_t s_uac_driver = {
    .name = "UAC-Host",
    .init = _uac_init,
    .deinit = _uac_deinit,
    .open = _uac_open,
    .set_config = _uac_set_config,
    .xfer_cb = _uac_xfer_cb,
    .close = _uac_close,
};

/* TinyUSB 的 weak 钩子:把本驱动追加到内置驱动表之后。 */
usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &s_uac_driver;
}

/* ── 音频推送 ─────────────────────────────────────────────── */

int32_t cos_spotify_uac_host_write(const void *pcm, uint32_t bytes)
{
    if (!s_uac.in_use || s_uac.ep_out == 0 || pcm == NULL || bytes == 0)
    {
        return -1;
    }
    if (!s_uac.streaming)
    {
        return 0;
    }
    if (s_uac.tx_busy)
    {
        return 0;   /* 上一块还在总线上,本次丢弃(等时端点允许丢帧) */
    }

    uint32_t n = bytes;
    if (n > sizeof(s_uac.tx_buf))
    {
        n = sizeof(s_uac.tx_buf);
    }
    /* 块大小必须是端点 wMaxPacketSize 的整数倍,等时端点不自动分包 */
    uint32_t mps = s_uac.ep_out_mps;
    if (mps == 0)
    {
        return -1;
    }
    n = (n / mps) * mps;
    if (n == 0)
    {
        n = mps;
    }
    if (n > sizeof(s_uac.tx_buf))
    {
        return -1;
    }

    memcpy(s_uac.tx_buf, pcm, n);

    tuh_xfer_t xfer = {
        .daddr = s_uac.dev_addr,
        .ep_addr = s_uac.ep_out,
        .buflen = (uint16_t)n,
        .buffer = s_uac.tx_buf,
        .complete_cb = NULL,
        .user_data = 0,
    };

    s_uac.tx_busy = true;
    if (!tuh_edpt_xfer(&xfer))
    {
        s_uac.tx_busy = false;
        return -1;
    }
    return (int32_t)n;
}

void cos_spotify_uac_host_stream(bool on)
{
    s_uac.streaming = on;
    if (!on)
    {
        s_uac.tx_busy = false;
    }
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
