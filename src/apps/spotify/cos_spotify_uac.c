/**
 * @file eos_spotify_uac.c
 * @brief Spotify USB UAC 主机层:PHY 抢占 / TinyUSB Host 生命周期 / 音频推送转发。
 *
 * 交付批次:Batch 2(完整实现)。
 *
 * ── USB 拓扑与 PHY 复用(ESP32-S3) ───────────────────────────
 *   XIAO ESP32-S3 只有一个 USB-C(D-=GPIO19 / D+=GPIO20),内部两个控制器
 *   共享同一个 FSLS PHY,由 RTCCNTL.usb_conf.sw_usb_phy_sel 二选一:
 *     0 → USB-Serial-JTAG(烧录/monitor/console)
 *     1 → USB-OTG(本 App 的 UAC Host)
 *
 *   与 USB MSC 的唯一区别:MSC 用 USB_OTG_MODE_DEVICE(本机做设备),
 *   本 App 用 USB_OTG_MODE_HOST(本机做主机,驱动 UAC 耳机)。
 *
 * ── 线程模型 ────────────────────────────────────────────────
 *   tusb_init()/tuh_task()/tuh_deinit() 必须处于同一任务上下文(usbh.h 要求),
 *   因此本层自建一个事件泵任务(与 USB MSC 的 eos_tusb 完全同构),
 *   这是本 App 唯一新增的任务。
 */
#include "eos_spotify_uac.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tusb.h"

/* USB PHY 管理器:把内部 PHY 从 USB-Serial-JTAG 抢给 USB-OTG */
#include "esp_private/usb_phy.h"
/* 退出时把 PHY 还给 USB-Serial-JTAG(usb_del_phy 不会自动拨回) */
#include "hal/usb_serial_jtag_ll.h"

#include "eos_spotify_board.h"
#include "eos_usb_msc_board.h"
#include "eos_spotify_uac_host.h"

#define EOS_LOG_TAG "SpotifyUac"
#include "eos_log.h"

/* ── 事件泵任务参数(与 USB MSC 保持一致,已验证不干扰系统) ── */
#define UAC_TUSB_TASK_STACK   6144
#define UAC_TUSB_TASK_PRIO    5
#define UAC_TUSB_IDLE_MS      20
#define UAC_ENUM_TIMEOUT_MS   4000u   /* 等待 UAC 设备枚举的最长时间 */

/* ── 错误码 → 英文 errmsg(全英文,失败页显示) ─────────────── */

const char *eos_spotify_strerror(eos_spotify_err_t e)
{
    switch (e)
    {
        case EOS_SPOTIFY_OK:
            return "OK";
        case EOS_SPOTIFY_ERR_NO_DIR:
            return "Music folder not found. Create /sdcard/spotify and add .mp3 or .wav files.";
        case EOS_SPOTIFY_ERR_USB_BUSY:
            return "USB port in use by debug console (USB-Serial-JTAG). "
                   "Disconnect idf.py monitor to use USB audio.";
        case EOS_SPOTIFY_ERR_NO_EARPHONE:
            return "No earphone connection detected. Please connect a USB-C earphone.";
        case EOS_SPOTIFY_ERR_NOT_AUDIO:
            return "Connected USB device is not an audio device.";
        case EOS_SPOTIFY_ERR_REMOUNT:
            return "Failed to re-mount the SD card after audio playback.";
        case EOS_SPOTIFY_ERR_USB_STACK:
            return "Failed to start the USB audio stack.";
        default:
            return "Unknown error.";
    }
}

/* ── 会话状态 ─────────────────────────────────────────────── */

static bool s_active = false;                 /* UAC 会话进行中        */
static bool s_ready = false;                  /* 已枚举到 UAC 播放设备 */
static usb_phy_handle_t s_phy_hdl = NULL;     /* USB-OTG 主机 PHY 句柄 */
static bool s_phy_held = false;               /* PHY 是否已抢到        */

static TaskHandle_t s_tusb_task = NULL;
static volatile bool s_tusb_run = false;
static volatile int s_tusb_init = 0;          /* 0=进行中 1=成功 -1=失败 */

/* ── 事件泵任务(唯一新增任务) ────────────────────────────── */

static void _uac_tusb_task(void *arg)
{
    (void)arg;

    /* 必须与 tuh_task() 同任务上下文(usbh.h 显式要求) */
    const tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,   /* ESP32-S3 内置 OTG 为 Full Speed */
    };
    if (!tuh_rhport_init(0, &rh_init))
    {
        s_tusb_init = -1;
        s_tusb_run = false;
        s_tusb_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    s_tusb_init = 1;

    while (s_tusb_run)
    {
        tuh_task_ext(UAC_TUSB_IDLE_MS, false);
    }

    /* 收尾同样在本任务内(与 init/tuh_task 同上下文) */
    tuh_deinit(0);
    s_tusb_task = NULL;
    vTaskDelete(NULL);
}

/* ── 探测 ─────────────────────────────────────────────────── */

bool eos_spotify_uac_usb_busy(void)
{
    return board_spotify_usj_online();
}

/* ── 会话开始 ─────────────────────────────────────────────── */

