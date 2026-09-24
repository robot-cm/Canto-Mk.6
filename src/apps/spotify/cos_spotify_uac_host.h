/**
 * @file eos_spotify_uac_host.h
 * @brief USB Audio Class (UAC) Host 类驱动 —— 供 eos_spotify_uac.c 使用。
 *
 * 背景:TinyUSB 0.21 只内置 UAC **Device**,无 UAC **Host**。
 *       本驱动参照内置 midi_host.c 的模式自实现,并通过 weak 钩子
 *       usbh_app_driver_get_cb() 注册到 USBH 核心。
 *
 * 支持子集:UAC1.0 / Format Type I / PCM 16-bit / 44.1k & 48k / 1-2 声道。
 */
#ifndef EOS_SPOTIFY_UAC_HOST_H
#define EOS_SPOTIFY_UAC_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/** @brief 等时 OUT 单块发送缓冲大小(必须 ≥ wMaxPacketSize 且为其整数倍)。
 *  48kHz 立体声 16bit、每帧 1 个包时 mps≈192B;留 1KB 余量以支持
 *  高 mps 耳机(如每微帧多包)。内部 RAM + 4 字节对齐(DWHCI DMA 要求)。 */
#define EOS_SPOTIFY_UAC_TX_BUF_SIZE   1024

/** @brief 是否已枚举到可用的 UAC 播放端点。 */
bool eos_spotify_uac_host_ready(void);

/** @brief UAC 设备是否仍在总线上(拔线检测用)。 */
bool eos_spotify_uac_host_connected(void);

/** @brief 协商到的采样率(Hz),未就绪时为 0。 */
uint32_t eos_spotify_uac_host_sample_rate(void);

/** @brief 协商到的声道数(1 或 2),未就绪时为 0。 */
uint8_t eos_spotify_uac_host_channels(void);

/**
 * @brief 向等时 OUT 端点推送一块 PCM(非阻塞,等时语义允许丢弃)。
 * @param pcm    PCM 数据指针
 * @param bytes  字节数(内部会裁到 wMaxPacketSize 整数倍)
 * @return 实际接受的字节数;0 = 本帧丢弃(忙);<0 = 未就绪/失败。
 */
int32_t eos_spotify_uac_host_write(const void *pcm, uint32_t bytes);

/** @brief 开始/停止等时流。停流时丢弃 tx_busy 状态。 */
void eos_spotify_uac_host_stream(bool on);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_UAC_HOST_H */
