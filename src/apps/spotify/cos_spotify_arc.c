/**
 * @file cos_spotify_arc.c
 * @brief 弧形调节控件实现(音量 / 速度)。
 *
 * ── 为什么用 lv_arc ─────────────────────────────────────────
 *   lv_arc 原生提供:角度范围、当前值、拖动改变值(内置极坐标换算)、
 *   以及"按下/拖动/释放"事件。我们只需要在其上叠加:
 *     · 120° 角度窗口的定位;
 *     · 长按 1s / 3s 的计时状态机;
 *     · "进入调节"时把 arc 宽度加粗作为视觉反馈。
 *   这样避免了自行实现极坐标命中测试与拖动插值。
 *
 * ── 长按计时 ────────────────────────────────────────────────
 *   不使用额外的 esp_timer,而是由 App 的 lv_timer(50ms)调用
 *   cos_spotify_arc_tick(),内部累计"已按下时长":
 *     ≥1000ms → edit_mode = true,加粗
 *     ≥3000ms → 复位到默认值(并保持 edit_mode)
 *   松手(RELEASED / PRESS_LOST)清零计时并退出 edit_mode。
 *
 * ── 防止与 swipe_back 冲突 ──────────────────────────────────
 *   弧控件本身是 CLICKABLE 且会消费按下事件,因此其热区内不会触发
 *   屏幕级滑动返回;弧外区域仍可正常滑动退出 App。
 */
#include "cos_spotify_arc.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <math.h>
#include <string.h>

#include "cos_mem.h"
#include "cos_font.h"

#define COS_LOG_TAG "SpotifyArc"
#include "cos_log.h"

/* ── 视觉参数 ─────────────────────────────────────────────── */

#define ARC_SIZE        200      /* 弧控件包围盒边长        */
#define ARC_WIDTH_IDLE  9        /* 静止时弧条宽度          */
#define ARC_WIDTH_EDIT  16       /* 调节模式加粗(反馈)     */

#define C_TRACK     0x2A2D31     /* 未填充轨道 */
#define C_FILL_VOL  0x1ED760     /* 音量:Spotify 绿 */
#define C_FILL_SPD  0x4A9EFF     /* 速度:蓝色,区分两者 */
#define C_KNOB      0xFFFFFF

/* ── 结构 ─────────────────────────────────────────────────── */

struct cos_spotify_arc_s
{
    lv_obj_t *arc;
    lv_obj_t *label;         /* 数值文本 */

    bool  right_side;
    float min_value;
    float max_value;
    float def_value;
    float value;

    bool     edit_mode;      /* 已长按 1s,进入滑动调节 */
    bool     pressed;
    uint32_t press_start;    /* 按下时刻(lv_tick)    */
    bool     reset_done;     /* 本次长按是否已执行复位 */

    cos_spotify_arc_cb_t on_change;
    void *user;
};

/* ── 数值 ↔ 弧值 映射 ─────────────────────────────────────── */

/* lv_arc 用 0..100 的整数表示值;这里做线性映射。 */
static int _val_to_arc(const cos_spotify_arc_t *a, float v)
{
    if (a->max_value <= a->min_value)
    {
        return 0;
    }
    float t = (v - a->min_value) / (a->max_value - a->min_value);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (int)(t * 100.0f + 0.5f);
}

static float _arc_to_val(const cos_spotify_arc_t *a, int av)
{
    float t = (float)av / 100.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return a->min_value + t * (a->max_value - a->min_value);
}

/* ── 数值文本 ─────────────────────────────────────────────── */

static void _update_label(cos_spotify_arc_t *a)
{
    if (a->label == NULL)
    {
        return;
    }
    char buf[16];
    if (a->max_value > 10.0f)
    {
        /* 音量:整数 */
        lv_label_set_text_fmt(a->label, "%d", (int)(a->value + 0.5f));
    }
    else
    {
        /* 速度:一位小数,如 1.0x */
        lv_label_set_text_fmt(a->label, "%.1fx", (double)a->value);
    }
}

