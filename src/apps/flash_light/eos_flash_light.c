/**
 * @file eos_flash_light.c
 * @brief Flashlight
 */

#include "eos_flash_light.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "eos_theme.h"
#include "eos_config.h"
#include "eos_swipe_panel.h"
#define EOS_LOG_TAG "FlashLight"
#include "eos_log.h"
#include "eos_event.h"
#include "eos_icon.h"
#include "eos_anim.h"
#include "eos_utils.h"
#include "eos_card_pager.h"
#include "eos_watchface.h"
#include "eos_port.h"
#include "eos_service_config.h"
#include "eos_service_display.h"
#include "eos_app_list.h"
#include "eos_lang.h"
#include "eos_basic_widgets.h"
#include "eos_app_header.h"
#include "eos_mem.h"
#include "eos_chrome_manager.h"

/* ============ 画图式色板（用户需求 2026-08 重做：2 页，点色块发光，无屏闪） ============ */
#define _FLASH_PALETTE_HUE_STEPS 8
#define _FLASH_PALETTE_VAL_STEPS 6


/* Macros and Definitions -------------------------------------*/
#define _MASK_OPA LV_OPA_80
#define _OPA_MAX_DIST_DIV 1 /**< Distance to reach maximum opacity */
#define _OPA_SCALE 1000 /**< Scaling factor for proportion calculation */
#define _BRIGHTNESS_SMOOTH_DURATION 300
#define _IMMERSIVE_TAP_MAX_DISPLACEMENT 8
#define _IMMERSIVE_FADE_DURATION 220
#define _INDICATOR_MODE1_ACTIVE_COLOR EOS_COLOR_WHITE
#define _INDICATOR_MODE1_INACTIVE_COLOR EOS_COLOR_BLACK
#define _INDICATOR_MODE2_ACTIVE_COLOR EOS_COLOR_BLACK
#define _INDICATOR_MODE2_INACTIVE_COLOR EOS_COLOR_TEXT_GREY
#define _BRIGHTNESS_DURATION 750
typedef struct
{
    eos_swipe_panel_t *sp;
    lv_obj_t *mask;
} _pressing_user_data_t;

typedef struct
{
    eos_activity_t *activity;
    eos_card_pager_t *cp;
    lv_obj_t *light_page;           /* 页0：手电筒（全屏 custom_color，点击关闭） */
    lv_obj_t *palette_page;         /* 页1：画图式色板（点色块 → 手电筒发光色） */
    lv_color_t custom_color;        /* 当前光色（默认白） */
    bool immersive_mode;
    lv_obj_t *palette_cells[_FLASH_PALETTE_VAL_STEPS * _FLASH_PALETTE_HUE_STEPS]; /* 48 色块 */
    lv_obj_t *sel_cell;             /* 当前选中色块（白框常驻） */
    lv_obj_t *sel_feedback;         /* "已选"提示 label（800ms 后隐藏） */
    lv_timer_t *sel_timer;          /* 提示隐藏定时器（复用） */
    lv_point_t press_start;         /* 触摸起点（过滤拖动误判的 CLICKED） */
    bool press_tracked;
} _flash_light_card_pager_ctx_t;
/* Variables --------------------------------------------------*/

static _pressing_user_data_t *_flash_light_ud = NULL;
static void _flash_light_overlay_pull_back(void);
static void _flash_light_overlay_hide(void);
static void _flash_light_overlay_on_focus(void);
static const eos_chrome_overlay_t _flash_light_overlay = {
    .pull_back = _flash_light_overlay_pull_back,
    .hide = _flash_light_overlay_hide,
    .on_focus = _flash_light_overlay_on_focus,
};

