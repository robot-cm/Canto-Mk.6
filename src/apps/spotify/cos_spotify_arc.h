/**
 * @file cos_spotify_arc.h
 * @brief 弧形调节控件(音量 / 播放速度),专为 240×240 圆屏设计。
 *
 * 交付批次:Batch 4。
 *
 * ── 视觉与几何 ──────────────────────────────────────────────
 *   · 弧角固定 120°,左右各一条:
 *       右侧 → 音量条(0–100)
 *       左侧 → 播放速度条(0.5x–4.0x)
 *   · 弧以屏幕中心为圆心,半径接近屏半径;
 *   · 中间留出 120° 缺口给歌词区,避免遮挡。
 *
 * ── 手势(任务书 §3.2 第 4/5 条) ────────────────────────────
 *   1. 长按 1s      → 进入"滑动调节"模式,并把弧条**加粗**作为反馈;
 *   2. 滑动         → 沿弧方向位置映射到数值(线性);
 *   3. 长按 3s      → 复位到默认值(音量 50,速度 1.0x);
 *   4. 松手         → 退出调节模式,弧条恢复原粗细。
 *
 *   实现方式:不用 LVGL 手写事件循环,而是:
 *     · 弧条本体用 LVGL 的 `lv_arc`(原生支持角度范围与拖动)
 *     · 长按用 lv_timer 轮询按下状态(50ms 粒度,复用 App 的 tick 频率)
 *     · 滑动用 lv_arc 的 value_changed 事件(拖动即触发)
 *   lv_arc 的 touch 拖动天然给出角度 → 数值映射,避免自行做极坐标换算。
 */
#ifndef COS_SPOTIFY_ARC_H
#define COS_SPOTIFY_ARC_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/** 弧角:120°。 */
#define COS_SPOTIFY_ARC_ANGLE   120

/** 长按进入调节的时长(ms)。 */
#define COS_SPOTIFY_ARC_HOLD_EDIT_MS   1000

/** 长按复位的时长(ms)。 */
#define COS_SPOTIFY_ARC_HOLD_RESET_MS  3000

/** 数值变化回调(参数为归一化后的实际值)。 */
typedef void (*cos_spotify_arc_cb_t)(void *user, float value);

typedef struct cos_spotify_arc_s cos_spotify_arc_t;

/**
 * @brief 创建弧控件。
 * @param parent      宿主(通常是播放页 screen)
 * @param right_side  true = 放屏幕右侧;false = 放左侧
 * @param min_value   最小值(如 0 或 0.5)
 * @param max_value   最大值(如 100 或 4.0)
 * @param def_value   默认值(长按 3s 复位到此值)
 * @param on_change   数值变化回调
 * @param user        回调上下文
 */
cos_spotify_arc_t *cos_spotify_arc_create(lv_obj_t *parent, bool right_side,
                                         float min_value, float max_value,
                                         float def_value,
                                         cos_spotify_arc_cb_t on_change, void *user);

/** @brief 销毁弧控件(含内部定时器)。 */
void cos_spotify_arc_destroy(cos_spotify_arc_t *arc);

/**
 * @brief 周期性调用(由 App 的 lv_timer 驱动),负责长按计时。
 * @note 必须在 UI 任务上下文调用。
 */
void cos_spotify_arc_tick(cos_spotify_arc_t *arc);

/** @brief 以程序方式设置数值(不触发回调)。 */
void cos_spotify_arc_set_value(cos_spotify_arc_t *arc, float value);

/** @brief 当前数值。 */
float cos_spotify_arc_get_value(const cos_spotify_arc_t *arc);

/** @brief 复位到默认值(与长按 3s 等效)。 */
void cos_spotify_arc_reset(cos_spotify_arc_t *arc);

/** @brief 该弧控件下方的 LVGL 对象(供布局/调试)。 */
lv_obj_t *cos_spotify_arc_obj(const cos_spotify_arc_t *arc);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* COS_SPOTIFY_ARC_H */
