/**
 * @file eos_spotify_player.h
 * @brief Spotify 播放界面(文件树 + 播放态 UI),供 eos_spotify.c 调用。
 *
 * 交付批次:Batch 3(文件树 + 播放 UI + 三行歌词 + 进度条)。
 *           Batch 4 会在本模块上叠加音量弧 / 速度弧手势。
 *
 * ── 界面结构(240×240 圆屏) ─────────────────────────────────
 *   文件树态:
 *     · 顶部:标题 "Spotify" + 歌曲数
 *     · 主体:树形列表(目录可展开,点击文件行右侧 ▶ 播放)
 *     · 底部:Exit 按钮
 *
 *   播放态:
 *     · 顶部(约 y=8..30):滚动显示歌曲名
 *     · 中部(约 y=62..150):三行歌词,中间行高亮
 *     · 下部(约 y=168..178):进度条(可点击跳转)
 *     · 最下(约 y=186..222):⏮  ⏪15  ⏯  ⏩15  ⏭
 *     · 左/右两侧留出弧条区域(Batch 4)
 *
 * ── 歌词滚动规则(任务书 §3.1 第 3 条) ─────────────────────
 *   · 每行显示时长为该行到下一行的间隔(最后一行用固定 4s);
 *   · 若文本宽度 > 可用宽,则水平滚动,速度 = (文本宽 - 可用宽 + 余量) / 显示时长;
 *   · 每 50ms 刷新一次滚动位置(LVGL 定时器,复用现有 tick)。
 */
#ifndef EOS_SPOTIFY_PLAYER_H
#define EOS_SPOTIFY_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/** 播放界面句柄。 */
typedef struct eos_spotify_player_s eos_spotify_player_t;

/** 请求返回文件树(由播放界面内部按钮触发)。 */
typedef void (*eos_spotify_back_cb_t)(void *user);

/** 请求退出 App(错误/长按返回)。 */
typedef void (*eos_spotify_exit_cb_t)(void *user);

/**
 * @brief 在给定父对象上构建整个 Spotify 界面(文件树 + 播放态)。
 * @param parent   宿主容器(由 App 的 activity view 提供)
 * @param on_back  用户在播放态请求回到文件树时回调
 * @param on_exit  用户请求退出 App 时回调
 * @param user     回调上下文
 * @return 句柄;失败返回 NULL。
 */
eos_spotify_player_t *eos_spotify_player_create(lv_obj_t *parent,
                                               eos_spotify_back_cb_t on_back,
                                               eos_spotify_exit_cb_t on_exit,
                                               void *user);

/** @brief 销毁全部 LVGL 对象与内部资源。 */
void eos_spotify_player_destroy(eos_spotify_player_t *p);

/**
 * @brief 刷新(由 App 的 lv_timer 每 50ms 调用一次)。
 *        内部负责:解码分片 → UAC 推送 → 歌词高亮/滚动 → 进度条更新。
 */
void eos_spotify_player_tick(eos_spotify_player_t *p);

/** @brief 是否处于播放态。 */
bool eos_spotify_player_is_playing(const eos_spotify_player_t *p);

/** @brief 用户在文件树里点了播放,或从外部触发播放。 */
void eos_spotify_player_play_file(eos_spotify_player_t *p, const char *path);

/**
 * @brief 返回文件树页(不停止播放;音频继续)。
 * @return true 表示确实从播放页切回了文件树。
 */
bool eos_spotify_player_show_browser(eos_spotify_player_t *p);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_PLAYER_H */
