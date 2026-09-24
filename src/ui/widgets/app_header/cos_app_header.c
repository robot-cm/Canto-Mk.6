/**
 * @file cos_app_header.c
 * @brief Application top navigation header
 */

#include "cos_app_header.h"
#include "cos_config.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "AppHeader"
#include "cos_log.h"
#include "cos_core.h"
#include "cos_port.h"
#include "cos_lang.h"
#include "cos_image.h"
#include "cos_theme.h"
#include "cos_font.h"
#include "cos_basic_widgets.h"
#include "cos_mem.h"
#include "cos_activity.h"
#include "cos_service_time.h"
#include "cos_event.h"
#include "cos_overlay_layer.h"
#include "cos_service_cache.h"

/* Macros and Definitions -------------------------------------*/
#define _HEADER_HEIGHT 120
#define _HEADER_CLOCK_UPDATE_PERIOD_MINUTES 1 /**< Clock label text update interval in minutes */

#define _HEADER_MARGIN_RIGHT 30

#define _HEADER_TITLE_WIDTH 240

#define _TITLE_LABEL_Y_OFFSET 20
#define _TITLE_LABEL_X_OFFSET -_HEADER_MARGIN_RIGHT
#define _ANIM_DURATION COS_VIEW_SWITCH_DURATION

#define _BACK_BTN_MARGIN_LEFT 20

#define _ANIM_TITLE_MOVE_DISTANCE 50
#define _ANIM_BACK_BTN_MOVE_DISTANCE _ANIM_TITLE_MOVE_DISTANCE

/**
 * @brief Application top header structure definition
 */
typedef struct
{
    lv_obj_t *container;
    lv_obj_t *clock_label;
    lv_obj_t *title_label;
    lv_obj_t *back_btn;
    lv_timer_t *clock_timer;
    lv_obj_t *original_parent; // Original parent object for restoration
    lv_obj_t *old_fading_title; // Old title label pending cleanup
    lv_obj_t *old_fading_back_btn; // Old back button pending cleanup
    bool is_anim_entering; // Animation direction
    bool attached_to_view; // Whether attached to View
    lv_image_dsc_t *grad_bg_img; // Pre-rendered gradient background (ARGB8888 full-size)
} cos_app_header_t;

/* Variables --------------------------------------------------*/
static cos_app_header_t *app_header = NULL;
static bool _back_btn_visible = true; /**< Master switch for the back button
    (apps use swipe-back, so the header back button is hidden while an app runs) */
static bool _clock_visible = true; /**< Master switch for the header clock label
    (Settings pages hide it because the status bar already shows the time) */
/* Function Implementations -----------------------------------*/
static void _clock_update_cb(lv_timer_t *timer);

static void _app_header_set_opa_anim_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (!obj || !lv_obj_is_valid(obj))
        return;

    lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
}

static void _app_header_set_translate_y_anim_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (!obj || !lv_obj_is_valid(obj))
        return;

    lv_obj_set_style_translate_y(obj, value, 0);
}

static void _app_header_fade_out_ready_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (!obj || !lv_obj_is_valid(obj))
        return;

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
}

