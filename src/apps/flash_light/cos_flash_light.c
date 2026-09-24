/**
 * @file cos_flash_light.c
 * @brief Flashlight
 */

#include "cos_flash_light.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "cos_theme.h"
#include "cos_config.h"
#include "cos_swipe_panel.h"
#define COS_LOG_TAG "FlashLight"
#include "cos_log.h"
#include "cos_event.h"
#include "cos_icon.h"
#include "cos_anim.h"
#include "cos_utils.h"
#include "cos_card_pager.h"
#include "cos_watchface.h"
#include "cos_port.h"
#include "cos_service_config.h"
#include "cos_service_display.h"
#include "cos_app_list.h"
#include "cos_lang.h"
#include "cos_basic_widgets.h"
#include "cos_app_header.h"
#include "cos_mem.h"
#include "cos_chrome_manager.h"

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
#define _INDICATOR_MODE1_ACTIVE_COLOR COS_COLOR_WHITE
#define _INDICATOR_MODE1_INACTIVE_COLOR COS_COLOR_BLACK
#define _INDICATOR_MODE2_ACTIVE_COLOR COS_COLOR_BLACK
#define _INDICATOR_MODE2_INACTIVE_COLOR COS_COLOR_TEXT_GREY
#define _BRIGHTNESS_DURATION 750
typedef struct
{
    cos_swipe_panel_t *sp;
    lv_obj_t *mask;
    lv_obj_t *flash_light;      /* 全屏手电筒对象(选色直接应用,勿用 get_child: swipe_panel 的 handle_bar 才是子对象0) */
    lv_color_t custom_color;    /* 当前光色(默认白,可从 SD 恢复) */
    lv_obj_t *palette_overlay;  /* 色板覆盖层 */
    lv_obj_t *hue_canvas;       /* HSV 色相环画布(ARGB8888,PSRAM) */
    lv_obj_t *hue_indicator;    /* 环上当前色相指示点 */
    lv_obj_t *color_preview;    /* 中心当前色预览圆 */
    lv_obj_t *color_hex_label;  /* 中心 RGB hex 文本 */
    void *hue_buf;              /* 色相环画布缓冲(PSRAM,LV_EVENT_DELETE 时释放) */
    bool hue_dirty;             /* 本次按下是否已选色(RELEASED 时据此保存 SD) */
} _pressing_user_data_t;

typedef struct
{
    cos_activity_t *activity;
    cos_card_pager_t *cp;
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
static const cos_chrome_overlay_t _flash_light_overlay = {
    .pull_back = _flash_light_overlay_pull_back,
    .hide = _flash_light_overlay_hide,
    .on_focus = _flash_light_overlay_on_focus,
};

/* Function Implementations -----------------------------------*/
static void _flash_light_on_destroy(cos_activity_t *a);
static inline void _flash_light_delete(_pressing_user_data_t *ud);
lv_obj_t *cos_flash_light_get_touch_obj(void);
static bool _flash_light_swipe_back(cos_activity_t *self, lv_dir_t dir);
static void _flash_light_exit_cb(lv_event_t *e);
static void _flash_light_card_pager_page_changed_cb(cos_card_pager_t *cp, uint8_t current_page_index, void *user_data);
static void _flash_light_card_pager_clicked_cb(lv_event_t *e);
static lv_obj_t *_flash_light_get_indicator_for_page(cos_card_pager_t *cp, lv_obj_t *page);
static void _flash_light_apply_page_visual_state(_flash_light_card_pager_ctx_t *ctx, uint8_t current_page_index);
static void _flash_light_set_indicator_visible_animated(_flash_light_card_pager_ctx_t *ctx,
                                                        bool visible,
                                                        uint32_t duration_ms);

static void _flash_light_apply_color(_flash_light_card_pager_ctx_t *ctx)
{
    COS_CHECK_PTR_RETURN(ctx && ctx->light_page);
    lv_obj_set_style_bg_color(ctx->light_page, ctx->custom_color, 0);
}

/* "已选"提示显示 800ms 后隐藏（复用 timer，每 tap 零新建） */
static void _flash_light_sel_feedback_timer_cb(lv_timer_t *t)
{
    _flash_light_card_pager_ctx_t *ctx = lv_timer_get_user_data(t);
    COS_CHECK_PTR_RETURN(ctx);
    if (ctx->sel_feedback && lv_obj_is_valid(ctx->sel_feedback))
        lv_obj_add_flag(ctx->sel_feedback, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
    printf("[flash] sel feedback hidden\n"); fflush(stdout);
}

/* 色板几何（创建与触摸反算共用） */
#define _FLASH_CELL_W 26
#define _FLASH_CELL_H 22
#define _FLASH_CELL_GAP 2
#define _FLASH_PALETTE_X0 ((COS_DISPLAY_WIDTH - (_FLASH_PALETTE_HUE_STEPS * (_FLASH_CELL_W + _FLASH_CELL_GAP) - _FLASH_CELL_GAP)) / 2)
#define _FLASH_PALETTE_Y0 40

/*
 * card_pager 的透明全屏 touch_area 盖在所有页面上（最上层），
 * 页面/子对象的 CLICKED 永远收不到（实测：选色无反应、点手电筒不关闭）。
 * 故在 touch_area 上统一处理点击与上下滑退出：
 *   页0（手电筒）→ 点击关闭；
 *   页1（色板）  → 按坐标反算色块行列 → 对应颜色发光。
 *   上下滑（任意页）→ 退出 app（framework 层统一处理，见 cos_activity.c）。
 * 左右滑翻页由 slide_widget 的 PRESSED/MOVING 处理，不受影响。
 */

/* 拖动到一半松开也算拖动（位移超 10px），不算点击 → 不触发页0关闭/选色 */
#define _FLASH_TAP_MAX_MOVE 10

/* PRESSED 时记录触摸起点（CLICKED 时比较位移，过滤拖动误判的点击） */
static void _flash_light_press_start_cb(lv_event_t *e)
{
    _flash_light_card_pager_ctx_t *ctx = lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx);
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    lv_indev_get_point(indev, &ctx->press_start);
    ctx->press_tracked = true;
}

static void _flash_light_touch_cb(lv_event_t *e)
{
    _flash_light_card_pager_ctx_t *ctx = lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx && ctx->cp);

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
        cos_activity_back();
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
        lv_label_set_text(ctx->sel_feedback, "Selected");
        if (ctx->sel_timer)
        {
            lv_timer_reset(ctx->sel_timer);
            lv_timer_resume(ctx->sel_timer);
        }
    }
    printf("[flash] sel feedback: cell=%p border=2 feedback_visible=1\n", (void *)cell); fflush(stdout);
}