/* Function Implementations -----------------------------------*/
static void _flash_light_on_destroy(eos_activity_t *a);
static inline void _flash_light_delete(_pressing_user_data_t *ud);
lv_obj_t *eos_flash_light_get_touch_obj(void);
static bool _flash_light_swipe_back(eos_activity_t *self, lv_dir_t dir);
static void _flash_light_exit_cb(lv_event_t *e);
static void _flash_light_card_pager_page_changed_cb(eos_card_pager_t *cp, uint8_t current_page_index, void *user_data);
static void _flash_light_card_pager_clicked_cb(lv_event_t *e);
static lv_obj_t *_flash_light_get_indicator_for_page(eos_card_pager_t *cp, lv_obj_t *page);
static void _flash_light_apply_page_visual_state(_flash_light_card_pager_ctx_t *ctx, uint8_t current_page_index);
static void _flash_light_set_indicator_visible_animated(_flash_light_card_pager_ctx_t *ctx,
                                                        bool visible,
                                                        uint32_t duration_ms);

static void _flash_light_apply_color(_flash_light_card_pager_ctx_t *ctx)
{
    EOS_CHECK_PTR_RETURN(ctx && ctx->light_page);
    lv_obj_set_style_bg_color(ctx->light_page, ctx->custom_color, 0);
}

/* "已选"提示显示 800ms 后隐藏（复用 timer，每 tap 零新建） */
static void _flash_light_sel_feedback_timer_cb(lv_timer_t *t)
{
    _flash_light_card_pager_ctx_t *ctx = lv_timer_get_user_data(t);
    EOS_CHECK_PTR_RETURN(ctx);
    if (ctx->sel_feedback && lv_obj_is_valid(ctx->sel_feedback))
        lv_obj_add_flag(ctx->sel_feedback, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
    printf("[flash] sel feedback hidden\n"); fflush(stdout);
}

/* 色板几何（创建与触摸反算共用） */
#define _FLASH_CELL_W 26
#define _FLASH_CELL_H 22
#define _FLASH_CELL_GAP 2
#define _FLASH_PALETTE_X0 ((EOS_DISPLAY_WIDTH - (_FLASH_PALETTE_HUE_STEPS * (_FLASH_CELL_W + _FLASH_CELL_GAP) - _FLASH_CELL_GAP)) / 2)
#define _FLASH_PALETTE_Y0 40

/*
 * card_pager 的透明全屏 touch_area 盖在所有页面上（最上层），
 * 页面/子对象的 CLICKED 永远收不到（实测：选色无反应、点手电筒不关闭）。
 * 故在 touch_area 上统一处理点击与上下滑退出：
 *   页0（手电筒）→ 点击关闭；
 *   页1（色板）  → 按坐标反算色块行列 → 对应颜色发光。
 *   上下滑（任意页）→ 退出 app（framework 层统一处理，见 eos_activity.c）。
 * 左右滑翻页由 slide_widget 的 PRESSED/MOVING 处理，不受影响。
 */

/* 拖动到一半松开也算拖动（位移超 10px），不算点击 → 不触发页0关闭/选色 */
#define _FLASH_TAP_MAX_MOVE 10

/* PRESSED 时记录触摸起点（CLICKED 时比较位移，过滤拖动误判的点击） */
static void _flash_light_press_start_cb(lv_event_t *e)
{
    _flash_light_card_pager_ctx_t *ctx = lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ctx);
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    lv_indev_get_point(indev, &ctx->press_start);
    ctx->press_tracked = true;
}

