/**
 * @file eos_spotify_uac.h
 * @brief Spotify USB UAC 主机层:TinyUSB Host UAC 初始化 / 枚举 / 流控制 / 去初始化。
 *
 * 交付批次:Batch 1 声明接口与错误码表,Batch 2 填充实现。
 *
 * 设计约束:
 *   - 只做"USB-OTG 主机侧 UAC 耳机的识别与等时音频发送",不做解码、不做 UI;
 *   - 每一个失败路径都返回 errcode,绝不 panic / abort;
 *   - 进入前经 usb_new_phy(USB_PHY_CTRL_OTG, USB_OTG_MODE_HOST) 抢占 PHY,
 *     退出时把 PHY 归还 USB-Serial-JTAG(与 USB MSC 同构);
 *   - TinyUSB 内部任务由 tusb_init() 创建,本层不自建任务。
 */
#ifndef EOS_SPOTIFY_UAC_H
#define EOS_SPOTIFY_UAC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/**
 * @brief UAC 会话错误码(全屏失败页显示的 errcode,英文 errmsg 见 strerror)。
 */
typedef enum
{
    EOS_SPOTIFY_OK = 0,
    EOS_SPOTIFY_ERR_NO_DIR = 1,      /* /sdcard/spotify missing                 */
    EOS_SPOTIFY_ERR_USB_BUSY = 2,    /* USB port occupied by debug console      */
    EOS_SPOTIFY_ERR_NO_EARPHONE = 3, /* No UAC device enumerated (charging only)*/
    EOS_SPOTIFY_ERR_NOT_AUDIO = 4,   /* Enumerated USB device is not UAC audio  */
    EOS_SPOTIFY_ERR_REMOUNT = 5,     /* Failed to re-mount SD after playback    */
    EOS_SPOTIFY_ERR_USB_STACK = 6,   /* Failed to start USB audio stack         */
} eos_spotify_err_t;

/** @brief errcode → 英文 errmsg(失败页显示用)。 */
const char *eos_spotify_strerror(eos_spotify_err_t e);

/* ── 探测阶段(不改变系统状态) ───────────────────────────── */

/**
 * @brief USB-OTG 是否已被其它控制器(调试控制台 / MSC)占用。
 * @return true 表示需要先抢占 PHY(方案 B)或直接失败。
 */
bool eos_spotify_uac_usb_busy(void);

/* ── UAC 会话 ────────────────────────────────────────────── */

/**
 * @brief 进入 UAC:抢占 USB PHY → 起 TinyUSB Host → 等待 UAC 设备枚举。
 * @param[out] out_err 失败时写入 errcode(可为 NULL)。
 * @return true 成功枚举到 UAC 播放设备。
 * @note 内部已包含 4s 枚举超时;失败时内部回滚(PHY 归还)。
 */
bool eos_spotify_uac_begin(eos_spotify_err_t *out_err);

/** @brief 是否已成功枚举到可用的 UAC 播放设备。 */
bool eos_spotify_uac_ready(void);

/** @brief UAC 耳机是否仍在连接(用于拔线检测)。 */
bool eos_spotify_uac_connected(void);

/**
 * @brief 推送一段 PCM 到 UAC 等时端点(非阻塞,按可用空间部分写入)。
 * @param pcm     PCM 数据(内部 DMA 安全缓冲)
 * @param bytes   字节数
 * @return 实际接受的字节数;<0 表示失败。
 */
int32_t eos_spotify_uac_write(const void *pcm, uint32_t bytes);

/** @brief 请求停止等时端点发送(退出流程第 3 步)。 */
void eos_spotify_uac_stop_stream(void);

/**
 * @brief 退出 UAC:停流 → 去初始化 TinyUSB → 归还 PHY(退出流程第 1–6 步)。
 * @note 幂等;未进入时调用安全。SD 重挂载由 App 层负责。
 */
void eos_spotify_uac_end(void);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_UAC_H */