/* 色板页：8 色相 × 6 亮度 = 48 色块（画图选色式，点哪是哪） */
static void _flash_light_create_palette_page(_flash_light_card_pager_ctx_t *ctx, cos_card_pager_t *cp)
{
    lv_obj_t *page = cos_card_pager_create_page(cp);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x101418), 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    ctx->palette_page = page;
    ctx->custom_color = COS_COLOR_WHITE;

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
    lv_label_set_text(fb, "Selected");
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

static const cos_activity_lifecycle_t _flash_light_lifecycle = {
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

static void _flash_light_on_destroy(cos_activity_t *a)
{
    lv_obj_t *view = cos_activity_get_view(a);
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

        cos_free(ctx);
    }

    /* 压感 show() 版本的清理（enter 版 _flash_light_ud 恒 NULL，勿传 NULL 调
     * _flash_light_delete —— 会触发 NULL pointer 误报） */
    if (_flash_light_ud)
        _flash_light_delete(_flash_light_ud);
    cos_display_restore(_BRIGHTNESS_DURATION);
}

static inline void _flash_light_delete(_pressing_user_data_t *ud)
{
    COS_CHECK_PTR_RETURN(ud);

    if (ud->sp)
        cos_swipe_panel_delete(ud->sp);

    if (ud->mask && lv_obj_is_valid(ud->mask))
        lv_obj_delete_async(ud->mask);

    /* 同步关闭残留色板(timer 回调内安全),LV_EVENT_DELETE 会释放 hue_buf */
    if (ud->palette_overlay && lv_obj_is_valid(ud->palette_overlay))
        lv_obj_delete(ud->palette_overlay);
    ud->palette_overlay = NULL;
    ud->mask = NULL;
    ud->sp = NULL;

    if (_flash_light_ud == ud)
        _flash_light_ud = NULL;

    cos_free(ud);
    COS_LOG_I("Flash light deleted");
}

static void _swipe_panel_pull_back_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ud);

    int32_t swipe_obj_coord_y = lv_obj_get_y(ud->sp->swipe_obj);
    if (swipe_obj_coord_y >= COS_DISPLAY_HEIGHT)
    {
        _flash_light_delete(ud);
        cos_display_restore(_BRIGHTNESS_DURATION);
    }
}

static void _flash_light_closed_cb(lv_event_t *e)
{
    COS_LOG_I("Flash light closed");
    cos_chrome_manager_notify_overlay_closed(&_flash_light_overlay);
}

static void _flash_light_overlay_pull_back(void)
{
    cos_flash_light_pull_back();
}

static void _flash_light_overlay_hide(void)
{
    cos_flash_light_hide();
}