static void _flash_light_touch_cb(lv_event_t *e)
{
    _flash_light_card_pager_ctx_t *ctx = lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ctx && ctx->cp);

    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    /* 滑动被 LVGL 误判为 CLICKED（view scroll_dir=NONE → scroll_obj 不设置 →
     * release 时照发 CLICKED）：左/右滑不算点击，交给 pager 翻页 */
    lv_dir_t gdir = lv_indev_get_gesture_dir(indev);
    if (gdir == LV_DIR_LEFT || gdir == LV_DIR_RIGHT)
        return;

    /* 拖动到一半松开：位移 < gesture 阈值时 gesture_dir=NONE，CLICKED 照发 →
     * 用 PRESSED 记录的起点比较总位移，超 10px 一律视为拖动，不算点击 */
    if (ctx->press_tracked)
    {
        int dx = pt.x - ctx->press_start.x;
        int dy = pt.y - ctx->press_start.y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > _FLASH_TAP_MAX_MOVE || dy > _FLASH_TAP_MAX_MOVE)
            return;
    }

    if (ctx->cp->current_page_index == 0)
    {
        /* 手电筒页：点击直接关闭 */
        eos_activity_back();
        return;
    }

    if (ctx->cp->current_page_index != 1)
        return;

    /* 色板页：反算行列 */
    int col = (pt.x - _FLASH_PALETTE_X0) / (_FLASH_CELL_W + _FLASH_CELL_GAP);
    int row = (pt.y - _FLASH_PALETTE_Y0) / (_FLASH_CELL_H + _FLASH_CELL_GAP);
    if (col < 0 || col >= _FLASH_PALETTE_HUE_STEPS || row < 0 || row >= _FLASH_PALETTE_VAL_STEPS)
        return;   /* 点空白区忽略 */
    uint32_t hue = (uint32_t)(col * 360 / _FLASH_PALETTE_HUE_STEPS);
    uint32_t val = 100 - (uint32_t)(row * 90 / (_FLASH_PALETTE_VAL_STEPS - 1));
    if (val < 10) val = 10;
    ctx->custom_color = lv_color_hsv_to_rgb(hue, 100, val);
    _flash_light_apply_color(ctx);

    /* 选色反馈：选中框切换 + "已选"提示（800ms 后隐藏，timer 复用） */
    lv_obj_t *cell = ctx->palette_cells[row * _FLASH_PALETTE_HUE_STEPS + col];
    if (cell && lv_obj_is_valid(cell))
    {
        if (ctx->sel_cell && lv_obj_is_valid(ctx->sel_cell))
            lv_obj_set_style_border_width(ctx->sel_cell, 0, 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_style_border_color(cell, lv_color_white(), 0);
        ctx->sel_cell = cell;
    }
    if (ctx->sel_feedback)
    {
        lv_obj_remove_flag(ctx->sel_feedback, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ctx->sel_feedback, "已选");
        if (ctx->sel_timer)
        {
            lv_timer_reset(ctx->sel_timer);
            lv_timer_resume(ctx->sel_timer);
        }
    }
    printf("[flash] sel feedback: cell=%p border=2 feedback_visible=1\n", (void *)cell); fflush(stdout);
}

/* 色板页：8 色相 × 6 亮度 = 48 色块（画图选色式，点哪是哪） */
static void _flash_light_create_palette_page(_flash_light_card_pager_ctx_t *ctx, eos_card_pager_t *cp)
{
    lv_obj_t *page = eos_card_pager_create_page(cp);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x101418), 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    ctx->palette_page = page;
    ctx->custom_color = EOS_COLOR_WHITE;

    for (int v = 0; v < _FLASH_PALETTE_VAL_STEPS; v++)
    {
        for (int h = 0; h < _FLASH_PALETTE_HUE_STEPS; h++)
        {
            uint32_t hue = (uint32_t)(h * 360 / _FLASH_PALETTE_HUE_STEPS);
            uint32_t val = 100 - (uint32_t)(v * 90 / (_FLASH_PALETTE_VAL_STEPS - 1));
            if (val < 10) val = 10;
            lv_color_t c = lv_color_hsv_to_rgb(hue, 100, val);
            lv_obj_t *cell = lv_obj_create(page);
            lv_obj_set_size(cell, _FLASH_CELL_W, _FLASH_CELL_H);
            lv_obj_set_pos(cell, _FLASH_PALETTE_X0 + h * (_FLASH_CELL_W + _FLASH_CELL_GAP),
                           _FLASH_PALETTE_Y0 + v * (_FLASH_CELL_H + _FLASH_CELL_GAP));
            lv_obj_set_style_bg_color(cell, c, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);   /* 点击由 touch_area 统一处理 */
            ctx->palette_cells[v * _FLASH_PALETTE_HUE_STEPS + h] = cell;
        }
    }

    /* "已选"提示 label（顶部，选中色块时显示 800ms） */
    lv_obj_t *fb = lv_label_create(page);
    lv_obj_set_size(fb, _FLASH_PALETTE_HUE_STEPS * (_FLASH_CELL_W + _FLASH_CELL_GAP), 16);
    lv_obj_align(fb, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(fb, "已选");
    lv_obj_set_style_text_align(fb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(fb, lv_color_white(), 0);
    lv_obj_set_style_text_opa(fb, LV_OPA_COVER, 0);
    lv_obj_add_flag(fb, LV_OBJ_FLAG_HIDDEN);
    ctx->sel_feedback = fb;
    ctx->sel_cell = NULL;

    ctx->sel_timer = lv_timer_create(_flash_light_sel_feedback_timer_cb, 800, ctx);
    if (ctx->sel_timer)
        lv_timer_pause(ctx->sel_timer);
}

static const eos_activity_lifecycle_t _flash_light_lifecycle = {
    .on_enter = NULL,
    .on_destroy = _flash_light_on_destroy,
    .on_swipe_back = _flash_light_swipe_back,
};

static void _flash_light_obj_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (!obj || !lv_obj_is_valid(obj))
        return;

    lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
}

