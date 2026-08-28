/**
 * @file eos_power_off_page.c
 * @brief Power-off confirmation page (overlay)
 */

#include "eos_power_off_page.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#define EOS_LOG_TAG "PowerOff"
#include "eos_log.h"
#include "lvgl.h"
#include "eos_icon.h"
#include "eos_theme.h"
#include "eos_font.h"
#include "eos_mem.h"
#include "eos_overlay_layer.h"
#include "eos_service_pm.h"
#include "eos_service_haptic.h"

/* 关机图标(由 resources/images/icon/eos_icon_poweroff.c 提供的 ARGB8888 静态位图) */
extern const lv_image_dsc_t eos_icon_poweroff;

/* Macros and Definitions -------------------------------------*/
#define POWER_OFF_PAGE_MAGIC 0x504F4646U /* "POFF" */

/* Variables --------------------------------------------------*/

typedef struct
{
    uint32_t magic;
    lv_obj_t *root;
} _power_off_page_ctx_t;

static _power_off_page_ctx_t *_ctx = NULL;

/* Function Implementations -----------------------------------*/

static void _on_power_icon_tap(lv_event_t *e)
{
    lv_event_stop_bubbling(e); /* 防止冒泡到根容器触发空白点击关闭 */
    EOS_LOG_I("Power icon tapped -> request deep sleep");
    eos_haptic_buzz();
    eos_power_off_page_hide();
    /* duration=0: no auto-wake, requires 5 taps on screen to wake up */
    eos_pm_deep_sleep_request(0);
}

static void _on_cancel_tap(lv_event_t *e)
{
    lv_event_stop_bubbling(e); /* 防止冒泡到根容器触发空白点击关闭 */
    EOS_LOG_I("Power-off cancelled");
    eos_power_off_page_hide();
}

static void _on_blank_tap(lv_event_t *e)
{
    EOS_LOG_I("Power-off dismissed by blank tap");
    eos_power_off_page_hide();
}

static void _create_ui(_power_off_page_ctx_t *ctx, lv_obj_t *parent)
{
    /* Root container filling parent — absolute top layer */
    ctx->root = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->root);
    lv_obj_set_size(ctx->root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ctx->root, 0, 0);
    lv_obj_set_style_border_width(ctx->root, 0, 0);
    lv_obj_remove_flag(ctx->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ctx->root, LV_SCROLLBAR_MODE_OFF);
    /* 点击空白区域关闭页面(防呆,避免误入后无法退出) */
    lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ctx->root, _on_blank_tap, LV_EVENT_CLICKED, NULL);

    /* Title */
    lv_obj_t *title = lv_label_create(ctx->root);
    lv_label_set_text(title, "Power Off");
    lv_obj_set_style_text_color(title, EOS_COLOR_WHITE, 0);
    eos_label_set_font_size(title, EOS_FONT_SIZE_LARGE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* Power icon (tap target) — 使用 eos_icon_poweroff.c 位图 */
    lv_obj_t *icon = lv_image_create(ctx->root);
    lv_image_set_src(icon, &eos_icon_poweroff);
    lv_obj_set_style_image_recolor(icon, EOS_COLOR_RED, 0);
    lv_obj_center(icon);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(icon, _on_power_icon_tap, LV_EVENT_CLICKED, NULL);

    /* Hint text */
    lv_obj_t *hint = lv_label_create(ctx->root);
    lv_label_set_text(hint, "Tap the icon to power off");
    lv_obj_set_style_text_color(hint, EOS_COLOR_TEXT_GREY, 0);
    eos_label_set_font_size(hint, EOS_FONT_SIZE_SMALL);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -70);

    /* Cancel button */
    lv_obj_t *cancel = lv_label_create(ctx->root);
    lv_label_set_text(cancel, "Cancel");
    lv_obj_set_style_text_color(cancel, EOS_COLOR_TEXT_GREY, 0);
    eos_label_set_font_size(cancel, EOS_FONT_SIZE_MEDIUM);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, _on_cancel_tap, LV_EVENT_CLICKED, NULL);
}

static void _destroy_ui(_power_off_page_ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->root && lv_obj_is_valid(ctx->root))
    {
        lv_obj_delete(ctx->root);
        ctx->root = NULL;
    }

    eos_free(ctx);
}

/* ---- Public API ---- */

void eos_power_off_page_show(void)
{
    if (_ctx)
    {
        EOS_LOG_W("Power-off page already showing");
        return;
    }

    _ctx = (_power_off_page_ctx_t *)eos_malloc_zeroed(sizeof(_power_off_page_ctx_t));
    if (!_ctx)
    {
        EOS_LOG_E("Failed to allocate power-off page context");
        return;
    }

    _ctx->magic = POWER_OFF_PAGE_MAGIC;

    /* Build UI on overlay layer — naturally above header_layer on lv_layer_top */
    _create_ui(_ctx, eos_overlay_get_overlay_layer());

    if (_ctx->root)
    {
        lv_obj_move_foreground(_ctx->root);
    }

    EOS_LOG_I("Power-off page shown");
}

void eos_power_off_page_hide(void)
{
    if (!_ctx)
    {
        return;
    }

    _destroy_ui(_ctx);
    _ctx = NULL;

    EOS_LOG_I("Power-off page hidden");
}

bool eos_power_off_page_is_visible(void)
{
    return _ctx != NULL && _ctx->root != NULL && lv_obj_is_valid(_ctx->root);
}