static void _flash_light_overlay_on_focus(void)
{
    lv_obj_t *touch_obj = cos_flash_light_get_touch_obj();
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
    cos_swipe_panel_t *sp = ud->sp;

    lv_obj_update_layout(sp->swipe_obj);

    int32_t y = lv_obj_get_y(sp->swipe_obj);

    int32_t max_dist = COS_DISPLAY_HEIGHT / _OPA_MAX_DIST_DIV;

    int32_t ratio = ((max_dist - y) * _OPA_SCALE) / max_dist;

    ratio = COS_CLAMP(ratio, 0, _OPA_SCALE);

    lv_opa_t opa = (lv_opa_t)((ratio * _MASK_OPA) / _OPA_SCALE);

    COS_LOG_I("y=%d, ratio=%d‰, opa=%d", y, ratio, opa);

    lv_obj_set_style_bg_opa(ud->mask, opa, 0);
}

/* 点击白色区域后延迟到下一 tick 删除 swipe panel:
 * 不能在 LV_EVENT_CLICKED 事件回调内同步删除事件源对象(flash_light 属于
 * swipe_obj),否则 LVGL 事件发送完成后会访问已释放对象(Use-After-Free)。
 * 延迟删除同时避免 cos_flash_light_enter() 期间 overlay 状态混乱。 */
static void _flash_light_delayed_delete_timer_cb(lv_timer_t *t)
{
    _pressing_user_data_t *ud = lv_timer_get_user_data(t);
    lv_timer_delete(t);
    if (ud)
        _flash_light_delete(ud);
}

/* ═══════════════ 用户需求 2026-08:底部按钮 + 色板 + SD 持久化 ═══════════════ */
#define _FLASH_COLOR_PATH "/flash/color.txt"

/* 读取 SD 中保存的光色,失败返回白色 */
static lv_color_t _flash_light_load_color(void)
{
    lv_color_t c = COS_COLOR_WHITE;
    cos_file_t f = cos_fs_open_read(_FLASH_COLOR_PATH);
    if (!f)
        return c;
    char buf[8] = {0};
    int n = cos_fs_read(f, buf, 7);
    cos_fs_close(f);
    if (n >= 6)
    {
        uint32_t rgb = (uint32_t)strtoul(buf, NULL, 16);
        c = lv_color_hex(rgb & 0xFFFFFFu);
    }
    return c;
}

/* 保存光色到 SD /flash/color.txt;无 SD 卡/写失败则忽略 */
static void _flash_light_save_color(lv_color_t c)
{
    cos_fs_mkdir("/flash"); /* 无 SD 时 mkdir 失败,直接忽略 */
    cos_file_t f = cos_fs_open_write(_FLASH_COLOR_PATH);
    if (!f)
    {
        COS_LOG_W("Save flash color ignored (no SD / write fail)");
        return;
    }
    /* 不依赖 lv_color_to32(仅 32 位色深存在),直接组合 8bit 分量 */
    uint32_t rgb = ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | (uint32_t)c.blue;
    char buf[8];
    snprintf(buf, sizeof(buf), "%06X", (unsigned)rgb);
    cos_fs_write(f, buf, 6);
    cos_fs_close(f);
    COS_LOG_I("Flash color saved to %s: %s", _FLASH_COLOR_PATH, buf);
}

/* ═══════════════ HSV 圆盘色板(用户需求 2026-08 改版:中心白→径向饱和,全精度过渡) ═══════════════ */
#define _FLASH_HUE_CX 120
#define _FLASH_HUE_CY 112
#define _FLASH_HUE_R_OUT 80
#define _FLASH_HUE_SIZE (2 * _FLASH_HUE_R_OUT)
#define _FLASH_HUE_PREVIEW_D 40
#define _FLASH_HUE_PI 3.14159265358979f

/* 全精度 HSV→RGB888(0xRRGGBB,分量 0-255)。
 * 勿用 lv_color_hsv_to_rgb:LV_COLOR_DEPTH=16 时它返回 RGB565,red/blue 仅 5bit、
 * green 仅 6bit,直接塞进 ARGB8888 会形成 32/64/32 级色阶并整体偏暗(旧版条带根源)。 */
static uint32_t _flash_light_hsv_to_rgb888(int h, int s, int v)
{
    float hh = (float)(h % 360) / 60.0f;
    int i = (int)hh;
    float f = hh - (float)i;
    float sf = (float)s / 100.0f;
    float vf = (float)v / 100.0f;
    float p = vf * (1.0f - sf);
    float q = vf * (1.0f - sf * f);
    float t = vf * (1.0f - sf * (1.0f - f));
    float r, g, b;
    switch (i % 6)
    {
    case 0: r = vf; g = t; b = p; break;
    case 1: r = q; g = vf; b = p; break;
    case 2: r = p; g = vf; b = t; break;
    case 3: r = p; g = q; b = vf; break;
    case 4: r = t; g = p; b = vf; break;
    default: r = vf; g = p; b = q; break;
    }
    uint32_t ri = (uint32_t)(r * 255.0f + 0.5f);
    uint32_t gi = (uint32_t)(g * 255.0f + 0.5f);
    uint32_t bi = (uint32_t)(b * 255.0f + 0.5f);
    if (ri > 255) ri = 255;
    if (gi > 255) gi = 255;
    if (bi > 255) bi = 255;
    return (ri << 16) | (gi << 8) | bi;
}