static void _flash_light_indicator_fade_out_ready_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (!obj || !lv_obj_is_valid(obj))
        return;

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
}

static void _flash_light_on_destroy(eos_activity_t *a)
{
    lv_obj_t *view = eos_activity_get_view(a);
    _flash_light_card_pager_ctx_t *ctx = view ? (_flash_light_card_pager_ctx_t *)lv_obj_get_user_data(view) : NULL;
    if (ctx)
    {
        /* exit_btn removed by user request (2026-08): 去掉退出开关 */

        if (ctx->sel_timer)
        {
            lv_timer_delete(ctx->sel_timer);
            ctx->sel_timer = NULL;
        }

        if (view && lv_obj_is_valid(view))
        {
            lv_obj_set_user_data(view, NULL);
        }

        eos_free(ctx);
    }

    /* 压感 show() 版本的清理（enter 版 _flash_light_ud 恒 NULL，勿传 NULL 调
     * _flash_light_delete —— 会触发 NULL pointer 误报） */
    if (_flash_light_ud)
        _flash_light_delete(_flash_light_ud);
    eos_display_restore(_BRIGHTNESS_DURATION);
}

static inline void _flash_light_delete(_pressing_user_data_t *ud)
{
    EOS_CHECK_PTR_RETURN(ud);

    if (ud->sp)
        eos_swipe_panel_delete(ud->sp);

    if (ud->mask && lv_obj_is_valid(ud->mask))
        lv_obj_delete_async(ud->mask);

    ud->mask = NULL;
    ud->sp = NULL;

    if (_flash_light_ud == ud)
        _flash_light_ud = NULL;

    eos_free(ud);
    EOS_LOG_I("Flash light deleted");
}

static void _swipe_panel_pull_back_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ud);

    int32_t swipe_obj_coord_y = lv_obj_get_y(ud->sp->swipe_obj);
    if (swipe_obj_coord_y >= EOS_DISPLAY_HEIGHT)
    {
        _flash_light_delete(ud);
        eos_display_restore(_BRIGHTNESS_DURATION);
    }
}

static void _flash_light_closed_cb(lv_event_t *e)
{
    EOS_LOG_I("Flash light closed");
    eos_chrome_manager_notify_overlay_closed(&_flash_light_overlay);
}

static void _flash_light_overlay_pull_back(void)
{
    eos_flash_light_pull_back();
}

static void _flash_light_overlay_hide(void)
{
    eos_flash_light_hide();
}

static void _flash_light_overlay_on_focus(void)
{
    lv_obj_t *touch_obj = eos_flash_light_get_touch_obj();
    if (touch_obj)
    {
        lv_obj_move_foreground(touch_obj);
    }
}

/**
 * @brief Used to set background opacity, decreases opacity as y-axis increases
 */
static void _swipe_panel_moving_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    eos_swipe_panel_t *sp = ud->sp;

    lv_obj_update_layout(sp->swipe_obj);

    int32_t y = lv_obj_get_y(sp->swipe_obj);

    int32_t max_dist = EOS_DISPLAY_HEIGHT / _OPA_MAX_DIST_DIV;

    int32_t ratio = ((max_dist - y) * _OPA_SCALE) / max_dist;

    ratio = EOS_CLAMP(ratio, 0, _OPA_SCALE);

    lv_opa_t opa = (lv_opa_t)((ratio * _MASK_OPA) / _OPA_SCALE);

    EOS_LOG_I("y=%d, ratio=%d‰, opa=%d", y, ratio, opa);

    lv_obj_set_style_bg_opa(ud->mask, opa, 0);
}