/* ── 事件 ─────────────────────────────────────────────────── */

static void _arc_value_changed(lv_event_t *e)
{
    cos_spotify_arc_t *a = (cos_spotify_arc_t *)lv_event_get_user_data(e);
    if (a == NULL)
    {
        return;
    }
    /* 仅在"调节模式"下才响应拖动,避免误触 */
    if (!a->edit_mode)
    {
        /* 把 arc 拉回当前值,防止误拖改变显示 */
        lv_arc_set_value(a->arc, _val_to_arc(a, a->value));
        return;
    }

    int av = lv_arc_get_value(a->arc);
    float nv = _arc_to_val(a, av);
    if (nv != a->value)
    {
        a->value = nv;
        _update_label(a);
        if (a->on_change)
        {
            a->on_change(a->user, nv);
        }
    }
}

static void _arc_pressed(lv_event_t *e)
{
    cos_spotify_arc_t *a = (cos_spotify_arc_t *)lv_event_get_user_data(e);
    if (a == NULL)
    {
        return;
    }
    a->pressed = true;
    a->press_start = lv_tick_get();
    a->reset_done = false;
}

static void _arc_released(lv_event_t *e)
{
    cos_spotify_arc_t *a = (cos_spotify_arc_t *)lv_event_get_user_data(e);
    if (a == NULL)
    {
        return;
    }
    a->pressed = false;
    if (a->edit_mode)
    {
        a->edit_mode = false;
        /* 恢复原粗细,作为"退出调节"的反馈 */
        lv_obj_set_style_arc_width(a->arc, ARC_WIDTH_IDLE, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a->arc, ARC_WIDTH_IDLE, LV_PART_INDICATOR);
        COS_LOG_I("arc: edit end, value=%.2f", (double)a->value);
    }
}

/* ── 长按计时(由 App tick 驱动) ──────────────────────────── */

void cos_spotify_arc_tick(cos_spotify_arc_t *a)
{
    if (a == NULL || !a->pressed)
    {
        return;
    }
    uint32_t held = lv_tick_get() - a->press_start;

    /* 阶段 1:长按 1s → 进入调节模式(加粗反馈) */
    if (!a->edit_mode && held >= COS_SPOTIFY_ARC_HOLD_EDIT_MS)
    {
        a->edit_mode = true;
        lv_obj_set_style_arc_width(a->arc, ARC_WIDTH_EDIT, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a->arc, ARC_WIDTH_EDIT, LV_PART_INDICATOR);
        COS_LOG_I("arc: edit mode on (%s)", a->right_side ? "volume" : "speed");
    }

    /* 阶段 2:长按 3s → 复位到默认值 */
    if (a->edit_mode && !a->reset_done && held >= COS_SPOTIFY_ARC_HOLD_RESET_MS)
    {
        a->reset_done = true;
        a->value = a->def_value;
        lv_arc_set_value(a->arc, _val_to_arc(a, a->value));
        _update_label(a);
        if (a->on_change)
        {
            a->on_change(a->user, a->value);
        }
        COS_LOG_I("arc: reset to %.2f", (double)a->value);
    }
}

/* ── 创建 ─────────────────────────────────────────────────── */