/* 渲染 HSV 圆盘到 ARGB8888 缓冲:圆心=白(S=0,V=100),径向=饱和度,环向=色相。
 * 色盘外像素透明。一次性渲染(~26k 像素),240MHz 下 <50ms,打开色板时执行一次。 */
static void _flash_light_hue_render(void *buf)
{
    uint32_t *px = (uint32_t *)buf;
    int cx = _FLASH_HUE_R_OUT, cy = _FLASH_HUE_R_OUT;
    int r2 = _FLASH_HUE_R_OUT * _FLASH_HUE_R_OUT;
    for (int y = 0; y < _FLASH_HUE_SIZE; y++)
    {
        for (int x = 0; x < _FLASH_HUE_SIZE; x++)
        {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2)
            {
                float ang = atan2f((float)dy, (float)dx); /* -PI..PI */
                if (ang < 0)
                    ang += 2.0f * _FLASH_HUE_PI;
                int hue = (int)(ang * 180.0f / _FLASH_HUE_PI); /* 0..359 */
                int sat = (int)(sqrtf((float)d2) / (float)_FLASH_HUE_R_OUT * 100.0f + 0.5f);
                if (sat > 100)
                    sat = 100;
                px[y * _FLASH_HUE_SIZE + x] = 0xFF000000u | _flash_light_hsv_to_rgb888(hue, sat, 100);
            }
            else
            {
                px[y * _FLASH_HUE_SIZE + x] = 0x00000000u; /* 透明 */
            }
        }
    }
}

/* RGB(565)→ H/S:先归一化到 0-255 再算色相与饱和度,供初始指示点定位 */
static void _flash_light_rgb_to_hs(lv_color_t c, int *hue_out, int *sat_out)
{
    float r = (float)c.red / 31.0f;
    float g = (float)c.green / 63.0f;
    float b = (float)c.blue / 31.0f;
    float maxv = fmaxf(r, fmaxf(g, b));
    float minv = fminf(r, fminf(g, b));
    float delta = maxv - minv;
    float hue = 0.0f;
    if (delta > 0.0001f)
    {
        if (maxv == r)      hue = 60.0f * fmodf((g - b) / delta, 6.0f);
        else if (maxv == g) hue = 60.0f * ((b - r) / delta + 2.0f);
        else                hue = 60.0f * ((r - g) / delta + 4.0f);
        if (hue < 0) hue += 360.0f;
    }
    *hue_out = (int)hue;
    float sat = (maxv > 0.0001f) ? (delta / maxv * 100.0f) : 0.0f;
    if (sat > 100.0f) sat = 100.0f;
    *sat_out = (int)sat;
}

/* 应用色相/饱和度(V=100):改手电筒光色 + 更新预览/hex + 移动盘上指示点 */
static void _flash_light_hue_apply(_pressing_user_data_t *ud, int hue, int sat)
{
    uint32_t rgb888 = _flash_light_hsv_to_rgb888(hue, sat, 100);
    lv_color_t c = lv_color_hex(rgb888);
    ud->custom_color = c;

    lv_obj_t *flash_light = ud->flash_light;
    if (flash_light && lv_obj_is_valid(flash_light))
        lv_obj_set_style_bg_color(flash_light, c, 0);

    if (ud->hue_indicator && lv_obj_is_valid(ud->hue_indicator))
    {
        if (sat < 3)
        {
            /* 白色在圆心:指示点盖住预览圆无意义,隐藏 */
            lv_obj_add_flag(ud->hue_indicator, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_remove_flag(ud->hue_indicator, LV_OBJ_FLAG_HIDDEN);
            float ang = (float)hue * 2.0f * _FLASH_HUE_PI / 360.0f;
            int rr = (int)((float)sat / 100.0f * (float)_FLASH_HUE_R_OUT);
            lv_obj_set_pos(ud->hue_indicator,
                           _FLASH_HUE_CX + (int)((float)rr * cosf(ang)) - 6,
                           _FLASH_HUE_CY + (int)((float)rr * sinf(ang)) - 6);
        }
    }

    if (ud->color_preview && lv_obj_is_valid(ud->color_preview))
        lv_obj_set_style_bg_color(ud->color_preview, c, 0);

    if (ud->color_hex_label && lv_obj_is_valid(ud->color_hex_label))
    {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                 (unsigned)((rgb888 >> 16) & 0xFFu),
                 (unsigned)((rgb888 >> 8) & 0xFFu),
                 (unsigned)(rgb888 & 0xFFu));
        lv_label_set_text(ud->color_hex_label, hex);
        lv_obj_set_style_text_color(ud->color_hex_label,
                                    lv_color_brightness(c) > 140 ? COS_COLOR_BLACK : COS_COLOR_WHITE, 0);
    }
}