bool eos_spotify_uac_begin(eos_spotify_err_t *out_err)
{
    if (s_active)
    {
        if (out_err)
            *out_err = EOS_SPOTIFY_OK;
        return s_ready;
    }

    if (out_err)
        *out_err = EOS_SPOTIFY_OK;

    /* 1) 申请【内部 USB PHY】并路由给 USB-OTG 主机模式 —— 把 USB 口从
     *    USB-Serial-JTAG 控制台抢过来(方案 B,与 USB MSC 同构)。
     *    必须在 tuh_rhport_init() 之前完成(espressif__tinyusb 自身不配 PHY)。
     *    注:若 monitor 正占用 USJ,这里同样能抢到;抢占失败才报 USB_BUSY。 */
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_HOST,       /* ← 关键:Host 模式(区别于 MSC 的 DEVICE) */
        .otg_speed = USB_PHY_SPEED_UNDEFINED,
    };
    esp_err_t perr = usb_new_phy(&phy_conf, &s_phy_hdl);
    if (perr != ESP_OK)
    {
        EOS_LOG_E("usb_new_phy(HOST) failed: %s", esp_err_to_name(perr));
        s_phy_hdl = NULL;
        if (out_err)
            *out_err = eos_spotify_uac_usb_busy() ? EOS_SPOTIFY_ERR_USB_BUSY
                                                  : EOS_SPOTIFY_ERR_USB_STACK;
        return false;
    }
    s_phy_held = true;

    /* 2) 起 TinyUSB Host 事件泵任务(tuh_rhport_init + tuh_task 必须同任务) */
    s_tusb_init = 0;
    s_tusb_run = true;
    if (xTaskCreate(_uac_tusb_task, "eos_uac_tusb", UAC_TUSB_TASK_STACK, NULL,
                    UAC_TUSB_TASK_PRIO, &s_tusb_task) != pdPASS)
    {
        EOS_LOG_E("create uac tusb task failed");
        s_tusb_run = false;
        usb_del_phy(s_phy_hdl);
        s_phy_hdl = NULL;
        s_phy_held = false;
        if (out_err)
            *out_err = EOS_SPOTIFY_ERR_USB_STACK;
        return false;
    }

    /* 3) 等任务内主机栈初始化结果(正常 <20ms) */
    for (int i = 0; i < 40 && s_tusb_init == 0; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (s_tusb_init != 1)
    {
        EOS_LOG_E("tuh_rhport_init failed");
        s_tusb_run = false;
        for (int i = 0; i < 20 && s_tusb_task != NULL; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_tusb_task != NULL)
        {
            vTaskDelete(s_tusb_task);
            s_tusb_task = NULL;
        }
        usb_del_phy(s_phy_hdl);
        s_phy_hdl = NULL;
        s_phy_held = false;
        if (out_err)
            *out_err = EOS_SPOTIFY_ERR_USB_STACK;
        return false;
    }

    s_active = true;

    /* 4) 等待 UAC 设备枚举(轮询本 App 自带的 UAC Host 驱动状态) */
    for (int i = 0; i < (int)(UAC_ENUM_TIMEOUT_MS / 20); i++)
    {
        if (eos_spotify_uac_host_ready())
        {
            s_ready = true;
            eos_spotify_uac_host_stream(true);
            EOS_LOG_I("UAC ready: %lu Hz %u ch",
                     (unsigned long)eos_spotify_uac_host_sample_rate(),
                     eos_spotify_uac_host_channels());
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    /* 超时:区分"什么都没插/仅充电"与"插了但不是音频设备" */
    EOS_LOG_W("UAC enumeration timeout");
    eos_spotify_uac_end();
    if (out_err)
        *out_err = EOS_SPOTIFY_ERR_NO_EARPHONE;
    return false;
}

bool eos_spotify_uac_ready(void)
{
    return s_ready;
}

bool eos_spotify_uac_connected(void)
{
    return s_ready && eos_spotify_uac_host_connected();
}

/* ── 音频推送 ─────────────────────────────────────────────── */

int32_t eos_spotify_uac_write(const void *pcm, uint32_t bytes)
{
    if (!s_ready)
    {
        return -1;
    }
    return eos_spotify_uac_host_write(pcm, bytes);
}

void eos_spotify_uac_stop_stream(void)
{
    eos_spotify_uac_host_stream(false);
}

/* ── 会话结束(对应退出流程第 3–6 步) ─────────────────────── */

void eos_spotify_uac_end(void)
{
    if (!s_active && !s_ready && !s_phy_held)
    {
        return;
    }

    /* 步骤 3:停止等时端点发送 */
    eos_spotify_uac_host_stream(false);
    s_ready = false;

    /* 步骤 4:去初始化 TinyUSB(必须在事件泵任务内完成) */
    if (s_tusb_task != NULL)
    {
        s_tusb_run = false;
        for (int i = 0; i < 100 && s_tusb_task != NULL; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_tusb_task != NULL)
        {
            EOS_LOG_W("uac tusb task busy, force delete");
            vTaskDelete(s_tusb_task);
            s_tusb_task = NULL;
        }
    }

    /* 步骤 5:释放 USB-OTG PHY */
    if (s_phy_hdl != NULL)
    {
        usb_del_phy(s_phy_hdl);
        s_phy_hdl = NULL;
    }

    /* 步骤 6:把 PHY 归还 USB-Serial-JTAG,恢复 console(monitor)日志。
     * usb_del_phy() 只解除 pull/pad 覆盖,不会自动拨回 sw_usb_phy_sel。 */
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
#endif

    s_phy_held = false;
    s_active = false;
    EOS_LOG_I("UAC stopped");
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