static void _app_header_apply_activity_mode(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN(app_header);

    bool time_only = false;
    lv_color_t clock_color = COS_COLOR_WHITE;
    if (activity)
    {
        time_only = cos_activity_is_app_header_time_only(activity);
        if (time_only)
        {
            clock_color = cos_activity_get_app_header_time_only_text_color(activity);
        }
    }

    if (app_header->container && lv_obj_is_valid(app_header->container))
    {
        lv_opa_t bg_opa = time_only ? LV_OPA_TRANSP : LV_OPA_COVER;
        lv_obj_set_style_bg_image_opa(app_header->container, bg_opa, 0);
    }

    if (app_header->clock_label && lv_obj_is_valid(app_header->clock_label))
    {
        lv_obj_set_style_text_color(app_header->clock_label, clock_color, 0);
        if (_clock_visible)
            lv_obj_remove_flag(app_header->clock_label, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(app_header->clock_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (app_header->title_label && lv_obj_is_valid(app_header->title_label))
    {
        if (time_only)
            lv_obj_add_flag(app_header->title_label, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(app_header->title_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (app_header->back_btn && lv_obj_is_valid(app_header->back_btn))
    {
        if (time_only || !_back_btn_visible)
            lv_obj_add_flag(app_header->back_btn, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(app_header->back_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

void cos_app_header_set_back_btn_visible(bool visible)
{
    _back_btn_visible = visible;
    if (app_header && app_header->back_btn && lv_obj_is_valid(app_header->back_btn))
    {
        if (visible)
            lv_obj_remove_flag(app_header->back_btn, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(app_header->back_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

void cos_app_header_set_clock_visible(bool visible)
{
    _clock_visible = visible;
    if (app_header && app_header->clock_label && lv_obj_is_valid(app_header->clock_label))
    {
        if (visible)
            lv_obj_remove_flag(app_header->clock_label, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(app_header->clock_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool _app_header_can_reparent(lv_obj_t *new_parent)
{
    if (!app_header)
    {
        return false;
    }

    if (!app_header->container || !lv_obj_is_valid(app_header->container))
    {
        app_header->attached_to_view = false;
        COS_LOG_E("AppHeader container is invalid");
        return false;
    }

    if (!new_parent || !lv_obj_is_valid(new_parent))
    {
        COS_LOG_E("AppHeader target parent is invalid");
        return false;
    }

    return true;
}

static void _set_title_style(lv_obj_t *label)
{
    lv_obj_add_style(label, cos_theme_get_label_style(), 0);
    lv_obj_set_width(label, _HEADER_TITLE_WIDTH);
    lv_label_set_text(label, "");
    cos_label_set_font_size(label, COS_FONT_SIZE_LARGE);

    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(label, COS_THEME_PRIMARY_COLOR, 0);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, _TITLE_LABEL_X_OFFSET, _TITLE_LABEL_Y_OFFSET);
}

static void _set_back_btn_style(lv_obj_t *btn)
{
    lv_obj_set_size(btn, 64, 64);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, COS_THEME_SECONDARY_COLOR, 0);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, _BACK_BTN_MARGIN_LEFT, 0);
}

static void _ah_set_translate_x_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void _ah_set_opa_layered_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa_layered((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void _play_title_changed_anim(cos_activity_t *from,
                              cos_activity_t *to,
                              bool need_anim,
                              bool reverse_anim,
                              lv_anim_timeline_t *at)
{
    COS_CHECK_PTR_RETURN(app_header);
    if (!(lv_obj_is_valid(app_header->title_label) && lv_obj_has_class(app_header->title_label, &lv_label_class)))
        return;

    bool from_time_only = from ? cos_activity_is_app_header_time_only(from) : false;
    bool to_time_only = to ? cos_activity_is_app_header_time_only(to) : false;

    if (from_time_only || to_time_only)
    {
        need_anim = false;
    }

    if (!need_anim || !at)
    {
        const char *new_title = cos_activity_get_title(to);
        COS_LOG_D("New title: %s", new_title ? new_title : "(null)");
        lv_label_set_text(app_header->title_label, new_title ? new_title : "");

        lv_color_t color = cos_activity_get_title_color(to);
        lv_obj_set_style_text_color(app_header->title_label, color, 0);
        _app_header_apply_activity_mode(to);
        return;
    }

    if (app_header->old_fading_title && lv_obj_is_valid(app_header->old_fading_title))
    {
        lv_obj_delete(app_header->old_fading_title);
    }
    if (app_header->old_fading_back_btn && lv_obj_is_valid(app_header->old_fading_back_btn))
    {
        lv_obj_delete(app_header->old_fading_back_btn);
    }
    app_header->old_fading_title = NULL;
    app_header->old_fading_back_btn = NULL;

    if (reverse_anim)
    {
        app_header->is_anim_entering = false;
    }
    else
    {
        app_header->is_anim_entering = true;
    }

    lv_obj_t *l = app_header->title_label;
    lv_obj_t *back_btn = app_header->back_btn;
    lv_obj_t *parent = lv_obj_get_parent(l);

    int32_t title_start_x = 0;
    int32_t title_end_x;
    int32_t back_btn_start_x = 0;
    int32_t back_btn_end_x;

    if (app_header->is_anim_entering)
    {
        title_end_x = title_start_x - _ANIM_TITLE_MOVE_DISTANCE;
        back_btn_end_x = back_btn_start_x - _ANIM_BACK_BTN_MOVE_DISTANCE;
    }
    else
    {
        title_end_x = title_start_x + _ANIM_TITLE_MOVE_DISTANCE;
        back_btn_end_x = back_btn_start_x + _ANIM_BACK_BTN_MOVE_DISTANCE;
    }

    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_values(&a, title_start_x, title_end_x);
    lv_anim_set_exec_cb(&a, _ah_set_translate_x_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION);
    lv_anim_timeline_add(at, 0, &a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_exec_cb(&a, _ah_set_opa_layered_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION + 1);
    lv_anim_timeline_add(at, 0, &a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, back_btn);
    lv_anim_set_values(&a, back_btn_start_x, back_btn_end_x);
    lv_anim_set_exec_cb(&a, _ah_set_translate_x_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION);
    lv_anim_timeline_add(at, 0, &a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, back_btn);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_exec_cb(&a, _ah_set_opa_layered_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION + 1);
    lv_anim_timeline_add(at, 0, &a);

    app_header->old_fading_title = l;
    app_header->old_fading_back_btn = back_btn;

    lv_obj_t *new_l = lv_label_create(parent);
    _set_title_style(new_l);

    const char *new_title = cos_activity_get_title(to);
    COS_LOG_D("New title: %s", new_title ? new_title : "(null)");
    lv_label_set_text(new_l, new_title ? new_title : "");

    lv_color_t color = cos_activity_get_title_color(to);
    lv_obj_set_style_text_color(new_l, color, 0);

    lv_obj_t *new_back_btn = cos_back_btn_create(parent, false);
    _set_back_btn_style(new_back_btn);

    int32_t new_title_start_x, new_title_end_x = 0;
    int32_t new_back_btn_start_x, new_back_btn_end_x = 0;

    if (app_header->is_anim_entering)
    {
        new_title_start_x = new_title_end_x + _ANIM_TITLE_MOVE_DISTANCE;
        new_back_btn_start_x = new_back_btn_end_x + _ANIM_BACK_BTN_MOVE_DISTANCE;
    }
    else
    {
        new_title_start_x = new_title_end_x - _ANIM_TITLE_MOVE_DISTANCE;
        new_back_btn_start_x = new_back_btn_end_x - _ANIM_BACK_BTN_MOVE_DISTANCE;
    }

    lv_obj_set_style_translate_x(new_l, new_title_start_x, 0);
    lv_obj_set_style_opa_layered(new_l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_translate_x(new_back_btn, new_back_btn_start_x, 0);
    lv_obj_set_style_opa_layered(new_back_btn, LV_OPA_TRANSP, 0);

    lv_anim_init(&a);
    lv_anim_set_var(&a, new_l);
    lv_anim_set_values(&a, new_title_start_x, new_title_end_x);
    lv_anim_set_exec_cb(&a, _ah_set_translate_x_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION);
    lv_anim_timeline_add(at, 0, &a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, new_l);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a, _ah_set_opa_layered_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION);
    lv_anim_timeline_add(at, 0, &a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, new_back_btn);
    lv_anim_set_values(&a, new_back_btn_start_x, new_back_btn_end_x);
    lv_anim_set_exec_cb(&a, _ah_set_translate_x_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION);
    lv_anim_timeline_add(at, 0, &a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, new_back_btn);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a, _ah_set_opa_layered_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&a, _ANIM_DURATION);
    lv_anim_timeline_add(at, 0, &a);

    app_header->title_label = new_l;
    app_header->back_btn = new_back_btn;
}

/**
 * @brief Update LVGL string to display current time
 */
static inline void _app_header_update_clock_label(lv_obj_t *label)
{
    cos_datetime_t dt = cos_time_get();
    uint32_t next_ms = (uint32_t)((_HEADER_CLOCK_UPDATE_PERIOD_MINUTES * 60) - dt.sec) * 1000;
    lv_timer_set_period(app_header->clock_timer, next_ms);
    lv_label_set_text_fmt(label, "%02d:%02d", dt.hour, dt.min);
}

/**
 * @brief Time refresh callback, triggered by LVGL timer
 */
static void _clock_update_cb(lv_timer_t *timer)
{
    lv_obj_t *label = lv_timer_get_user_data(timer);
    COS_CHECK_PTR_RETURN(app_header && label);
    if (!(app_header->container && lv_obj_is_valid(app_header->container)))
    {
        return;
    }
    if (lv_obj_has_flag(app_header->container, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }
    // Update display text
    _app_header_update_clock_label(label);
}

void cos_app_header_hide(void)
{
    COS_CHECK_PTR_RETURN(app_header);
    COS_LOG_D("Hide app header");
    // If attached to a View, restore the parent object first
    if (app_header->attached_to_view)
    {
        lv_obj_t *restore_parent = app_header->original_parent;
        if (!restore_parent || !lv_obj_is_valid(restore_parent))
        {
            restore_parent = cos_overlay_get_header_layer();
            app_header->original_parent = restore_parent;
        }

        if (_app_header_can_reparent(restore_parent))
        {
            lv_obj_set_parent(app_header->container, restore_parent);
        }
        app_header->attached_to_view = false;
    }

    if (app_header->container && lv_obj_is_valid(app_header->container))
    {
        lv_obj_add_flag(app_header->container, LV_OBJ_FLAG_HIDDEN);
    }
}

void cos_app_header_show(cos_activity_t *a)
{
    COS_CHECK_PTR_RETURN(app_header);

    // Check if it is a Watchface Activity; if so, hide AppHeader
    cos_activity_t *target_activity = a;
    if (!target_activity)
        target_activity = cos_activity_get_current();

    if (target_activity && target_activity == cos_activity_get_watchface())
    {
        COS_LOG_D("Skip showing app header for watchface activity");
        return;
    }

    COS_LOG_D("Show app header");
    if (!(app_header->container && lv_obj_is_valid(app_header->container)))
    {
        app_header->attached_to_view = false;
        COS_LOG_E("AppHeader container is invalid");
        return;
    }

    if (app_header->attached_to_view)
    {
        lv_obj_t *restore_parent = app_header->original_parent;
        if (!restore_parent || !lv_obj_is_valid(restore_parent))
        {
            restore_parent = cos_overlay_get_header_layer();
            app_header->original_parent = restore_parent;
        }

        if (_app_header_can_reparent(restore_parent))
        {
            lv_obj_set_parent(app_header->container, restore_parent);
        }
        app_header->attached_to_view = false;
    }
    // Get title text from current Activity
    const char *title = NULL;
    if (a)
        title = cos_activity_get_title(a);
    else
        title = cos_activity_get_title(cos_activity_get_current());
    if (title)
        lv_label_set_text(app_header->title_label, title);
    else
        lv_label_set_text(app_header->title_label, "");
    // Get title color from current Activity
    lv_color_t color = COS_COLOR_WHITE;
    if (a)
        color = cos_activity_get_title_color(a);
    else
        color = cos_activity_get_title_color(cos_activity_get_current());
    if (lv_obj_is_valid(app_header->title_label))
        lv_obj_set_style_text_color(app_header->title_label, color, 0);

    _app_header_apply_activity_mode(target_activity);
    _app_header_update_clock_label(app_header->clock_label);
    lv_obj_remove_flag(app_header->container, LV_OBJ_FLAG_HIDDEN);
}

void cos_app_header_set_visible_animated(cos_activity_t *a, bool visible, uint32_t duration_ms)
{
    COS_CHECK_PTR_RETURN(app_header);

    if (!(app_header->container && lv_obj_is_valid(app_header->container)))
        return;

    if (duration_ms == 0)
    {
        if (visible)
            cos_app_header_show(a);
        else
            cos_app_header_hide();
        return;
    }

    lv_obj_t *container = app_header->container;
    lv_anim_delete(container, _app_header_set_opa_anim_cb);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container);
    lv_anim_set_exec_cb(&anim, _app_header_set_opa_anim_cb);
    lv_anim_set_duration(&anim, duration_ms);

    if (visible)
    {
        cos_app_header_show(a);
        lv_obj_set_style_opa(container, LV_OPA_TRANSP, 0);
        lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_start(&anim);
    }
    else
    {
        if (lv_obj_has_flag(container, LV_OBJ_FLAG_HIDDEN))
            return;

        lv_obj_set_style_opa(container, LV_OPA_COVER, 0);
        lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_completed_cb(&anim, _app_header_fade_out_ready_cb);
        lv_anim_start(&anim);
    }
}

static void _app_header_slide_hide_ready_cb(lv_anim_t *a)
{
    lv_obj_t *container = (lv_obj_t *)a->var;
    if (!container || !lv_obj_is_valid(container))
    {
        return;
    }

    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_translate_y(container, 0, 0);
}

void cos_app_header_slide_visible_animated(cos_activity_t *a, bool visible, uint32_t duration_ms)
{
    COS_CHECK_PTR_RETURN(app_header);

    if (!(app_header->container && lv_obj_is_valid(app_header->container)))
    {
        return;
    }

    if (duration_ms == 0)
    {
        if (visible)
        {
            cos_app_header_show(a);
        }
        else
        {
            cos_app_header_hide();
        }
        return;
    }

    lv_obj_t *container = app_header->container;
    lv_anim_delete(container, _app_header_set_translate_y_anim_cb);
    lv_anim_delete(container, _app_header_set_opa_anim_cb);

    int32_t header_height = lv_obj_get_height(container);
    if (header_height <= 0)
    {
        header_height = _HEADER_HEIGHT;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container);
    lv_anim_set_exec_cb(&anim, _app_header_set_translate_y_anim_cb);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_duration(&anim, duration_ms);

    if (visible)
    {
        cos_app_header_show(a);
        lv_obj_remove_flag(container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_translate_y(container, -header_height, 0);
        lv_anim_set_values(&anim, -header_height, 0);
        lv_anim_start(&anim);
    }
    else
    {
        if (lv_obj_has_flag(container, LV_OBJ_FLAG_HIDDEN))
        {
            return;
        }

        lv_obj_set_style_translate_y(container, 0, 0);
        lv_anim_set_values(&anim, 0, -header_height);
        lv_anim_set_completed_cb(&anim, _app_header_slide_hide_ready_cb);
        lv_anim_start(&anim);
    }
}
/**
 * @brief Attach app header to specified View
 * @param view View to attach to
 */
void cos_app_header_attach_to_view(lv_obj_t *view)
{
    COS_CHECK_PTR_RETURN(app_header && view);

    if (!_app_header_can_reparent(view))
    {
        app_header->attached_to_view = false;
        return;
    }

    if (app_header->attached_to_view)
    {
        lv_obj_t *cur_parent = lv_obj_get_parent(app_header->container);
        if (cur_parent == view)
        {
            return;
        }
    }

    if (!app_header->original_parent || !lv_obj_is_valid(app_header->original_parent))
    {
        app_header->original_parent = cos_overlay_get_header_layer();
    }

    lv_obj_set_parent(app_header->container, view);
    app_header->attached_to_view = true;
}

/**
 * @brief Detach app header from View, restore to original parent object
 */
void cos_app_header_detach_from_view(void)
{
    COS_CHECK_PTR_RETURN(app_header);
    if (!app_header->attached_to_view)
    {
        return;
    }

    lv_obj_t *restore_parent = app_header->original_parent;
    if (!restore_parent || !lv_obj_is_valid(restore_parent))
    {
        restore_parent = cos_overlay_get_header_layer();
        app_header->original_parent = restore_parent;
    }

    if (_app_header_can_reparent(restore_parent))
    {
        lv_obj_set_parent(app_header->container, restore_parent);
    }
    app_header->attached_to_view = false;
}

bool cos_app_header_is_attached_to_view(void)
{
    COS_CHECK_PTR_RETURN_VAL(app_header, false);
    return app_header->attached_to_view;
}

bool cos_app_header_is_visible(void)
{
    COS_CHECK_PTR_RETURN_VAL(app_header, false);
    if (!app_header->container || !lv_obj_is_valid(app_header->container))
    {
        return false;
    }
    return !lv_obj_has_flag(app_header->container, LV_OBJ_FLAG_HIDDEN);
}

static void _update_title_label(lv_event_t *e)
{
    const char *title = cos_activity_get_title(cos_activity_get_current());
    if (title)
        lv_label_set_text(app_header->title_label, title);
    else
        lv_label_set_text(app_header->title_label, "");
}

static lv_image_dsc_t *_create_gradient_bg(void)
{
    const lv_coord_t img_w = COS_DISPLAY_WIDTH;
    const lv_coord_t img_h = _HEADER_HEIGHT;
    const uint32_t cf_bytes = 4;
    const uint32_t buf_size = (uint32_t)img_w * img_h * cf_bytes;

    uint8_t *buf = cos_cache_buf_alloc(buf_size);
    if (!buf)
    {
        COS_LOG_E("Failed to allocate gradient bg buffer");
        return NULL;
    }

    for (lv_coord_t y = 0; y < img_h; y++)
    {
        int frac = y * 255 / (img_h - 1);
        uint8_t opa;
        if (frac <= 125)
            opa = LV_OPA_90;
        else
            opa = LV_OPA_90 - (uint8_t)((uint32_t)LV_OPA_90 * (frac - 125) / (255 - 125));
        for (lv_coord_t x = 0; x < img_w; x++)
        {
            uint8_t *p = &buf[(y * img_w + x) * cf_bytes];
            p[0] = 0x00; // B
            p[1] = 0x00; // G
            p[2] = 0x00; // R
            p[3] = opa; // A
        }
    }

    lv_image_dsc_t *dsc = cos_malloc_zeroed(sizeof(lv_image_dsc_t));
    if (!dsc)
    {
        cos_cache_buf_free(buf);
        return NULL;
    }

    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = img_w;
    dsc->header.h = img_h;
    dsc->header.stride = img_w * cf_bytes;
    dsc->data = buf;
    dsc->data_size = buf_size;

    return dsc;
}

static void _grad_bg_img_delete_cb(lv_event_t *e)
{
    lv_image_dsc_t *dsc = lv_event_get_user_data(e);
    if (dsc)
    {
        if (dsc->data)
            cos_cache_buf_free((void *)dsc->data);
        cos_free(dsc);
    }
}

static void _app_header_container_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!app_header)
    {
        return;
    }
    /* container 被删除时清空子对象指针,防止后续悬垂访问
     * (日志实证:转场 snapshot 时 cos_app_header_is_visible 访问已释放的
     *  container → lv_obj_has_flag(NULL) assert → LV_ASSERT_HANDLER 死循环) */
    app_header->container = NULL;
    app_header->title_label = NULL;
    app_header->back_btn = NULL;
    app_header->clock_label = NULL;
}

void cos_app_header_init(void)
{
    COS_LOG_D("Init cos_app_header");
    app_header = cos_malloc_zeroed(sizeof(cos_app_header_t));
    COS_CHECK_PTR_RETURN_FREE(app_header, app_header);

    app_header->grad_bg_img = _create_gradient_bg();
    if (!app_header->grad_bg_img)
    {
        /* 降级继续:渐变背景缺失不影响 header 基本功能(标题/返回键/时钟)。
         * 原 COS_CHECK 直接返回导致整个组件瘫痪(container=NULL → 后续
         * Show/Hide/SetTitle 全部报 "container is invalid",污染每帧渲染)。 */
        COS_LOG_W("Gradient bg unavailable, app header runs without background");
    }

    // Semi-transparent container
    app_header->container = lv_obj_create(cos_overlay_get_header_layer());
    app_header->original_parent = lv_obj_get_parent(app_header->container); // Save original parent object
    lv_obj_remove_style_all(app_header->container);
    lv_obj_set_size(app_header->container, COS_DISPLAY_WIDTH, _HEADER_HEIGHT);
    lv_obj_align(app_header->container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_image_src(app_header->container, app_header->grad_bg_img, 0);
    lv_obj_set_style_bg_image_opa(app_header->container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(app_header->container, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(app_header->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(app_header->container, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(app_header->container, _grad_bg_img_delete_cb, LV_EVENT_DELETE, app_header->grad_bg_img);
    lv_obj_add_event_cb(app_header->container, _app_header_container_delete_cb, LV_EVENT_DELETE, NULL);

    // Back button
    app_header->back_btn = cos_back_btn_create(app_header->container, false);
    _set_back_btn_style(app_header->back_btn);

    // Clock label
    app_header->clock_label = lv_label_create(app_header->container);
    lv_obj_add_style(app_header->clock_label, cos_theme_get_label_style(), 0);
    app_header->clock_timer =
        lv_timer_create(_clock_update_cb, _HEADER_CLOCK_UPDATE_PERIOD_MINUTES * 60 * 1000, app_header->clock_label);
    lv_timer_set_repeat_count(app_header->clock_timer, -1);
    _app_header_update_clock_label(app_header->clock_label);
    lv_obj_align(app_header->clock_label, LV_ALIGN_RIGHT_MID, -_HEADER_MARGIN_RIGHT, -20);

    // Title label
    app_header->title_label = lv_label_create(app_header->container);
    _set_title_style(app_header->title_label);
    lv_obj_add_event_cb(app_header->title_label, _update_title_label, LV_EVENT_REFRESH, NULL);

    // Hide app_header by default
    lv_obj_add_flag(app_header->container, LV_OBJ_FLAG_HIDDEN);

    app_header->is_anim_entering = false;
    app_header->attached_to_view = false;
}