/* 圆盘触摸:盘内任意点 = 色相(角度)×饱和度(半径),中心即白色。
 * PRESSED/PRESSING 实时选色,RELEASED 保存 SD */
static void _flash_light_hue_event_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    if (!ud)
        return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    int dx = pt.x - _FLASH_HUE_CX;
    int dy = pt.y - _FLASH_HUE_CY;
    int d2 = dx * dx + dy * dy;
    lv_event_code_t code = lv_event_get_code(e);

    int r_max2 = (_FLASH_HUE_R_OUT + 10) * (_FLASH_HUE_R_OUT + 10);
    if (d2 > r_max2)
    {
        if (code == LV_EVENT_RELEASED)
            ud->hue_dirty = false; /* 松手在色盘外:不保存 */
        return;
    }

    float ang = atan2f((float)dy, (float)dx);
    if (ang < 0)
        ang += 2.0f * _FLASH_HUE_PI;
    int hue = (int)(ang * 180.0f / _FLASH_HUE_PI);
    int sat = (int)(sqrtf((float)d2) / (float)_FLASH_HUE_R_OUT * 100.0f + 0.5f);
    if (sat > 100)
        sat = 100;
    _flash_light_hue_apply(ud, hue, sat);

    if (code == LV_EVENT_RELEASED)
    {
        if (ud->hue_dirty)
            _flash_light_save_color(ud->custom_color);
        ud->hue_dirty = false;
    }
    else
    {
        ud->hue_dirty = true;
    }
}

/* overlay 删除时释放画布缓冲(避免 async 删除后 UAF) */
static void _flash_light_hue_canvas_delete_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    if (!ud)
        return;
    lv_event_stop_bubbling(e);
    if (ud->hue_buf)
    {
        cos_free(ud->hue_buf);
        ud->hue_buf = NULL;
    }
    ud->hue_canvas = NULL;
}

static void _flash_light_palette_overlay_close(_pressing_user_data_t *ud)
{
    if (ud->palette_overlay && lv_obj_is_valid(ud->palette_overlay))
    {
        lv_obj_delete_async(ud->palette_overlay);
    }
    ud->palette_overlay = NULL;
}

static void _flash_light_palette_overlay_close_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    if (!ud)
        return;
    lv_event_stop_bubbling(e);
    _flash_light_palette_overlay_close(ud);
}

