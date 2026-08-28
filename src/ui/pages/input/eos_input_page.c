/**
 * @file eos_input_page.c
 * @brief Implementation of the input page for Canto Mk.6
 */

#include "eos_input_page.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "eos_activity.h"
#include "eos_app_header.h"
#include "eos_icon.h"
#include "eos_mem.h"
#include "eos_theme.h"
#include "eos_round_keyboard.h"
#define EOS_LOG_TAG "InputPage"
#include "eos_log.h"

/* Macros and Definitions -------------------------------------*/
#define MAX_INPUT_LENGTH (512 - 1)
/* 240x240 圆屏垂直布局: top_bar(14+34) + textarea(66) + keyboard(120) = 234 */
#define BUTTON_HEIGHT 34
#define TEXTAREA_HEIGHT 66
#define KEYBOARD_HEIGHT 120
#define INPUT_PAGE_CTX_MAGIC 0x49504D47U

/* Variables --------------------------------------------------*/
typedef struct
{
    uint32_t magic;
    eos_activity_t *activity;
    lv_obj_t *root;
    lv_obj_t *textarea;
    lv_obj_t *label_target;
    lv_obj_t *keyboard;
    eos_input_close_callback_t close_callback;
    void *user_data;
} _input_page_ctx_t;

/* Forward declarations */
static void _on_activity_destroy(eos_activity_t *activity);
static void _input_page_register_transition_anim_route(void);
static void _input_page_slide_in_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to);
static void _input_page_slide_out_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to);
static _input_page_ctx_t *_input_page_get_ctx(eos_activity_t *activity);
static const eos_activity_lifecycle_t _input_page_lifecycle = {
    .on_destroy = _on_activity_destroy,
};

/* Callback data structure for async execution */
typedef struct
{
    char text[MAX_INPUT_LENGTH + 1];
    eos_input_close_callback_t callback;
    void *user_data;
    eos_input_result_t result;
    _input_page_ctx_t *ctx;
} _callback_param_t;

/* Data structure for deferred (transition-waiting) enter */
typedef struct
{
    eos_activity_t *activity;
    _input_page_ctx_t *ctx;
    eos_activity_type_t from_type;
    uint32_t retry;
} _input_enter_param_t;

/* Function Implementations -----------------------------------*/

static void _input_page_set_translate_y(lv_obj_t *obj, int32_t value)
{
    if (obj && lv_obj_is_valid(obj))
    {
        lv_obj_set_style_translate_y(obj, value, 0);
    }
}

static void _input_page_anim_translate_y_cb(void *var, int32_t value)
{
    _input_page_set_translate_y((lv_obj_t *)var, value);
}

static void _input_page_anim_init(lv_anim_t *anim, lv_obj_t *obj, int32_t start, int32_t end, uint32_t duration)
{
    lv_anim_init(anim);
    lv_anim_set_var(anim, obj);
    lv_anim_set_values(anim, start, end);
    lv_anim_set_duration(anim, duration);
    lv_anim_set_exec_cb(anim, _input_page_anim_translate_y_cb);
    lv_anim_set_path_cb(anim, lv_anim_path_ease_in_out);
}

static void _input_page_enter_anim(void *param)
{
    _input_page_ctx_t *ctx = (_input_page_ctx_t *)param;
    if (!ctx || !ctx->root || !lv_obj_is_valid(ctx->root))
    {
        return;
    }

    lv_display_t *display = lv_obj_get_display(ctx->root);
    int32_t height = display ? lv_display_get_vertical_resolution(display) : 0;
    if (height <= 0)
    {
        height = TEXTAREA_HEIGHT + KEYBOARD_HEIGHT + BUTTON_HEIGHT;
    }

    _input_page_set_translate_y(ctx->root, height);

    lv_anim_t anim;
    _input_page_anim_init(&anim, ctx->root, height, 0, 260);
    lv_anim_start(&anim);
}

static int32_t _input_page_get_slide_height(lv_obj_t *obj)
{
    lv_display_t *display = obj ? lv_obj_get_display(obj) : NULL;
    int32_t height = display ? lv_display_get_vertical_resolution(display) : 0;
    if (height <= 0)
    {
        height = TEXTAREA_HEIGHT + KEYBOARD_HEIGHT + BUTTON_HEIGHT;
    }

    return height;
}