static void _flash_light_clicked_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    _flash_light_delete(ud);
    eos_flash_light_enter();
}

static void _flash_light_update_indicator_theme(_flash_light_card_pager_ctx_t *ctx,
                                                lv_obj_t *current_page,
                                                bool red_page_active)
{
    EOS_CHECK_PTR_RETURN(ctx && ctx->cp);

    for (eos_card_pager_node_t *node = ctx->cp->page_list_head; node; node = node->next)
    {
        if (!node->indicator)
            continue;

        bool is_current = (node->page == current_page);
        if (red_page_active)
        {
            // mode1: current=white, non-current=black
            lv_obj_set_style_bg_color(node->indicator,
                                      is_current ? _INDICATOR_MODE1_ACTIVE_COLOR : _INDICATOR_MODE1_INACTIVE_COLOR,
                                      0);
        }
        else
        {
            // mode2: current=black, non-current=grey
            lv_obj_set_style_bg_color(node->indicator,
                                      is_current ? _INDICATOR_MODE2_ACTIVE_COLOR : _INDICATOR_MODE2_INACTIVE_COLOR,
                                      0);
        }
    }
}

static void _flash_light_card_pager_page_changed_cb(eos_card_pager_t *cp, uint8_t current_page_index, void *user_data)
{
    _flash_light_card_pager_ctx_t *ctx = user_data;
    EOS_CHECK_PTR_RETURN(ctx && cp);

    _flash_light_apply_page_visual_state(ctx, current_page_index);
}

static void _flash_light_card_pager_clicked_cb(lv_event_t *e)
{
    _flash_light_card_pager_ctx_t *ctx = lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ctx && ctx->cp);

    if (ctx->cp->sw)
    {
        lv_coord_t disp = eos_slide_widget_get_displacement(ctx->cp->sw);
        if (abs(disp) > _IMMERSIVE_TAP_MAX_DISPLACEMENT)
            return;
    }

    ctx->immersive_mode = !ctx->immersive_mode;

    eos_activity_set_app_header_visible_animated(ctx->activity, !ctx->immersive_mode, _IMMERSIVE_FADE_DURATION);

    _flash_light_set_indicator_visible_animated(ctx, !ctx->immersive_mode, _IMMERSIVE_FADE_DURATION);

    _flash_light_apply_page_visual_state(ctx, ctx->cp->current_page_index);
}