/* 色板覆盖层:半透明深色全屏 + HSV 色相环(360° 连续渐变,S=100%,V=100%) */
static void _flash_light_palette_overlay_create(_pressing_user_data_t *ud)
{
    if (ud->palette_overlay)
        return;

    lv_obj_t *ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
    lv_obj_set_style_radius(ov, COS_DISPLAY_RADIUS, 0);
    ud->palette_overlay = ov;

    lv_obj_t *title = lv_label_create(ov);
    lv_label_set_text(title, "Select Color");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* HSV 色相环画布(ARGB8888,PSRAM,约 164×164×4B) */
    uint32_t *buf = (uint32_t *)cos_malloc((size_t)_FLASH_HUE_SIZE * _FLASH_HUE_SIZE * 4);
    if (buf)
    {
        _flash_light_hue_render(buf);
        lv_obj_t *cv = lv_canvas_create(ov);
        lv_canvas_set_buffer(cv, buf, _FLASH_HUE_SIZE, _FLASH_HUE_SIZE, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_remove_flag(cv, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(cv, _FLASH_HUE_CX - _FLASH_HUE_R_OUT, _FLASH_HUE_CY - _FLASH_HUE_R_OUT);
        ud->hue_canvas = cv;
        ud->hue_buf = buf;
    }

    /* 中心当前色预览圆(盖住圆盘白色心,视觉自然) */
    int preview_d = _FLASH_HUE_PREVIEW_D;
    lv_obj_t *pv = lv_obj_create(ov);
    lv_obj_set_size(pv, preview_d, preview_d);
    lv_obj_set_pos(pv, _FLASH_HUE_CX - preview_d / 2, _FLASH_HUE_CY - preview_d / 2);
    lv_obj_remove_flag(pv, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(pv, ud->custom_color, 0);
    lv_obj_set_style_radius(pv, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(pv, 1, 0);
    lv_obj_set_style_border_color(pv, lv_color_white(), 0);
    lv_obj_set_style_border_opa(pv, LV_OPA_40, 0);
    ud->color_preview = pv;

    /* #RRGGBB 代码:放在预览圆正下方,避免小圆内放不下 7 字符 */
    lv_obj_t *hex = lv_label_create(ov);
    lv_label_set_text(hex, "#FFFFFF");
    lv_obj_align(hex, LV_ALIGN_TOP_MID, 0, 134);
    ud->color_hex_label = hex;

    /* 盘上当前色指示点(白底黑圈,白色时隐藏) */
    lv_obj_t *ind = lv_obj_create(ov);
    lv_obj_set_size(ind, 12, 12);
    lv_obj_remove_flag(ind, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ind, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ind, lv_color_white(), 0);
    lv_obj_set_style_border_width(ind, 2, 0);
    lv_obj_set_style_border_color(ind, lv_color_black(), 0);
    lv_obj_set_style_border_opa(ind, LV_OPA_60, 0);
    ud->hue_indicator = ind;

    /* 触摸:整个覆盖层接收按/拖/放,环带内实时选色 */
    lv_obj_add_event_cb(ov, _flash_light_hue_event_cb, LV_EVENT_PRESSED, ud);
    lv_obj_add_event_cb(ov, _flash_light_hue_event_cb, LV_EVENT_PRESSING, ud);
    lv_obj_add_event_cb(ov, _flash_light_hue_event_cb, LV_EVENT_RELEASED, ud);
    lv_obj_add_event_cb(ov, _flash_light_hue_canvas_delete_cb, LV_EVENT_DELETE, ud);

    /* 底部 Close 按钮 */
    lv_obj_t *close_btn = lv_button_create(ov);
    lv_obj_set_size(close_btn, 72, 34);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(close_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_color(close_lbl, COS_COLOR_BLACK, 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, _flash_light_palette_overlay_close_cb, LV_EVENT_CLICKED, ud);

    /* 初始指示点/预览/hex 同步当前光色(按 H/S 定位) */
    int init_hue = 0, init_sat = 0;
    _flash_light_rgb_to_hs(ud->custom_color, &init_hue, &init_sat);
    _flash_light_hue_apply(ud, init_hue, init_sat);
}

/* 底部"色板"按钮 */
static void _flash_light_btn_palette_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    if (!ud)
        return;
    lv_event_stop_bubbling(e);
    _flash_light_palette_overlay_create(ud);
}

/* 底部"退出"按钮:延迟删除(不能在事件回调内同步删除源对象) */
static void _flash_light_btn_exit_cb(lv_event_t *e)
{
    _pressing_user_data_t *ud = lv_event_get_user_data(e);
    if (!ud)
        return;
    lv_event_stop_bubbling(e);
    if (_flash_light_ud == ud)
        _flash_light_ud = NULL;
    lv_timer_t *t = lv_timer_create(_flash_light_delayed_delete_timer_cb, 0, ud);
    if (t)
        lv_timer_set_repeat_count(t, 1);
    cos_display_restore(_BRIGHTNESS_DURATION);
}

/* 底部工具按钮(半透明黑底白字,任意光色下可读) */
static lv_obj_t *_flash_light_create_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 64, 36); /* 64 宽:容纳 "Palette" 7 字母(56 宽会溢出) */
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

static void _flash_light_update_indicator_theme(_flash_light_card_pager_ctx_t *ctx,
                                                lv_obj_t *current_page,
                                                bool red_page_active)
{
    COS_CHECK_PTR_RETURN(ctx && ctx->cp);

    for (cos_card_pager_node_t *node = ctx->cp->page_list_head; node; node = node->next)
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

static void _flash_light_card_pager_page_changed_cb(cos_card_pager_t *cp, uint8_t current_page_index, void *user_data)
{
    _flash_light_card_pager_ctx_t *ctx = user_data;
    COS_CHECK_PTR_RETURN(ctx && cp);

    _flash_light_apply_page_visual_state(ctx, current_page_index);
}

static void _flash_light_card_pager_clicked_cb(lv_event_t *e)
{
    _flash_light_card_pager_ctx_t *ctx = lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx && ctx->cp);

    if (ctx->cp->sw)
    {
        lv_coord_t disp = cos_slide_widget_get_displacement(ctx->cp->sw);
        if (abs(disp) > _IMMERSIVE_TAP_MAX_DISPLACEMENT)
            return;
    }

    ctx->immersive_mode = !ctx->immersive_mode;

    cos_activity_set_app_header_visible_animated(ctx->activity, !ctx->immersive_mode, _IMMERSIVE_FADE_DURATION);

    _flash_light_set_indicator_visible_animated(ctx, !ctx->immersive_mode, _IMMERSIVE_FADE_DURATION);

    _flash_light_apply_page_visual_state(ctx, ctx->cp->current_page_index);
}

static void _flash_light_set_indicator_visible_animated(_flash_light_card_pager_ctx_t *ctx,
                                                        bool visible,
                                                        uint32_t duration_ms)
{
    COS_CHECK_PTR_RETURN(ctx && ctx->cp);

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
    COS_CHECK_PTR_RETURN(ctx && ctx->cp);

    lv_obj_t *current_page = cos_card_pager_get_page(ctx->cp, current_page_index);
    bool palette_page_active = (current_page == ctx->palette_page);

    /* 页0 背景始终 = 当前光色（切回即发光色） */
    if (ctx->light_page && lv_obj_is_valid(ctx->light_page))
    {
        lv_obj_set_style_bg_color(ctx->light_page, ctx->custom_color, 0);
    }

    if (!ctx->immersive_mode)
        _flash_light_update_indicator_theme(ctx, current_page, palette_page_active);
}

/* Unused: kept for future use when pager indicator and swipe-back integration are needed */
#define COS_FLASH_LIGHT_UNUSED_FUNCTIONS
#if 0
static lv_obj_t *_flash_light_get_indicator_for_page(cos_card_pager_t *cp, lv_obj_t *page)
{
    COS_CHECK_PTR_RETURN_VAL(cp && page, NULL);

    for (cos_card_pager_node_t *node = cp->page_list_head; node; node = node->next)
    {
        if (node->page == page)
        {
            return node->indicator;
        }
    }

    return NULL;
}

static void _flash_light_exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    cos_activity_back();
}
#endif /* COS_FLASH_LIGHT_UNUSED_FUNCTIONS */

static bool _flash_light_swipe_back(cos_activity_t *self, lv_dir_t dir)
{
    LV_UNUSED(self);

    /* 左右滑：pager 自己翻页，消费掉（不让框架退出）；
     * 上下滑：交给框架统一退出（framework 层已改为上下滑退出手势）。 */
    return (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT);
}

void cos_flash_light_show(void)
{
    if (_flash_light_ud)
    {
        COS_LOG_W("Flash light is already showing");
        return;
    }

    _pressing_user_data_t *ud = cos_malloc(sizeof(_pressing_user_data_t));
    COS_CHECK_PTR_RETURN(ud);

    lv_obj_t *layer_top = lv_layer_top();

    lv_obj_t *mask = lv_obj_create(layer_top);
    lv_obj_remove_style_all(mask);
    lv_obj_set_size(mask, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(mask, COS_COLOR_BLACK, 0);

    ud->mask = mask;

    cos_swipe_panel_t *sp = cos_swipe_panel_create(layer_top);
    cos_swipe_panel_set_dir(sp, COS_SWIPE_DIR_UP);
    cos_swipe_panel_slide_down(sp);
    cos_swipe_panel_hide_handle_bar(sp);
    lv_obj_set_style_bg_opa(sp->swipe_obj, LV_OPA_TRANSP, 0);

    ud->sp = sp;
    ud->palette_overlay = NULL; /* cos_malloc 未清零,需手动初始化 */
    ud->hue_canvas = NULL;
    ud->hue_indicator = NULL;
    ud->color_preview = NULL;
    ud->color_hex_label = NULL;
    ud->hue_buf = NULL;
    ud->hue_dirty = false;

    cos_slide_widget_add_event_cb_done(sp->sw, _swipe_panel_pull_back_cb, ud);
    cos_slide_widget_add_event_cb_moving(sp->sw, _swipe_panel_moving_cb, ud);
    cos_slide_widget_add_event_cb_closed(sp->sw, _flash_light_closed_cb, NULL);
    int32_t touch_area_height = COS_DISPLAY_HEIGHT * 0.2;
    lv_obj_set_height(cos_slide_widget_get_touch_obj(sp->sw), touch_area_height);

    lv_obj_t *container = sp->swipe_obj;
    lv_obj_set_height(container, 2 * COS_DISPLAY_HEIGHT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    /* flash_light 必须是第一个子对象:swipe panel 完全展开后 container 顶部
     * 对齐屏幕顶部,flash_light(高=COS_DISPLAY_HEIGHT)恰好覆盖 0-H 全屏。
     * 下滑关闭手势由 cos_slide_widget_get_touch_obj 独立提供 */
    ud->custom_color = _flash_light_load_color(); /* 恢复上次保存的光色(无 SD 则白色) */
    lv_obj_t *flash_light = lv_obj_create(container);
    ud->flash_light = flash_light; /* 保存引用:选色时直接应用(见 _flash_light_hue_apply) */
    lv_obj_set_style_bg_color(flash_light, ud->custom_color, 0);
    lv_obj_set_size(flash_light, lv_pct(100), COS_DISPLAY_HEIGHT);
    lv_obj_set_style_border_width(flash_light, 0, 0);
    lv_obj_set_style_radius(flash_light, COS_DISPLAY_RADIUS, 0);

    /* 底部两个按钮(置于下滑手势区上方,与 touch_obj 不重叠):
     *   Exit -> 关闭手电筒; Palette -> 打开色板选择 Flash 颜色 */
    lv_obj_t *exit_btn = _flash_light_create_bottom_btn(flash_light, "Exit");
    lv_obj_align(exit_btn, LV_ALIGN_BOTTOM_MID, -40, -54);
    lv_obj_add_event_cb(exit_btn, _flash_light_btn_exit_cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *palette_btn = _flash_light_create_bottom_btn(flash_light, "Palette");
    lv_obj_align(palette_btn, LV_ALIGN_BOTTOM_MID, 40, -54);
    lv_obj_add_event_cb(palette_btn, _flash_light_btn_palette_cb, LV_EVENT_CLICKED, ud);

    _flash_light_ud = ud;
    cos_display_set_brightness(COS_DISPLAY_BRIGHTNESS_MAX, _BRIGHTNESS_DURATION, true);
    cos_chrome_manager_notify_overlay_opened(&_flash_light_overlay);
}

bool cos_flash_light_is_open(void)
{
    if (!_flash_light_ud || !_flash_light_ud->sp || !_flash_light_ud->sp->sw)
        return false;
    return cos_slide_widget_get_state(_flash_light_ud->sp->sw) == COS_SLIDE_WIDGET_STATE_OPEN;
}

lv_obj_t *cos_flash_light_get_touch_obj(void)
{
    if (!_flash_light_ud || !_flash_light_ud->sp || !_flash_light_ud->sp->sw)
        return NULL;
    return cos_slide_widget_get_touch_obj(_flash_light_ud->sp->sw);
}

void cos_flash_light_pull_back(void)
{
    if (_flash_light_ud && _flash_light_ud->sp)
    {
        cos_swipe_panel_pull_back(_flash_light_ud->sp);
    }
}

void cos_flash_light_hide(void)
{
    if (_flash_light_ud)
    {
        _flash_light_delete(_flash_light_ud);
    }
}

const cos_chrome_overlay_t *cos_flash_light_get_overlay_descriptor(void)
{
    return &_flash_light_overlay;
}

void cos_flash_light_enter(void)
{
    cos_display_set_brightness(COS_DISPLAY_BRIGHTNESS_MAX, _BRIGHTNESS_DURATION, true);
    cos_activity_t *a = cos_activity_create(&_flash_light_lifecycle);
    if (!a)
        return;

    _flash_light_card_pager_ctx_t *ctx = cos_malloc_zeroed(sizeof(_flash_light_card_pager_ctx_t));
    if (!ctx)
    {
        cos_activity_back();
        return;
    }

    cos_activity_set_type(a, COS_ACTIVITY_TYPE_APP);
    cos_activity_set_app_header_visible(a, true);
    /* 恢复 header 时钟(Settings 系列页面将其隐藏) */
    cos_app_header_set_clock_visible(true);
    /* 必须设置 title:header 的 _play_title_changed_anim 会以 %s 打印 title,
     * NULL → vsnprintf → strlen(NULL) → LoadProhibited(日志已证实) */
    cos_activity_set_title(a, "Flash Light");
    cos_activity_set_app_header_time_only(a, true);

    lv_obj_t *view = cos_activity_get_view(a);
    if (!view)
    {
        cos_free(ctx);
        cos_activity_back();
        return;
    }

    ctx->activity = a;
    ctx->immersive_mode = false;
    lv_obj_set_user_data(view, ctx);

    lv_obj_remove_style_all(view);
    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(view, COS_COLOR_WHITE, 0);

    cos_card_pager_t *cp = cos_card_pager_create(view, COS_CARD_PAGER_DIR_HOR);
    if (cp)
    {
        ctx->cp = cp;

        // 页0：手电筒（全屏 custom_color；点击关闭 — 由 touch_area 统一处理）
        lv_obj_t *page = cos_card_pager_get_page(cp, 0);
        lv_obj_set_style_bg_color(page, COS_COLOR_WHITE, 0);
        ctx->light_page = page;
        ctx->custom_color = COS_COLOR_WHITE;

        // 页1：画图式色板（点色块 → 发光色）
        _flash_light_create_palette_page(ctx, cp);

        // card_pager 的全屏 touch_area 在最上层，点击统一在这里收
        // （页0点击关闭 / 页1点色块选色；上下滑退出由 framework 层统一处理）
        lv_obj_t *touch_obj = cos_slide_widget_get_touch_obj(cp->sw);
        if (touch_obj)
        {
            lv_obj_add_event_cb(touch_obj, _flash_light_press_start_cb, LV_EVENT_PRESSED, ctx);
            lv_obj_add_event_cb(touch_obj, _flash_light_touch_cb, LV_EVENT_CLICKED, ctx);
        }

        cos_card_pager_set_page_changed_cb(cp, _flash_light_card_pager_page_changed_cb, ctx);

        lv_obj_t *sw1_touch_obj = cos_slide_widget_get_touch_obj(cp->sw);
        if (sw1_touch_obj)
        {
            lv_obj_add_event_cb(sw1_touch_obj, _flash_light_card_pager_clicked_cb, LV_EVENT_CLICKED, ctx);
        }
    }

    /* exit_btn removed by user request (2026-08): 退出靠左滑/系统返回 */

    cos_activity_enter(a);

    if (ctx->cp)
    {
        _flash_light_apply_page_visual_state(ctx, ctx->cp->current_page_index);
    }
}