cos_spotify_arc_t *cos_spotify_arc_create(lv_obj_t *parent, bool right_side,
                                         float min_value, float max_value,
                                         float def_value,
                                         cos_spotify_arc_cb_t on_change, void *user)
{
    if (parent == NULL || max_value <= min_value)
    {
        return NULL;
    }

    cos_spotify_arc_t *a = (cos_spotify_arc_t *)cos_malloc(sizeof(*a));
    if (a == NULL)
    {
        return NULL;
    }
    memset(a, 0, sizeof(*a));
    a->right_side = right_side;
    a->min_value = min_value;
    a->max_value = max_value;
    a->def_value = def_value;
    a->value = def_value;
    a->on_change = on_change;
    a->user = user;

    /* 弧控件本体。尺寸放大到屏幕外一部分,只露出内侧弧段,
     * 使弧条紧贴圆屏边缘。 */
    a->arc = lv_arc_create(parent);
    lv_obj_set_size(a->arc, ARC_SIZE, ARC_SIZE);
    lv_obj_center(a->arc);
    /* 左右各移出屏幕,让弧贴着对应侧边缘 */
    lv_obj_align(a->arc, right_side ? LV_ALIGN_CENTER : LV_ALIGN_CENTER,
                 right_side ? 62 : -62, 0);

    /* 120° 窗口:右侧弧占用 [-60°, +60°](以 90° 为中心向下),
     * lv_arc 的 0° 在右侧、顺时针增大。
     *   - 右侧音量条:从 300° 到 60°(即跨越右侧)
     *   - 左侧速度条:从 120° 到 240°(跨越左侧)
     * 这里使用"起始角 + 120° 跨度"的方式设定。 */
    lv_arc_set_rotation(a->arc, right_side ? 300 : 120);
    lv_arc_set_bg_angles(a->arc, 0, COS_SPOTIFY_ARC_ANGLE);
    lv_arc_set_range(a->arc, 0, 100);
    lv_arc_set_value(a->arc, _val_to_arc(a, a->value));

    /* 样式:轨道 + 指示段 + 旋钮 */
    lv_obj_remove_style(a->arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a->arc, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_set_style_arc_width(a->arc, ARC_WIDTH_IDLE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a->arc, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a->arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a->arc, ARC_WIDTH_IDLE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a->arc,
                               lv_color_hex(right_side ? C_FILL_VOL : C_FILL_SPD),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a->arc, true, LV_PART_INDICATOR);

    /* 旋钮:小而亮,便于看到当前位置 */
    lv_obj_set_style_bg_color(a->arc, lv_color_hex(C_KNOB), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(a->arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a->arc, 2, LV_PART_KNOB);

    /* 数值文本放在弧的内侧 */
    a->label = lv_label_create(parent);
    lv_label_set_text(a->label, "");
    lv_obj_set_style_text_color(a->label, lv_color_hex(C_KNOB), 0);
    cos_label_set_font_size(a->label, COS_FONT_SIZE_MICRO);
    _update_label(a);
    /* 贴着弧的内侧边缘 */
    lv_obj_align(a->label, LV_ALIGN_CENTER, right_side ? 82 : -82, 34);

    /* 事件 */
    lv_obj_add_event_cb(a->arc, _arc_value_changed, LV_EVENT_VALUE_CHANGED, a);
    lv_obj_add_event_cb(a->arc, _arc_pressed, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->arc, _arc_released, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->arc, _arc_released, LV_EVENT_PRESS_LOST, a);

    COS_LOG_I("arc: created %s [%.1f..%.1f] def=%.1f",
              right_side ? "volume" : "speed",
              (double)min_value, (double)max_value, (double)def_value);
    return a;
}

void cos_spotify_arc_destroy(cos_spotify_arc_t *a)
{
    if (a == NULL)
    {
        return;
    }
    /* arc / label 由 LVGL 随父对象销毁,这里只释放结构体 */
    cos_free(a);
}

void cos_spotify_arc_set_value(cos_spotify_arc_t *a, float value)
{
    if (a == NULL)
    {
        return;
    }
    if (value < a->min_value) value = a->min_value;
    if (value > a->max_value) value = a->max_value;
    a->value = value;
    lv_arc_set_value(a->arc, _val_to_arc(a, value));
    _update_label(a);
}

float cos_spotify_arc_get_value(const cos_spotify_arc_t *a)
{
    return a ? a->value : 0.0f;
}

void cos_spotify_arc_reset(cos_spotify_arc_t *a)
{
    if (a == NULL)
    {
        return;
    }
    a->value = a->def_value;
    lv_arc_set_value(a->arc, _val_to_arc(a, a->value));
    _update_label(a);
    if (a->on_change)
    {
        a->on_change(a->user, a->value);
    }
}

lv_obj_t *cos_spotify_arc_obj(const cos_spotify_arc_t *a)
{
    return a ? a->arc : NULL;
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