static _input_page_ctx_t *_input_page_get_ctx(eos_activity_t *activity)
{
    _input_page_ctx_t *ctx = activity ? (_input_page_ctx_t *)eos_activity_get_user_data(activity) : NULL;
    if (!ctx || ctx->magic != INPUT_PAGE_CTX_MAGIC || ctx->activity != activity)
    {
        return NULL;
    }

    return ctx;
}

static void _input_page_slide_out_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to)
{
    if (!at || !from)
    {
        return;
    }

    _input_page_ctx_t *ctx = _input_page_get_ctx(from);
    if (!ctx || !ctx->root || !lv_obj_is_valid(ctx->root))
    {
        return;
    }

    int32_t height = _input_page_get_slide_height(ctx->root);

    lv_obj_t *to_view = to ? eos_activity_get_view(to) : NULL;
    if (to_view && lv_obj_is_valid(to_view))
    {
        lv_obj_remove_flag(to_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(to_view);
    }

    lv_obj_t *from_snapshot = eos_activity_take_snapshot(from, false);
    lv_obj_t *anim_target = (from_snapshot && lv_obj_is_valid(from_snapshot)) ? from_snapshot : ctx->root;

    lv_obj_set_style_translate_y(anim_target, 0, 0);

    lv_anim_t anim;
    _input_page_anim_init(&anim, anim_target, 0, height, 220);
    lv_anim_timeline_add(at, 0, &anim);

    if (to && eos_activity_is_app_header_visible(to))
    {
        eos_app_header_slide_visible_animated(to, true, 220);
    }
}

static void _input_page_slide_in_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to)
{
    if (!at || !to)
    {
        return;
    }

    _input_page_ctx_t *ctx = _input_page_get_ctx(to);
    if (!ctx || !ctx->root || !lv_obj_is_valid(ctx->root))
    {
        return;
    }

    lv_obj_t *to_view = eos_activity_get_view(to);
    if (to_view && lv_obj_is_valid(to_view))
    {
        lv_obj_remove_flag(to_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(to_view);
    }

    int32_t height = _input_page_get_slide_height(ctx->root);
    lv_obj_set_style_translate_y(ctx->root, height, 0);

    lv_anim_t anim;
    _input_page_anim_init(&anim, ctx->root, height, 0, 260);
    lv_anim_timeline_add(at, 0, &anim);

    if (from && eos_activity_is_app_header_visible(from))
    {
        eos_app_header_slide_visible_animated(NULL, false, 220);
    }
}

static void _input_page_register_transition_anim_route(void)
{
    static bool close_route_registered = false;
    static bool open_route_registered = false;

    if (!close_route_registered)
    {
        if (eos_activity_register_anim_route(EOS_ACTIVITY_TYPE_INPUT_PAGE,
                                             EOS_ACTIVITY_TYPE_APP,
                                             _input_page_slide_out_anim_cb)
            == EOS_OK)
        {
            close_route_registered = true;
        }
    }

    if (!open_route_registered)
    {
        if (eos_activity_register_anim_route(EOS_ACTIVITY_TYPE_APP,
                                             EOS_ACTIVITY_TYPE_INPUT_PAGE,
                                             _input_page_slide_in_anim_cb)
            == EOS_OK)
        {
            open_route_registered = true;
        }
    }
}

static void _async_execute_callback_and_close(void *param)
{
    _callback_param_t *cb_param = (_callback_param_t *)param;

    if (cb_param && cb_param->callback)
    {
        cb_param->callback(cb_param->text, cb_param->result, cb_param->user_data);
    }

    if (cb_param)
    {
        eos_free(cb_param);
    }

    eos_activity_back();
}

/* Enter the activity once the previous activity transition has finished.
 * eos_activity_enter() is silently dropped while a transition is running,
 * so opening the input page right after a close callback (e.g. the second
 * step of passcode setup) would never show the page/keyboard.
 *
 * Implemented with a low-frequency periodic lv_timer instead of chained
 * lv_async_call() retries: lv_async_call() creates period=0 timers and
 * lv_timer_handler() restarts its walk whenever a timer is created/deleted,
 * so rescheduling from inside the callback re-executes it in the SAME frame
 * in an endless loop (ui task watchdog). A 50ms periodic timer only fires
 * on later frames, so it is safe here. */
#define INPUT_PAGE_ENTER_DEFER_PERIOD_MS 50u
#define INPUT_PAGE_ENTER_DEFER_MAX_RETRY 100u /* 50ms * 100 = 5s 兜底 */

static void _input_page_enter_deferred(lv_timer_t *timer)
{
    _input_enter_param_t *p = (_input_enter_param_t *)lv_timer_get_user_data(timer);
    if (!p)
    {
        lv_timer_delete(timer);
        return;
    }

    if (eos_activity_is_transition_in_progress() && (p->retry < INPUT_PAGE_ENTER_DEFER_MAX_RETRY))
    {
        /* still transitioning: keep waiting (the periodic timer re-fires) */
        p->retry++;
        return;
    }

    lv_timer_delete(timer);

    eos_activity_t *activity = p->activity;
    _input_page_ctx_t *ctx = p->ctx;
    eos_activity_type_t from_type = p->from_type;
    eos_free(p);

    if (!activity)
    {
        return;
    }

    /* sanity: the activity must still be alive and own this context */
    if (eos_activity_get_user_data(activity) != (void *)ctx)
    {
        return;
    }

    _input_page_register_transition_anim_route();
    eos_activity_enter(activity);

    if (from_type != EOS_ACTIVITY_TYPE_APP)
    {
        eos_app_header_slide_visible_animated(activity, false, 220);
        if (ctx && ctx->root && lv_obj_is_valid(ctx->root))
        {
            lv_async_call(_input_page_enter_anim, ctx);
        }
    }

    EOS_LOG_I("Input page opened");
}

static void _on_cancel_btn_clicked(lv_event_t *e)
{
    _input_page_ctx_t *ctx = (_input_page_ctx_t *)lv_event_get_user_data(e);
    if (!ctx)
    {
        return;
    }

    const char *textarea_text = ctx->textarea ? lv_textarea_get_text(ctx->textarea) : NULL;

    if (ctx->close_callback)
    {
        _callback_param_t *cb_param = (_callback_param_t *)eos_malloc(sizeof(_callback_param_t));
        if (cb_param)
        {
            if (textarea_text)
            {
                strncpy(cb_param->text, textarea_text, MAX_INPUT_LENGTH);
                cb_param->text[MAX_INPUT_LENGTH] = '\0';
            }
            else
            {
                cb_param->text[0] = '\0';
            }
            cb_param->callback = ctx->close_callback;
            cb_param->user_data = ctx->user_data;
            cb_param->result = EOS_INPUT_RESULT_CANCEL;
            cb_param->ctx = ctx;

            lv_async_call(_async_execute_callback_and_close, cb_param);
        }
        else
        {
            eos_activity_back();
        }
    }
    else
    {
        eos_activity_back();
    }
}

static void _on_ok_btn_clicked(lv_event_t *e)
{
    _input_page_ctx_t *ctx = (_input_page_ctx_t *)lv_event_get_user_data(e);
    if (!ctx)
    {
        return;
    }

    const char *textarea_text = ctx->textarea ? lv_textarea_get_text(ctx->textarea) : NULL;
    char text_copy[MAX_INPUT_LENGTH + 1];

    if (textarea_text)
    {
        strncpy(text_copy, textarea_text, MAX_INPUT_LENGTH);
        text_copy[MAX_INPUT_LENGTH] = '\0';
    }
    else
    {
        text_copy[0] = '\0';
    }

    if (ctx->close_callback)
    {
        _callback_param_t *cb_param = (_callback_param_t *)eos_malloc(sizeof(_callback_param_t));
        if (cb_param)
        {
            strcpy(cb_param->text, text_copy);
            cb_param->callback = ctx->close_callback;
            cb_param->user_data = ctx->user_data;
            cb_param->result = EOS_INPUT_RESULT_OK;
            cb_param->ctx = ctx;

            lv_async_call(_async_execute_callback_and_close, cb_param);
        }
        else
        {
            eos_activity_back();
        }
    }
    else if (ctx->label_target && lv_obj_is_valid(ctx->label_target))
    {
        lv_label_set_text(ctx->label_target, text_copy);
        eos_activity_back();
    }
    else
    {
        eos_activity_back();
    }
}

static void _on_delete_btn_clicked(lv_event_t *e)
{
    _input_page_ctx_t *ctx = (_input_page_ctx_t *)lv_event_get_user_data(e);
    if (ctx && ctx->textarea)
    {
        lv_textarea_delete_char(ctx->textarea);
    }
}

static void _on_activity_destroy(eos_activity_t *activity)
{
    _input_page_ctx_t *ctx = eos_activity_get_user_data(activity);

    if (ctx)
    {
        if (ctx->root && lv_obj_is_valid(ctx->root))
        {
            lv_obj_set_user_data(ctx->root, NULL);
        }

        eos_free(ctx);
        eos_activity_set_user_data(activity, NULL);
    }

    EOS_LOG_I("Input page destroyed");
}

eos_result_t eos_input_page_open(lv_obj_t *label)
{
    return eos_input_page_open_with_callback(label, NULL, NULL);
}

eos_result_t eos_input_page_open_with_callback(lv_obj_t *label,
                                               eos_input_close_callback_t close_callback,
                                               void *user_data)
{
    /* Create Activity */
    eos_activity_t *activity = eos_activity_create(&_input_page_lifecycle);
    if (activity == NULL)
    {
        EOS_LOG_E("Failed to create activity");
        return EOS_FAILED;
    }

    eos_activity_set_type(activity, EOS_ACTIVITY_TYPE_INPUT_PAGE);

    _input_page_ctx_t *ctx = (_input_page_ctx_t *)eos_malloc_zeroed(sizeof(_input_page_ctx_t));
    if (!ctx)
    {
        EOS_LOG_E("Failed to allocate input page context");
        eos_activity_back();
        return EOS_FAILED;
    }

    ctx->magic = INPUT_PAGE_CTX_MAGIC;
    ctx->activity = activity;
    ctx->label_target = label;
    ctx->close_callback = close_callback;
    ctx->user_data = user_data;

    eos_activity_set_user_data(activity, ctx);

    /* Create root container */
    ctx->root = eos_activity_get_view(activity);
    if (!ctx->root)
    {
        EOS_LOG_E("Failed to get activity view");
        eos_free(ctx);
        eos_activity_back();
        return EOS_FAILED;
    }

    lv_obj_set_user_data(ctx->root, ctx);
    lv_obj_set_size(ctx->root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(ctx->root, 0, 0);
    lv_obj_set_style_border_width(ctx->root, 0, 0);
    lv_obj_set_style_bg_color(ctx->root, lv_color_black(), 0);
    lv_obj_set_flex_flow(ctx->root, LV_FLEX_FLOW_COLUMN);

    /* ============ Create top button bar ============ */
    lv_obj_t *top_bar = lv_obj_create(ctx->root);
    lv_obj_set_width(top_bar, lv_pct(100));
    lv_obj_set_height(top_bar, BUTTON_HEIGHT);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 10, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_hor(top_bar, 40, 0);
    lv_obj_set_style_margin_top(top_bar, 14, 0);

    /* Cancel button */
    lv_obj_t *cancel_btn = lv_button_create(top_bar);
    lv_obj_set_size(cancel_btn, 68, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(cancel_btn, EOS_THEME_BUTTON_COLOR, 0);
    lv_obj_set_style_border_width(cancel_btn, 0, 0);
    lv_obj_set_style_radius(cancel_btn, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    eos_label_set_text_id(cancel_label, STR_ID_CANCEL);
    lv_obj_center(cancel_label);

    lv_obj_add_event_cb(cancel_btn, _on_cancel_btn_clicked, LV_EVENT_CLICKED, ctx);

    /* OK button */
    lv_obj_t *ok_btn = lv_button_create(top_bar);
    lv_obj_set_size(ok_btn, 68, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ok_btn, EOS_THEME_BUTTON_COLOR, 0);
    lv_obj_set_style_border_width(ok_btn, 0, 0);
    lv_obj_set_style_radius(ok_btn, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *ok_label = lv_label_create(ok_btn);
    eos_label_set_text_id(ok_label, STR_ID_DONE);
    lv_obj_center(ok_label);
    lv_obj_set_style_text_color(ok_label, EOS_THEME_PRIMARY_COLOR, 0);
    lv_obj_add_event_cb(ok_btn, _on_ok_btn_clicked, LV_EVENT_CLICKED, ctx);

    /* ============ Create textarea container with delete button ============ */
    lv_obj_t *textarea_container = lv_obj_create(ctx->root);
    lv_obj_set_width(textarea_container, lv_pct(100));
    lv_obj_set_height(textarea_container, TEXTAREA_HEIGHT);
    lv_obj_set_style_bg_color(textarea_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(textarea_container, 0, 0);
    lv_obj_set_style_pad_all(textarea_container, 10, 0);
    lv_obj_set_flex_flow(textarea_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(textarea_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(textarea_container, LV_OBJ_FLAG_SCROLLABLE);

    /* Textarea */
    ctx->textarea = lv_textarea_create(textarea_container);
    lv_obj_set_width(ctx->textarea, lv_pct(85));
    lv_obj_set_height(ctx->textarea, lv_pct(100));
    lv_textarea_set_max_length(ctx->textarea, MAX_INPUT_LENGTH);
    lv_textarea_set_one_line(ctx->textarea, false);
    lv_obj_set_style_bg_opa(ctx->textarea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(ctx->textarea, lv_color_black(), 0);
    lv_obj_set_style_border_width(ctx->textarea, 0, 0);
    lv_obj_set_style_pad_all(ctx->textarea, 0, 0);
    lv_obj_set_style_text_color(ctx->textarea, EOS_COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(ctx->textarea, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ctx->textarea, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ctx->textarea, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ctx->textarea, EOS_COLOR_BLUE, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ctx->textarea, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(ctx->textarea, 400, LV_PART_CURSOR | LV_STATE_FOCUSED);

    /* If an input label was provided, prefill the textarea with its text */
    if (ctx->label_target && lv_obj_is_valid(ctx->label_target))
    {
        const char *init_text = lv_label_get_text(ctx->label_target);
        if (init_text)
        {
            lv_textarea_set_text(ctx->textarea, init_text);
        }
    }

    /* 键盘"确定"键发送 READY 事件 → 等效点击右上 OK */
    lv_obj_add_event_cb(ctx->textarea, _on_ok_btn_clicked, LV_EVENT_READY, ctx);

    /* Delete button (label with symbol) */
    lv_obj_t *delete_btn = lv_label_create(textarea_container);
    lv_label_set_text(delete_btn, RI_DELETE_BACK_2_LINE);
    lv_obj_add_flag(delete_btn, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(delete_btn, _on_delete_btn_clicked, LV_EVENT_CLICKED, ctx);

    /* ============ Create round keyboard (half-circle, fits 240x240 round screen) ============ */
    ctx->keyboard = eos_round_keyboard_create(ctx->root);
    eos_round_keyboard_set_textarea(ctx->keyboard, ctx->textarea);

    /* Focus the textarea and move cursor to the end so input is ready immediately */
    if (ctx->textarea && lv_obj_is_valid(ctx->textarea))
    {
        const char *ta_text = lv_textarea_get_text(ctx->textarea);
        if (ta_text)
        {
            lv_textarea_set_cursor_pos(ctx->textarea, strlen(ta_text));
        }
        else
        {
            lv_textarea_set_cursor_pos(ctx->textarea, 0);
        }
        lv_obj_add_state(ctx->textarea, LV_STATE_FOCUSED);
    }

    eos_activity_t *current = eos_activity_get_current();
    eos_activity_type_t current_type = current ? eos_activity_get_type(current) : EOS_ACTIVITY_TYPE_NULL;

    /* Enter Activity */
    _input_page_register_transition_anim_route();

    if (eos_activity_is_transition_in_progress())
    {
        /* A switch animation (e.g. slide-out of the previous input page) is
         * still running: eos_activity_enter() would be silently dropped and
         * this page would never appear (no keyboard). Defer the enter until
         * the transition completes and keep the view hidden meanwhile. */
        if (ctx->root && lv_obj_is_valid(ctx->root))
        {
            lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_HIDDEN);
        }

        _input_enter_param_t *p = (_input_enter_param_t *)eos_malloc(sizeof(_input_enter_param_t));
        if (p)
        {
            p->activity = activity;
            p->ctx = ctx;
            p->from_type = current_type;
            p->retry = 0;

            lv_timer_t *timer =
                lv_timer_create(_input_page_enter_deferred, INPUT_PAGE_ENTER_DEFER_PERIOD_MS, p);
            if (!timer)
            {
                /* fallback: try a direct enter anyway */
                eos_free(p);
                eos_activity_enter(activity);
                if (current_type != EOS_ACTIVITY_TYPE_APP)
                {
                    eos_app_header_slide_visible_animated(activity, false, 220);
                    lv_async_call(_input_page_enter_anim, ctx);
                }
            }
        }
        else
        {
            /* fallback: try a direct enter anyway */
            eos_activity_enter(activity);
            if (current_type != EOS_ACTIVITY_TYPE_APP)
            {
                eos_app_header_slide_visible_animated(activity, false, 220);
                lv_async_call(_input_page_enter_anim, ctx);
            }
        }

        return EOS_OK;
    }

    eos_activity_enter(activity);

    if (current_type != EOS_ACTIVITY_TYPE_APP)
    {
        eos_app_header_slide_visible_animated(activity, false, 220);
        lv_async_call(_input_page_enter_anim, ctx);
    }

    EOS_LOG_I("Input page opened");
    return EOS_OK;
}
