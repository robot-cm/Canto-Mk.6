/**
 * @file eos_spotify_mp3.h
 * @brief 轻量 MP3 (MPEG-1/2 Layer III) 解码器 —— 本 App 自带。
 *
 * 背景:仓库内无 minimp3 / helix。为保持"App 自包含、不改第三方目录"的约束,
 *       本解码器以纯 C 实现,覆盖 USB-C 耳机播放场景的常见 MP3:
 *         · MPEG-1 Layer III(32/44.1/48 kHz)
 *         · MPEG-2 / MPEG-2.5 Layer III(减半/四分之一采样率)
 *         · 单声道 / 立体声 / Joint Stereo(MS + Intensity)
 *         · 比特率 32–320 kbps(CBR/VBR 均可)
 *       输出固定为**交错 16-bit LE PCM**(与 UAC 等时端点直接对接)。
 *
 * 说明:为控制 Flash 占用,本实现只做 Layer III(不含 Layer I/II 解码),
 *       且不实现浮点 IMDCT 的完全高精度版本——使用定点运算,
 *       在 240MHz 的 ESP32-S3 上可实时解码 44.1kHz 立体声。
 */
#ifndef EOS_SPOTIFY_MP3_H
#define EOS_SPOTIFY_MP3_H

#include <stdbool.h>
#include <stdint.h>

#include "eos_service_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

typedef struct eos_spotify_mp3_s eos_spotify_mp3_t;

/** @brief 创建解码器实例(PSRAM 优先)。 */
eos_spotify_mp3_t *eos_spotify_mp3_create(void);

/** @brief 销毁解码器,释放全部内存。 */
void eos_spotify_mp3_destroy(eos_spotify_mp3_t *mp3);

/**
 * @brief 打开文件并解析首个有效帧头(得到采样率/声道/时长估算)。
 * @return true 成功。
 */
bool eos_spotify_mp3_open(eos_spotify_mp3_t *mp3, eos_file_t fp, uint32_t file_size);

/**
 * @brief 读取并解码一帧。
 * @param[out] out_frames  解出的帧数(每帧含 channels 个样点)
 * @param[out] out_rate    采样率(Hz)
 * @param[out] out_ch      声道数
 * @return 0 成功;<0 文件尾或错误。
 */
int eos_spotify_mp3_read_frame(eos_spotify_mp3_t *mp3, int16_t *pcm, int max_frames,
                               int *out_frames, uint32_t *out_rate, uint8_t *out_ch);

/** @brief 采样率(Hz)。 */
uint32_t eos_spotify_mp3_sample_rate(const eos_spotify_mp3_t *mp3);

/** @brief 声道数。 */
uint8_t eos_spotify_mp3_channels(const eos_spotify_mp3_t *mp3);

/** @brief 时长估算(ms);基于首帧位率 × 文件大小,对 CBR 较准。 */
uint32_t eos_spotify_mp3_duration_ms(const eos_spotify_mp3_t *mp3);

/** @brief 是否已到文件尾。 */
bool eos_spotify_mp3_eos(const eos_spotify_mp3_t *mp3);

/** @brief 跳到指定毫秒(按平均帧长估算,尽力而为)。 */
void eos_spotify_mp3_seek_ms(eos_spotify_mp3_t *mp3, uint32_t ms);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_MP3_H */