static void _flash_light_set_indicator_visible_animated(_flash_light_card_pager_ctx_t *ctx,
                                                        bool visible,
                                                        uint32_t duration_ms)
{
    EOS_CHECK_PTR_RETURN(ctx && ctx->cp);

    lv_obj_t *indicator = ctx->cp->indicator_container;
    if (!indicator || !lv_obj_is_valid(indicator))
        return;

    if (duration_ms == 0)
    {
        if (visible)
            lv_obj_remove_flag(indicator, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_anim_delete(indicator, _flash_light_obj_opa_anim_cb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, indicator);
    lv_anim_set_exec_cb(&anim, _flash_light_obj_opa_anim_cb);
    lv_anim_set_duration(&anim, duration_ms);

    if (visible)
    {
        lv_obj_remove_flag(indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(indicator, LV_OPA_TRANSP, 0);
        lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_start(&anim);
    }
    else
    {
        if (lv_obj_has_flag(indicator, LV_OBJ_FLAG_HIDDEN))
            return;

        lv_obj_set_style_opa(indicator, LV_OPA_COVER, 0);
        lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_completed_cb(&anim, _flash_light_indicator_fade_out_ready_cb);
        lv_anim_start(&anim);
    }
}

static void _flash_light_apply_page_visual_state(_flash_light_card_pager_ctx_t *ctx, uint8_t current_page_index)
{
    EOS_CHECK_PTR_RETURN(ctx && ctx->cp);

    lv_obj_t *current_page = eos_card_pager_get_page(ctx->cp, current_page_index);
    bool palette_page_active = (current_page == ctx->palette_page);

    /* 页0 背景始终 = 当前光色（切回即发光色） */
    if (ctx->light_page && lv_obj_is_valid(ctx->light_page))
    {
        lv_obj_set_style_bg_color(ctx->light_page, ctx->custom_color, 0);
    }

    if (!ctx->immersive_mode)
        _flash_light_update_indicator_theme(ctx, current_page, palette_page_active);
}

static lv_obj_t *_flash_light_get_indicator_for_page(eos_card_pager_t *cp, lv_obj_t *page)
{
    EOS_CHECK_PTR_RETURN_VAL(cp && page, NULL);

    for (eos_card_pager_node_t *node = cp->page_list_head; node; node = node->next)
    {
        if (node->page == page)
        {
            return node->indicator;
        }
    }

    return NULL;
}

static bool _flash_light_swipe_back(eos_activity_t *self, lv_dir_t dir)
{
    LV_UNUSED(self);

    /* 左右滑：pager 自己翻页，消费掉（不让框架退出）；
     * 上下滑：交给框架统一退出（framework 层已改为上下滑退出手势）。 */
    return (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT);
}

static void _flash_light_exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    eos_activity_back();
}

void eos_flash_light_show(void)
{
    if (_flash_light_ud)
    {
        EOS_LOG_W("Flash light is already showing");
        return;
    }

    _pressing_user_data_t *ud = eos_malloc(sizeof(_pressing_user_data_t));
    EOS_CHECK_PTR_RETURN(ud);

    lv_obj_t *layer_top = lv_layer_top();

    lv_obj_t *mask = lv_obj_create(layer_top);
    lv_obj_remove_style_all(mask);
    lv_obj_set_size(mask, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(mask, EOS_COLOR_BLACK, 0);

    ud->mask = mask;

    eos_swipe_panel_t *sp = eos_swipe_panel_create(layer_top);
    eos_swipe_panel_set_dir(sp, EOS_SWIPE_DIR_UP);
    eos_swipe_panel_slide_down(sp);
    eos_swipe_panel_hide_handle_bar(sp);
    lv_obj_set_style_bg_opa(sp->swipe_obj, LV_OPA_TRANSP, 0);

    ud->sp = sp;

    eos_slide_widget_add_event_cb_done(sp->sw, _swipe_panel_pull_back_cb, ud);
    eos_slide_widget_add_event_cb_moving(sp->sw, _swipe_panel_moving_cb, ud);
    eos_slide_widget_add_event_cb_closed(sp->sw, _flash_light_closed_cb, NULL);
    int32_t touch_area_height = EOS_DISPLAY_HEIGHT * 0.2;
    lv_obj_set_height(eos_slide_widget_get_touch_obj(sp->sw), touch_area_height);

    lv_obj_t *container = sp->swipe_obj;
    lv_obj_set_height(container, 2 * EOS_DISPLAY_HEIGHT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row1 = lv_obj_create(container);
    lv_obj_remove_style_all(row1);
    lv_obj_set_size(row1, lv_pct(100), touch_area_height);

    lv_obj_t *label = lv_label_create(row1);
    lv_obj_set_height(label, LV_SIZE_CONTENT);

    lv_label_set_text_fmt(label, "%s\n" RI_ARROW_DOWN_WIDE_FILL, eos_lang_get_text(STR_ID_APP_FLASH_LIGHT_DISMISS));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *flash_light = lv_obj_create(container);
    lv_obj_set_style_bg_color(flash_light, EOS_COLOR_WHITE, 0);
    lv_obj_set_size(flash_light, lv_pct(100), EOS_DISPLAY_HEIGHT);
    lv_obj_set_style_border_width(flash_light, 0, 0);
    lv_obj_set_style_radius(flash_light, EOS_DISPLAY_RADIUS, 0);
    lv_obj_add_event_cb(flash_light, _flash_light_clicked_cb, LV_EVENT_CLICKED, ud);

    _flash_light_ud = ud;
    eos_display_set_brightness(EOS_DISPLAY_BRIGHTNESS_MAX, _BRIGHTNESS_DURATION, true);
    eos_chrome_manager_notify_overlay_opened(&_flash_light_overlay);
}

bool eos_flash_light_is_open(void)
{
    if (!_flash_light_ud || !_flash_light_ud->sp || !_flash_light_ud->sp->sw)
        return false;
    return eos_slide_widget_get_state(_flash_light_ud->sp->sw) == EOS_SLIDE_WIDGET_STATE_OPEN;
}

lv_obj_t *eos_flash_light_get_touch_obj(void)
{
    if (!_flash_light_ud || !_flash_light_ud->sp || !_flash_light_ud->sp->sw)
        return NULL;
    return eos_slide_widget_get_touch_obj(_flash_light_ud->sp->sw);
}

void eos_flash_light_pull_back(void)
{
    if (_flash_light_ud && _flash_light_ud->sp)
    {
        eos_swipe_panel_pull_back(_flash_light_ud->sp);
    }
}

void eos_flash_light_hide(void)
{
    if (_flash_light_ud)
    {
        _flash_light_delete(_flash_light_ud);
    }
}

const eos_chrome_overlay_t *eos_flash_light_get_overlay_descriptor(void)
{
    return &_flash_light_overlay;
}

void eos_flash_light_enter(void)
{
    eos_display_set_brightness(EOS_DISPLAY_BRIGHTNESS_MAX, _BRIGHTNESS_DURATION, true);
    eos_activity_t *a = eos_activity_create(&_flash_light_lifecycle);
    if (!a)
        return;

    _flash_light_card_pager_ctx_t *ctx = eos_malloc_zeroed(sizeof(_flash_light_card_pager_ctx_t));
    if (!ctx)
    {
        eos_activity_back();
        return;
    }

    eos_activity_set_type(a, EOS_ACTIVITY_TYPE_APP);
    eos_activity_set_app_header_visible(a, true);
    eos_activity_set_app_header_time_only(a, true);

    lv_obj_t *view = eos_activity_get_view(a);
    if (!view)
    {
        eos_free(ctx);
        eos_activity_back();
        return;
    }

    ctx->activity = a;
    ctx->immersive_mode = false;
    lv_obj_set_user_data(view, ctx);

    lv_obj_remove_style_all(view);
    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(view, EOS_COLOR_WHITE, 0);

    eos_card_pager_t *cp = eos_card_pager_create(view, EOS_CARD_PAGER_DIR_HOR);
    if (cp)
    {
        ctx->cp = cp;

        // 页0：手电筒（全屏 custom_color；点击关闭 — 由 touch_area 统一处理）
        lv_obj_t *page = eos_card_pager_get_page(cp, 0);
        lv_obj_set_style_bg_color(page, EOS_COLOR_WHITE, 0);
        ctx->light_page = page;
        ctx->custom_color = EOS_COLOR_WHITE;

        // 页1：画图式色板（点色块 → 发光色）
        _flash_light_create_palette_page(ctx, cp);

        // card_pager 的全屏 touch_area 在最上层，点击统一在这里收
        // （页0点击关闭 / 页1点色块选色；上下滑退出由 framework 层统一处理）
        lv_obj_t *touch_obj = eos_slide_widget_get_touch_obj(cp->sw);
        if (touch_obj)
        {
            lv_obj_add_event_cb(touch_obj, _flash_light_press_start_cb, LV_EVENT_PRESSED, ctx);
            lv_obj_add_event_cb(touch_obj, _flash_light_touch_cb, LV_EVENT_CLICKED, ctx);
        }

        eos_card_pager_set_page_changed_cb(cp, _flash_light_card_pager_page_changed_cb, ctx);

        lv_obj_t *sw1_touch_obj = eos_slide_widget_get_touch_obj(cp->sw);
        if (sw1_touch_obj)
        {
            lv_obj_add_event_cb(sw1_touch_obj, _flash_light_card_pager_clicked_cb, LV_EVENT_CLICKED, ctx);
        }
    }

    /* exit_btn removed by user request (2026-08): 退出靠左滑/系统返回 */

    eos_activity_enter(a);

    if (ctx->cp)
    {
        _flash_light_apply_page_visual_state(ctx, ctx->cp->current_page_index);
    }
}
