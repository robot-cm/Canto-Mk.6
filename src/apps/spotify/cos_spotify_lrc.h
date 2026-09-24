/**
 * @file eos_spotify_lrc.h
 * @brief LRC 歌词解析与时间戳检索。
 *
 * 交付批次:Batch 3。
 *
 * 支持的 LRC 语法:
 *   · 时间标签 [mm:ss.xx] / [mm:ss.xxx] / [mm:ss] / [m:ss]
 *   · 一行多个时间标签:[00:12.00][00:45.30]副歌
 *   · 元数据标签 [ti:]/[ar:]/[al:]/[by:]/[offset:±ms] 被识别并跳过
 *   · 空行、CRLF、UTF-8/GB2312 混合(仅按字节存储,由 LVGL 字体链渲染)
 *
 * 不支持的:
 *   · [00:12.00]<00:12.50> 类的逐字增强标签(会被当作普通文本保留)
 *
 * 内存:解析结果放 PSRAM;每行歌词单独分配(便于按需释放)。
 */
#ifndef EOS_SPOTIFY_LRC_H
#define EOS_SPOTIFY_LRC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/** 单行歌词在屏幕上的显示时长上限(ms):用于计算滚动速度。
 *  最后一行没有"下一行"时,使用该值作为虚拟时长。 */
#define EOS_SPOTIFY_LRC_TAIL_MS   4000u

/** 单条歌词最大字节数(含中文,UTF-8 下 1 汉字 3 字节)。 */
#define EOS_SPOTIFY_LRC_LINE_MAX  192

typedef struct eos_spotify_lrc_s eos_spotify_lrc_t;

/**
 * @brief 从文件加载 .lrc。文件不存在或解析失败返回 NULL。
 * @param path .lrc 文件完整路径
 */
eos_spotify_lrc_t *eos_spotify_lrc_load(const char *path);

/** @brief 释放歌词对象。 */
void eos_spotify_lrc_free(eos_spotify_lrc_t *lrc);

/** @brief 歌词行数。 */
int eos_spotify_lrc_count(const eos_spotify_lrc_t *lrc);

/**
 * @brief 查找当前时间对应的歌词行下标。
 * @param ms 当前播放位置(毫秒,已含 offset 修正)
 * @return 行下标(0..count-1);早于第一行时返回 -1。
 */
int eos_spotify_lrc_index_at(const eos_spotify_lrc_t *lrc, uint32_t ms);

/** @brief 取第 idx 行的文本(UTF-8,NUL 结尾);越界返回 NULL。 */
const char *eos_spotify_lrc_line(const eos_spotify_lrc_t *lrc, int idx);

/** @brief 取第 idx 行的时间戳(ms);越界返回 0。 */
uint32_t eos_spotify_lrc_time(const eos_spotify_lrc_t *lrc, int idx);

/**
 * @brief 第 idx 行的持续时长(ms)= 下一行时间 - 本行时间;
 *        最后一行用 EOS_SPOTIFY_LRC_TAIL_MS。
 * @return 时长(ms),至少 300ms,避免除零与过快的滚动。
 */
uint32_t eos_spotify_lrc_duration(const eos_spotify_lrc_t *lrc, int idx);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_LRC_H */
