/**
 * @file cos_activity.c
 * @brief Activity controller
 */

#include "cos_activity.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cos_stack.h"
#include "cos_mem.h"
#define COS_LOG_TAG "Activity"
#include "cos_log.h"
#include "cos_core.h"
#include "cos_config.h"
#include "cos_dispatcher.h"
#include "cos_theme.h"
#include "cos_lang.h"
#include "cos_basic_widgets.h"
#include "cos_app_header.h"
#include "cos_chrome_manager.h"
#include "cos_overlay_layer.h"
#include "cos_bg_indicator.h"
#include "cos_event.h"
#include "cos_image.h"
#include "cos_touch.h"
/* Macros and Definitions -------------------------------------*/
#define _ACTIVITY_STACK_INIT_CAPACITY 8
#define _DEFAULT_TITLE_COLOR COS_COLOR_BLUE
#define _SNAPSHOT_COLOR_FORMAT LV_COLOR_FORMAT_RGB565
#define _DEBUG_SNAPSHOT 0

typedef enum
{
    _TITLE_TYPE_INVALID = 0,
    _TITLE_TYPE_STRING,
    _TITLE_TYPE_ID
} cos_activity_title_type_t;

typedef struct cos_activity_snapshot_node_t
{
    lv_obj_t *snapshot_obj;
    lv_draw_buf_t *draw_buf;
    cos_activity_t *owner;
    struct cos_activity_snapshot_node_t *next;
} cos_activity_snapshot_node_t;

typedef struct
{
    lv_anim_timeline_t *at;
    lv_anim_t dummy_anim;

    cos_activity_t *from;
    cos_activity_t *to;
    bool destroy_from;
    bool cleanup_scheduled;
    cos_activity_snapshot_node_t *snapshots;
} cos_activity_anim_ctx_t;

struct cos_activity_t
{
    lv_obj_t *view;
    cos_activity_type_t type;
    uint8_t snapshot_ref_count;
    bool is_app_header_visible;
    bool is_app_header_time_only;
    lv_color_t app_header_time_only_text_color;
    bool destroy_on_exit;
    bool has_started;
    bool keep_alive_on_back;
    struct
    {
        lv_color_t color;
        union
        {
            char *string;
            uint32_t id;
        };
        cos_activity_title_type_t type;
    } title;

    cos_activity_lifecycle_t lifecycle;

    void *user_data;

    void *fault_panel;
};

typedef struct
{
    cos_activity_t *root_activity; /**< Root Activity (e.g., watchface), not in stack */
    cos_activity_t *current_activity;
    cos_activity_t *visible_activity;
    cos_activity_t *previous_activity;
    cos_stack_t *activity_stack;
    lv_obj_t *root_screen;
    bool transition_in_progress;
    bool snapshot_capture_window;
    cos_activity_anim_ctx_t *active_anim_ctx;
} cos_activity_ctx_t;

/* Variables --------------------------------------------------*/
static cos_activity_ctx_t _activity_ctx = {
    .root_activity = NULL,
    .current_activity = NULL,
    .visible_activity = NULL,
    .previous_activity = NULL,
    .activity_stack = NULL,
    .root_screen = NULL,
    .transition_in_progress = false,
    .snapshot_capture_window = false,
    .active_anim_ctx = NULL,
};

static cos_activity_anim_cb_t _anim_callback_routes[COS_ACTIVITY_TYPE_COUNT][COS_ACTIVITY_TYPE_COUNT] = {0};

/* Function Implementations -----------------------------------*/
static const char *_activity_type_to_str(cos_activity_type_t type)
{
    switch (type)
    {
        case COS_ACTIVITY_TYPE_NULL:
            return "NULL";
        case COS_ACTIVITY_TYPE_APP:
            return "APP";
        case COS_ACTIVITY_TYPE_INPUT_PAGE:
            return "INPUT_PAGE";
        case COS_ACTIVITY_TYPE_APP_LIST:
            return "APP_LIST";
        case COS_ACTIVITY_TYPE_WATCHFACE:
            return "WATCHFACE";
        case COS_ACTIVITY_TYPE_WATCHFACE_LIST:
            return "WATCHFACE_LIST";
        case COS_ACTIVITY_TYPE_LOCK_SCREEN:
            return "LOCK_SCREEN";
        default:
            return "UNKNOWN";
    }
}

static void _update_app_header_if_needed(cos_activity_t *activity);
static void _anim_timeline_start(cos_activity_t *from, cos_activity_t *to, cos_activity_anim_ctx_t *anim_ctx);
static void _init_anim_timeline(cos_activity_anim_ctx_t *anim_ctx);
static void _anim_dummy_exec_cb(void *var, int32_t value);
static void _anim_clean_up_activity_deferred(void *user_data);
static void _activity_mark_visible(cos_activity_t *activity);
static void _snapshot_img_delete_cb(lv_event_t *e);
static void _activity_snapshot_hold(cos_activity_t *activity);
static void _activity_snapshot_release(cos_activity_t *activity);

static bool _controller_initialized(void)
{
    return _activity_ctx.activity_stack != NULL && _activity_ctx.root_screen != NULL;
}

static void _activity_run_destroy(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN(activity);

    COS_LOG_I("Activity destroy begin: activity=%p type=%s destroy_on_exit=%d started=%d view=%p valid=%d "
              "snapshot_ref=%u header_visible=%d header_time_only=%d user_data=%p",
              (void *)activity,
              _activity_type_to_str(activity->type),
              activity->destroy_on_exit,
              activity->has_started,
              (void *)activity->view,
              (activity->view && lv_obj_is_valid(activity->view)),
              activity->snapshot_ref_count,
              activity->is_app_header_visible,
              activity->is_app_header_time_only,
              activity->user_data);

    // Call on_destroy callback before destroying resources
    if (activity->lifecycle.on_destroy)
    {
        activity->lifecycle.on_destroy(activity);
    }

    activity->user_data = NULL;

    if (activity->view && lv_obj_is_valid(activity->view))
    {
        lv_obj_delete(activity->view);
        activity->view = NULL;
    }

    if (activity->title.type == _TITLE_TYPE_STRING)
    {
        if (activity->title.string)
        {
            cos_free(activity->title.string);
            activity->title.string = NULL;
        }
    }

    /* Clear any dangling context pointers that still reference
       this activity so subsequent code does not read freed memory. */
    if (_activity_ctx.previous_activity == activity)
        _activity_ctx.previous_activity = NULL;
    if (_activity_ctx.visible_activity == activity)
        _activity_ctx.visible_activity = NULL;
    if (_activity_ctx.current_activity == activity)
        _activity_ctx.current_activity = NULL;
    /* NOTE: root_activity is intentionally NOT cleared here.
       It is managed explicitly by cos_activity_replace_root. */

    cos_free(activity);

    COS_LOG_I("Activity destroy end");
}

static void _activity_reset_context(void)
{
    _activity_ctx.root_activity = NULL;
    _activity_ctx.current_activity = NULL;
    _activity_ctx.visible_activity = NULL;
    _activity_ctx.previous_activity = NULL;
    _activity_ctx.activity_stack = NULL;
    _activity_ctx.root_screen = NULL;
    _activity_ctx.transition_in_progress = false;
    _activity_ctx.snapshot_capture_window = false;
    _activity_ctx.active_anim_ctx = NULL;
    memset(_anim_callback_routes, 0, sizeof(_anim_callback_routes));
}

static void _activity_controller_init_failed(cos_activity_t *root_activity)
{
    if (_activity_ctx.activity_stack)
    {
        cos_stack_destroy(_activity_ctx.activity_stack);
        _activity_ctx.activity_stack = NULL;
    }

    if (_activity_ctx.root_screen && lv_obj_is_valid(_activity_ctx.root_screen))
    {
        lv_obj_delete(_activity_ctx.root_screen);
        if (root_activity && root_activity->view && !lv_obj_is_valid(root_activity->view))
        {
            root_activity->view = NULL;
        }
    }

    _activity_reset_context();
}

static void _reset_view_visual_state(cos_activity_t *activity)
{
    if (!activity || !activity->view || !lv_obj_is_valid(activity->view))
    {
        return;
    }

    lv_obj_t *view = activity->view;

    lv_coord_t w = lv_obj_get_width(view);
    lv_coord_t h = lv_obj_get_height(view);

    lv_obj_set_style_opa(view, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_x(view, 256, LV_PART_MAIN);
    lv_obj_set_style_transform_scale_y(view, 256, LV_PART_MAIN);
    lv_obj_set_style_translate_x(view, 0, LV_PART_MAIN);
    lv_obj_set_style_translate_y(view, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(view, 0, LV_PART_MAIN);
    lv_obj_set_pos(view, 0, 0);
    lv_obj_set_width(view, w);
    lv_obj_set_height(view, h);
}

static void _activity_show(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN(activity);
    if (!activity->view || !lv_obj_is_valid(activity->view))
    {
        COS_LOG_W("_activity_show: Invalid or NULL view for activity, skipping");
        return;
    }

    lv_obj_remove_flag(activity->view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(activity->view);

    /* 后台运行指示器只在主界面（watchface/root）出现（用户需求） */
    cos_bg_indicator_set_visible(activity == _activity_ctx.root_activity);
}

static void _activity_move_to_background(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN(activity);

    COS_LOG_I("Activity moving to background (keep-alive): activity=%p[%s]",
              (void *)activity,
              _activity_type_to_str(activity->type));

    /* Hide the view so it does not linger on screen above the next activity. */
    if (activity->view && lv_obj_is_valid(activity->view))
    {
        lv_obj_add_flag(activity->view, LV_OBJ_FLAG_HIDDEN);
    }

    /* on_pause lets the app layer suspend its program and register in the
     * Background App Indicator. Note: _activity_switch_to() only calls on_pause
     * on a FORWARD switch, never on back, so we call it explicitly here. */
    if (activity->lifecycle.on_pause)
    {
        activity->lifecycle.on_pause(activity);
    }
}

static void _anim_dummy_exec_cb(void *var, int32_t value)
{
    LV_UNUSED(var);
    LV_UNUSED(value);
}

static void _activity_mark_visible(cos_activity_t *activity)
{
    _activity_ctx.visible_activity = activity;
    _activity_ctx.transition_in_progress = false;
    COS_LOG_I("Activity visible updated: visible=%p[%s] current=%p[%s] trans=%d",
              (void *)_activity_ctx.visible_activity,
              _activity_type_to_str(_activity_ctx.visible_activity ? _activity_ctx.visible_activity->type
                                                                   : COS_ACTIVITY_TYPE_NULL),
              (void *)_activity_ctx.current_activity,
              _activity_type_to_str(_activity_ctx.current_activity ? _activity_ctx.current_activity->type
                                                                   : COS_ACTIVITY_TYPE_NULL),
              _activity_ctx.transition_in_progress);
    cos_event_post(COS_EVENT_ACTIVITY_SCREEN_SWITCHED,
                   activity ? activity->view : NULL,
                   activity ? activity->view : NULL);
}

static void _snapshot_img_delete_cb(lv_event_t *e)
{
    cos_activity_snapshot_node_t *snapshot_node = lv_event_get_user_data(e);
    if (!snapshot_node)
    {
        return;
    }

    if (snapshot_node->draw_buf)
    {
        cos_draw_buf_destroy(snapshot_node->draw_buf);
        snapshot_node->draw_buf = NULL;
    }

    if (snapshot_node->owner)
    {
        _activity_snapshot_release(snapshot_node->owner);
        snapshot_node->owner = NULL;
    }
}

static void _activity_snapshot_hold(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN(activity);
    if (activity->snapshot_ref_count < UINT8_MAX)
    {
        activity->snapshot_ref_count++;
    }

    if (activity->view)
    {
        lv_obj_add_flag(activity->view, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _activity_snapshot_release(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN(activity);
    if (activity->snapshot_ref_count > 0)
    {
        activity->snapshot_ref_count--;
    }
}

static void _anim_clean_up_activity_deferred(void *user_data)
{
    cos_activity_anim_ctx_t *anim_ctx = (cos_activity_anim_ctx_t *)user_data;
    if (!anim_ctx)
    {
        return;
    }

    /* NOTE: do NOT dereference anim_ctx->from here — if it is already freed
     * (use-after-free) the argument evaluation crashes before the log prints,
     * hiding the root cause. Print raw pointers only. */
    COS_LOG_I("Anim cleanup begin: anim_ctx=%p from=%p to=%p[%s destroy=%d] destroy_from=%d snapshots=%p",
              (void *)anim_ctx,
              (void *)anim_ctx->from,
              (void *)anim_ctx->to,
              _activity_type_to_str(anim_ctx->to ? anim_ctx->to->type : COS_ACTIVITY_TYPE_NULL),
              anim_ctx->to ? anim_ctx->to->destroy_on_exit : false,
              anim_ctx->destroy_from,
              (void *)anim_ctx->snapshots);

    cos_activity_snapshot_node_t *node = anim_ctx->snapshots;
    while (node)
    {
        cos_activity_snapshot_node_t *next = node->next;
        if (node->snapshot_obj && lv_obj_is_valid(node->snapshot_obj))
        {
            lv_obj_delete(node->snapshot_obj);
        }
        cos_free(node);
        node = next;
    }
    anim_ctx->snapshots = NULL;

    if (anim_ctx->destroy_from && anim_ctx->from)
    {
        if (!cos_activity_is_app_header_visible(anim_ctx->to))
        {
            cos_app_header_hide();
        }
        _activity_run_destroy(anim_ctx->from);
        anim_ctx->from = NULL;
    }
    else if (!cos_activity_is_app_header_visible(anim_ctx->to) && cos_activity_is_app_header_visible(anim_ctx->from))
    {
        if (!cos_app_header_is_attached_to_view())
        {
            cos_app_header_hide();
        }
    }

    if (anim_ctx->to && anim_ctx->to->snapshot_ref_count == 0)
    {
        _reset_view_visual_state(anim_ctx->to);
        _activity_show(anim_ctx->to);
    }
    if (anim_ctx->to && cos_activity_is_app_header_visible(anim_ctx->to))
    {
        cos_app_header_show(anim_ctx->to);
    }

    if (anim_ctx->at)
    {
        lv_anim_timeline_delete(anim_ctx->at);
        anim_ctx->at = NULL;
    }

    cos_event_post(COS_EVENT_ACTIVITY_SCREEN_SWITCHED,
                   anim_ctx->to ? anim_ctx->to->view : NULL,
                   anim_ctx->to ? anim_ctx->to->view : NULL);
    cos_free(anim_ctx);

    COS_LOG_I("Anim cleanup end");
}

static void _activity_switch_to(cos_activity_t *next_activity, bool is_returning)
{
    COS_CHECK_PTR_RETURN(next_activity);
    cos_activity_t *cur_activity = _activity_ctx.current_activity;
    COS_LOG_I("Activity switch request: next=%p[%s] returning=%d current=%p[%s] visible=%p[%s] transition=%d",
              (void *)next_activity,
              _activity_type_to_str(next_activity->type),
              is_returning,
              (void *)cur_activity,
              _activity_type_to_str(cur_activity ? cur_activity->type : COS_ACTIVITY_TYPE_NULL),
              (void *)_activity_ctx.visible_activity,
              _activity_type_to_str(_activity_ctx.visible_activity ? _activity_ctx.visible_activity->type
                                                                   : COS_ACTIVITY_TYPE_NULL),
              _activity_ctx.transition_in_progress);
    if (cur_activity == next_activity)
    {
        _activity_mark_visible(next_activity);
        _reset_view_visual_state(next_activity);
        _activity_show(next_activity);
        if (next_activity->is_app_header_visible)
        {
            cos_app_header_show(next_activity);
        }
        else
        {
            cos_app_header_hide();
        }
        return;
    }

    _activity_ctx.previous_activity = cur_activity;
    _activity_ctx.current_activity = next_activity;

    /* Keep target view hidden during lifecycle work to avoid transient one-frame flashes. */
    if (next_activity->view && lv_obj_is_valid(next_activity->view))
    {
        lv_obj_add_flag(next_activity->view, LV_OBJ_FLAG_HIDDEN);
    }

    cos_chrome_manager_handle_activity_switch();

    if (!is_returning && cur_activity && cur_activity->lifecycle.on_pause)
    {
        cur_activity->lifecycle.on_pause(cur_activity);
    }

    if (!next_activity->has_started && next_activity->lifecycle.on_enter)
    {
        next_activity->lifecycle.on_enter(next_activity);
        next_activity->has_started = true;
    }
    else if (is_returning && next_activity->has_started && next_activity->lifecycle.on_resume)
    {
        next_activity->lifecycle.on_resume(next_activity);
    }

    bool header_need_anim = false;
    bool header_reverse_anim = false;
    if (cos_activity_is_app_header_visible(next_activity))
    {
        if (cur_activity)
        {
            if (cos_activity_is_app_header_visible(cur_activity))
            {
                header_need_anim = true;

                if (cur_activity->destroy_on_exit)
                {
                    header_reverse_anim = true;
                }
            }
        }
    }
    else
    {
        if (cur_activity && cur_activity->view && lv_obj_is_valid(cur_activity->view)
            && cos_activity_is_app_header_visible(cur_activity))
        {
            cos_app_header_attach_to_view(cur_activity->view);
        }
        else
        {
            cos_app_header_hide();
        }
    }

    cos_activity_anim_cb_t anim_cb = NULL;
    bool list_anim_available = false;
    if (cur_activity)
    {
        anim_cb = cos_activity_get_anim_route(cur_activity->type, next_activity->type);
        if (!anim_cb)
        {
            list_anim_available =
                cos_list_transition_should_animate(cur_activity, next_activity, cur_activity->destroy_on_exit);
        }
    }

    bool transition_started = false;
    if (cur_activity && (anim_cb || list_anim_available))
    {
        cos_activity_anim_ctx_t *anim_ctx = cos_malloc_zeroed(sizeof(cos_activity_anim_ctx_t));
        if (anim_ctx)
        {
            anim_ctx->at = lv_anim_timeline_create();
            if (!anim_ctx->at)
            {
                cos_free(anim_ctx);
                anim_ctx = NULL;
            }
        }

        if (anim_ctx)
        {
            anim_ctx->from = cur_activity;
            anim_ctx->to = next_activity;
            anim_ctx->destroy_from = cur_activity ? cur_activity->destroy_on_exit : false;

            COS_LOG_I("Activity transition start: from=%p[%s destroy=%d] to=%p[%s destroy=%d] anim_cb=%p list_anim=%d",
                      (void *)cur_activity,
                      _activity_type_to_str(cur_activity ? cur_activity->type : COS_ACTIVITY_TYPE_NULL),
                      cur_activity ? cur_activity->destroy_on_exit : false,
                      (void *)next_activity,
                      _activity_type_to_str(next_activity->type),
                      next_activity->destroy_on_exit,
                      (void *)anim_cb,
                      list_anim_available);

            _activity_ctx.transition_in_progress = true;
            transition_started = true;
            _init_anim_timeline(anim_ctx);
            _activity_ctx.active_anim_ctx = anim_ctx;
            _activity_ctx.snapshot_capture_window = true;
            if (anim_cb)
            {
                anim_cb(anim_ctx->at, cur_activity, next_activity);
            }
            else
            {
                cos_list_transition_play(anim_ctx->at, cur_activity, next_activity, cur_activity->destroy_on_exit);
            }
            _activity_ctx.snapshot_capture_window = false;
            _activity_ctx.active_anim_ctx = NULL;
            if (cur_activity)
            {
                _play_title_changed_anim(cur_activity,
                                         next_activity,
                                         header_need_anim,
                                         header_reverse_anim,
                                         anim_ctx->at);
            }
            _anim_timeline_start(cur_activity, next_activity, anim_ctx);
        }
    }

    if (!transition_started)
    {
        COS_LOG_I("Activity switch no-transition path: cur=%p[%s destroy=%d] next=%p[%s]",
                  (void *)cur_activity,
                  _activity_type_to_str(cur_activity ? cur_activity->type : COS_ACTIVITY_TYPE_NULL),
                  cur_activity ? cur_activity->destroy_on_exit : false,
                  (void *)next_activity,
                  _activity_type_to_str(next_activity->type));
        if (cur_activity && cur_activity->destroy_on_exit)
        {
            if (!cos_activity_is_app_header_visible(next_activity))
            {
                cos_app_header_hide();
            }
            _activity_run_destroy(cur_activity);
        }
        else if (!cos_activity_is_app_header_visible(next_activity) && cur_activity
                 && cos_activity_is_app_header_visible(cur_activity))
        {
            cos_app_header_hide();
        }

        _activity_show(next_activity);
        if (cos_activity_is_app_header_visible(next_activity))
        {
            if (cur_activity)
            {
                _play_title_changed_anim(cur_activity, next_activity, header_need_anim, header_reverse_anim, NULL);
            }
            cos_app_header_show(next_activity);
        }
        _activity_mark_visible(next_activity);
    }
}

static void _activity_on_gesture(lv_event_t *e);

static lv_obj_t *_view_create(lv_obj_t *parent)
{
    if (!parent)
    {
        parent = _activity_ctx.root_screen;
    }

    lv_obj_t *view = lv_obj_create(parent);
    if (!view)
    {
        return NULL;
    }

    lv_obj_remove_style_all(view);
    lv_obj_add_style(view, cos_theme_get_view_style(), 0);
    lv_obj_set_style_radius(view, COS_DISPLAY_RADIUS, 0);
    /* Round-display clip: enables the circular watch-face look (square
     * framebuffer corners hidden behind the black bezel). Keeping
     * LV_OBJ_FLAG_SCROLLABLE is required: in this fork LVGL only generates
     * LV_EVENT_GESTURE when the pressed object is scrollable. The trick is
     * scroll_dir NONE — with no direction, the scroll engine never starts
     * (scroll_obj stays NULL), so `indev_gesture()` doesn't early-return and
     * the indev-level swipe-back still fires. (clip_corner + scrollable dir
     * would make LVGL treat the view as scrollable and swallow the gesture.) */
    lv_obj_set_style_clip_corner(view, true, 0);
    lv_obj_set_scroll_dir(view, LV_DIR_NONE);
    lv_obj_update_layout(view);

    /* Swipe-back escape hatch: a left-swipe anywhere on the view pops the
     * activity, unless the activity's on_swipe_back handler consumed it
     * (e.g. a card pager that pages on horizontal swipe). The root/launcher
     * activity handles its own horizontal gestures, so it is excluded here. */
    lv_obj_add_event_cb(view, _activity_on_gesture, LV_EVENT_GESTURE, NULL);

    return view;
}

static void _activity_on_gesture(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        return;

    /* The launcher (root) already uses left/right swipe for Home <-> Apps
     * paging; never treat that as a back gesture. */
    if (_activity_ctx.current_activity == _activity_ctx.root_activity)
        return;

    /* Don't start a transition while one is already in flight. */
    if (_activity_ctx.transition_in_progress)
        return;

    /* Only act on the fully-visible top activity. */
    if (_activity_ctx.current_activity != _activity_ctx.visible_activity)
        return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    COS_LOG_D("View gesture: dir=%d", (int)dir);
    /* 退出手势 = 【右滑】（用户要求 2026-08-23）：上下滑被 list 垂直滚动占用,
     * 左滑交给 app 自行处理(on_swipe_back 可消费)。root/控制中心除外。 */
    if (dir != LV_DIR_RIGHT)
        return;

    cos_activity_t *a = _activity_ctx.current_activity;
    COS_LOG_D("View swipe-back: firing back, current=%p", (void *)a);
    if (!a)
        return;

    bool consumed = false;
    if (a->lifecycle.on_swipe_back)
        consumed = a->lifecycle.on_swipe_back(a, dir);

    if (!consumed)
        cos_activity_back();
}

/* Indev-level swipe-back. The view-level `_activity_on_gesture` above only
 * fires when the finger lands directly on the (mostly empty) activity view.
 * In practice every app covers its view with a full-screen child (a list, a
 * grid, ...), so the gesture lands on that child, which has no
 * LV_OBJ_FLAG_GESTURE_BUBBLE and never propagates to the view. Listening at
 * the input-device level catches LV_EVENT_GESTURE no matter which child was
 * pressed, which is what actually makes "left-swipe to exit an app" work. */
static void _indev_swipe_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        return;

    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev)
        return;

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    COS_LOG_D("Indev gesture: dir=%d", (int)dir);

    /* Same guards as _activity_on_gesture: ignore on the root/watchface and
     * while a transition is in flight. */
    if (_activity_ctx.current_activity == _activity_ctx.root_activity)
        return;
    if (_activity_ctx.transition_in_progress)
        return;
    if (_activity_ctx.current_activity != _activity_ctx.visible_activity)
        return;

    cos_activity_t *a = _activity_ctx.current_activity;
    if (!a)
        return;

    /* 退出手势 = 【右滑】（用户要求 2026-08-23）：左滑/上下滑一律不再退出，
     * 交给 app 自行处理（pager 翻页等）。watchface 的左滑开 app list / 右滑
     * 开控制中心仍由 home catcher 处理（root guard 已 bail）。 */
    if (dir != LV_DIR_RIGHT)
        return;

    COS_LOG_D("Indev swipe-back: firing back, current=%p", (void *)a);
    bool consumed = false;
    if (a->lifecycle.on_swipe_back)
        consumed = a->lifecycle.on_swipe_back(a, dir);

    if (!consumed)
        cos_activity_back();
}

static void _anim_clean_up_activity(lv_anim_t *a)
{
    if (!a)
        return;

    cos_activity_anim_ctx_t *anim_ctx = (cos_activity_anim_ctx_t *)lv_anim_get_user_data(a);
    if (!anim_ctx)
        return;

    if (anim_ctx->cleanup_scheduled)
    {
        return;
    }

    anim_ctx->cleanup_scheduled = true;

    COS_LOG_I("Anim completed PRE-SET: visible=%p[%s] current=%p[%s] to=%p[%s] from=%p[%s] trans=%d",
              (void *)_activity_ctx.visible_activity,
              _activity_type_to_str(_activity_ctx.visible_activity ? _activity_ctx.visible_activity->type
                                                                   : COS_ACTIVITY_TYPE_NULL),
              (void *)_activity_ctx.current_activity,
              _activity_type_to_str(_activity_ctx.current_activity ? _activity_ctx.current_activity->type
                                                                   : COS_ACTIVITY_TYPE_NULL),
              (void *)anim_ctx->to,
              _activity_type_to_str(anim_ctx->to ? anim_ctx->to->type : COS_ACTIVITY_TYPE_NULL),
              (void *)anim_ctx->from,
              _activity_type_to_str(anim_ctx->from ? anim_ctx->from->type : COS_ACTIVITY_TYPE_NULL),
              _activity_ctx.transition_in_progress);

    _activity_ctx.visible_activity = anim_ctx->to;
    _activity_ctx.transition_in_progress = false;
    COS_LOG_I("Anim completed gate open: visible=%p[%s] trans=false",
              (void *)anim_ctx->to,
              _activity_type_to_str(anim_ctx->to ? anim_ctx->to->type : COS_ACTIVITY_TYPE_NULL));

    cos_dispatcher_call(_anim_clean_up_activity_deferred, anim_ctx);
}

static void _init_anim_timeline(cos_activity_anim_ctx_t *anim_ctx)
{
    if (!anim_ctx)
    {
        return;
    }

    lv_anim_init(&anim_ctx->dummy_anim);
    lv_anim_set_var(&anim_ctx->dummy_anim, lv_screen_active());
    lv_anim_set_exec_cb(&anim_ctx->dummy_anim, _anim_dummy_exec_cb);
    lv_anim_set_values(&anim_ctx->dummy_anim, 0, 100);
    lv_anim_set_delay(&anim_ctx->dummy_anim, 1);
    lv_anim_set_completed_cb(&anim_ctx->dummy_anim, _anim_clean_up_activity);
    lv_anim_set_user_data(&anim_ctx->dummy_anim, anim_ctx);
}

static void _anim_timeline_start(cos_activity_t *from, cos_activity_t *to, cos_activity_anim_ctx_t *anim_ctx)
{
    LV_UNUSED(from);
    LV_UNUSED(to);

    if (!(anim_ctx && anim_ctx->at))
    {
        return;
    }

    uint32_t playtime = lv_anim_timeline_get_playtime(anim_ctx->at);
    if (playtime == 0)
    {
        _anim_clean_up_activity(&anim_ctx->dummy_anim);
        return;
    }

    lv_anim_set_duration(&anim_ctx->dummy_anim, playtime);
    lv_anim_timeline_add(anim_ctx->at, 0, &anim_ctx->dummy_anim);

    lv_anim_timeline_start(anim_ctx->at);
}

void cos_activity_set_type(cos_activity_t *activity, cos_activity_type_t type)
{
    COS_CHECK_PTR_RETURN(activity);
    if (type <= COS_ACTIVITY_TYPE_NULL || type >= COS_ACTIVITY_TYPE_COUNT)
    {
        COS_LOG_W("Invalid activity type: %d", type);
        activity->type = COS_ACTIVITY_TYPE_NULL;
        return;
    }

    activity->type = type;
}

cos_activity_type_t cos_activity_get_type(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, COS_ACTIVITY_TYPE_NULL);
    return activity->type;
}

cos_result_t cos_activity_register_anim_route(cos_activity_type_t from_type,
                                              cos_activity_type_t to_type,
                                              cos_activity_anim_cb_t cb)
{
    if (from_type <= COS_ACTIVITY_TYPE_NULL || from_type >= COS_ACTIVITY_TYPE_COUNT || to_type <= COS_ACTIVITY_TYPE_NULL
        || to_type >= COS_ACTIVITY_TYPE_COUNT || cb == NULL)
    {
        COS_LOG_W("Invalid route register request: from=%d to=%d cb=%p", from_type, to_type, cb);
        return COS_FAILED;
    }

    if (_anim_callback_routes[from_type][to_type] != NULL)
    {
        COS_LOG_W("Animation route duplicated: from=%d to=%d, overwrite", from_type, to_type);
    }

    _anim_callback_routes[from_type][to_type] = cb;
    return COS_OK;
}

cos_activity_anim_cb_t cos_activity_get_anim_route(cos_activity_type_t from_type, cos_activity_type_t to_type)
{
    if (from_type <= COS_ACTIVITY_TYPE_NULL || from_type >= COS_ACTIVITY_TYPE_COUNT || to_type <= COS_ACTIVITY_TYPE_NULL
        || to_type >= COS_ACTIVITY_TYPE_COUNT)
    {
        return NULL;
    }

    return _anim_callback_routes[from_type][to_type];
}

void *cos_activity_get_user_data(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, NULL);
    return activity->user_data;
}

void cos_activity_set_user_data(cos_activity_t *activity, void *user_data)
{
    COS_CHECK_PTR_RETURN(activity);
    activity->user_data = user_data;
}

void cos_activity_set_swipe_back_handler(cos_activity_t *activity, cos_activity_on_swipe_back_t cb)
{
    COS_CHECK_PTR_RETURN(activity);
    activity->lifecycle.on_swipe_back = cb;
}

void cos_activity_set_fault_panel(cos_activity_t *activity, void *fault_panel)
{
    COS_CHECK_PTR_RETURN(activity);
    activity->fault_panel = fault_panel;
}

void *cos_activity_get_fault_panel(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, NULL);
    return activity->fault_panel;
}

lv_obj_t *cos_activity_get_view(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, NULL);
    return activity->view;
}

void cos_activity_set_view(cos_activity_t *activity, lv_obj_t *view)
{
    COS_CHECK_PTR_RETURN(activity);
    if (activity->view && activity->view != view && lv_obj_is_valid(activity->view))
    {
        lv_obj_delete(activity->view);
    }
    activity->view = view;
}

lv_obj_t *cos_activity_get_root_screen(void)
{
    return _activity_ctx.root_screen;
}

lv_obj_t *cos_activity_take_snapshot(cos_activity_t *activity, bool include_header)
{
#if LV_USE_SNAPSHOT
    COS_CHECK_PTR_RETURN_VAL(activity, NULL);

    if (!(_activity_ctx.snapshot_capture_window && _activity_ctx.active_anim_ctx))
    {
        COS_LOG_W("cos_activity_take_snapshot is only allowed in animation callback");
        return NULL;
    }

    lv_obj_t *view = activity->view;
    if (!(view && lv_obj_is_valid(view)))
    {
        return NULL;
    }

    cos_activity_snapshot_node_t *snapshot_node = cos_malloc_zeroed(sizeof(cos_activity_snapshot_node_t));
    if (!snapshot_node)
    {
        return NULL;
    }

    lv_obj_t *snapshot_obj = lv_image_create(cos_overlay_get_snapshot_layer());
    if (!snapshot_obj)
    {
        cos_free(snapshot_node);
        return NULL;
    }

#if _DEBUG_SNAPSHOT
    lv_obj_set_style_image_recolor(snapshot_obj, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_image_recolor_opa(snapshot_obj, LV_OPA_20, 0);
#endif /* _DEBUG_SNAPSHOT */

    lv_draw_buf_t *snapshot = cos_draw_buf_create((uint32_t)lv_obj_get_width(view),
                                                  (uint32_t)lv_obj_get_height(view),
                                                  _SNAPSHOT_COLOR_FORMAT,
                                                  0);
    if (!snapshot)
    {
        lv_obj_delete(snapshot_obj);
        cos_free(snapshot_node);
        return NULL;
    }

    bool prev_header_visible = cos_app_header_is_visible();
    cos_activity_t *prev_visible_activity = cos_activity_get_visible();
    bool need_attach_header = include_header && activity->is_app_header_visible;
    if (need_attach_header)
    {
        cos_app_header_show(activity);
        cos_app_header_attach_to_view(view);
    }

    lv_obj_update_layout(view);
    /* 去掉 lv_refr_now:转场动画帧会在每帧正常刷新,这里强制全屏刷新
     * 1) 加深转场路径栈深度(曾促发 ui 任务栈溢出)
     * 2) 与动画帧叠加渲染,是滑动/转场时颜色残影的嫌疑点 */

    lv_result_t snapshot_result = lv_snapshot_take_to_draw_buf(view, _SNAPSHOT_COLOR_FORMAT, snapshot);

    if (need_attach_header)
    {
        cos_app_header_detach_from_view();

        if (prev_header_visible)
        {
            if (prev_visible_activity)
            {
                cos_app_header_show(prev_visible_activity);
            }
            else
            {
                cos_app_header_show(cos_activity_get_current());
            }
        }
        else
        {
            cos_app_header_hide();
        }
    }

    if (snapshot_result != LV_RESULT_OK)
    {
        cos_draw_buf_destroy(snapshot);
        lv_obj_delete(snapshot_obj);
        cos_free(snapshot_node);
        return NULL;
    }

    snapshot_node->snapshot_obj = snapshot_obj;
    snapshot_node->draw_buf = snapshot;
    snapshot_node->owner = activity;
    snapshot_node->next = _activity_ctx.active_anim_ctx->snapshots;
    _activity_ctx.active_anim_ctx->snapshots = snapshot_node;

    lv_image_set_src(snapshot_obj, snapshot);
    lv_obj_set_size(snapshot_obj, lv_obj_get_width(view), lv_obj_get_height(view));
    lv_obj_set_pos(snapshot_obj, lv_obj_get_x(view), lv_obj_get_y(view));
    lv_obj_add_event_cb(snapshot_obj, _snapshot_img_delete_cb, LV_EVENT_DELETE, snapshot_node);
    _activity_snapshot_hold(activity);
    return snapshot_obj;
#else
    LV_UNUSED(activity);
    LV_UNUSED(include_header);
    COS_LOG_W("LV_USE_SNAPSHOT disabled");
    return NULL;
#endif
}

cos_activity_t *cos_activity_get_watchface(void)
{
    return _activity_ctx.root_activity;
}

cos_result_t cos_activity_replace_root(cos_activity_t *new_root)
{
    COS_CHECK_PTR_RETURN_VAL(new_root, COS_FAILED);

    if (!_controller_initialized())
    {
        COS_LOG_E("Activity controller not initialized");
        return COS_FAILED;
    }

    if (_activity_ctx.transition_in_progress)
    {
        COS_LOG_W("Activity transition in progress");
        return COS_FAILED;
    }

    COS_CHECK_PTR_RETURN_VAL(new_root->view, COS_FAILED);

    // Destroy old root completely
    if (_activity_ctx.root_activity)
    {
        cos_activity_t *old_root = _activity_ctx.root_activity;
        _activity_ctx.root_activity = NULL;
        _activity_run_destroy(old_root);
    }

    // Set new root
    _activity_ctx.root_activity = new_root;

    // Ensure new root's view is parented to root screen
    if (lv_obj_get_parent(new_root->view) != _activity_ctx.root_screen)
    {
        lv_obj_set_parent(new_root->view, _activity_ctx.root_screen);
    }

    _activity_ctx.current_activity = new_root;
    _activity_ctx.visible_activity = new_root;

    // Enter new root (call on_enter) - now cos_view_active() will return correct view
    if (new_root->lifecycle.on_enter)
    {
        new_root->lifecycle.on_enter(new_root);
        new_root->has_started = true;
    }

    // Display new root
    _activity_show(new_root);

    if (new_root->is_app_header_visible)
    {
        cos_app_header_show(new_root);
    }
    else
    {
        cos_app_header_hide();
    }

    return COS_OK;
}

cos_activity_t *cos_activity_get_root(void)
{
    return _activity_ctx.root_activity;
}

const char *cos_activity_get_title(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, NULL);
    if (activity->title.type == _TITLE_TYPE_STRING)
    {
        return activity->title.string;
    }
    else if (activity->title.type == _TITLE_TYPE_ID)
    {
        return cos_lang_get_text(activity->title.id);
    }
    return NULL;
}

void cos_activity_set_title(cos_activity_t *activity, const char *title)
{
    COS_CHECK_PTR_RETURN(activity);
    if (activity->title.type == _TITLE_TYPE_STRING && activity->title.string)
    {
        cos_free(activity->title.string);
        activity->title.string = NULL;
    }
    if (title)
    {
        activity->title.string = (char *)cos_strdup(title);
        activity->title.type = _TITLE_TYPE_STRING;
    }
    else
    {
        activity->title.type = _TITLE_TYPE_INVALID;
    }

    _update_app_header_if_needed(activity);
}

void cos_activity_set_title_id(cos_activity_t *activity, lang_string_id_t id)
{
    COS_CHECK_PTR_RETURN(activity);
    if (activity->title.type == _TITLE_TYPE_STRING && activity->title.string)
    {
        cos_free(activity->title.string);
        activity->title.string = NULL;
    }
    activity->title.id = id;
    activity->title.type = _TITLE_TYPE_ID;

    _update_app_header_if_needed(activity);
}

void cos_activity_set_app_header_visible(cos_activity_t *activity, bool visible)
{
    COS_CHECK_PTR_RETURN(activity);

    if (visible && activity == cos_activity_get_watchface())
    {
        COS_LOG_D("Cannot set app header visible for watchface activity");
        return;
    }

    activity->is_app_header_visible = visible;

    if (visible)
    {
        _update_app_header_if_needed(activity);
    }
    else
    {
        cos_activity_t *current = cos_activity_get_current();
        if (current == activity)
        {
            cos_app_header_hide();
        }
    }
}

void cos_activity_set_app_header_visible_animated(cos_activity_t *activity, bool visible, uint32_t duration_ms)
{
    COS_CHECK_PTR_RETURN(activity);

    if (visible && activity == cos_activity_get_watchface())
    {
        COS_LOG_D("Cannot set app header visible for watchface activity");
        return;
    }

    activity->is_app_header_visible = visible;

    cos_activity_t *current = cos_activity_get_current();
    if (current != activity)
        return;

    if (visible)
    {
        cos_app_header_set_visible_animated(activity, true, duration_ms);
    }
    else
    {
        cos_app_header_set_visible_animated(NULL, false, duration_ms);
    }
}

static void _update_app_header_if_needed(cos_activity_t *activity)
{
    cos_activity_t *visible = cos_activity_get_visible();
    if (visible != activity)
        return;

    if (!activity->is_app_header_visible)
        return;

    cos_app_header_show(activity);
}

bool cos_activity_is_app_header_visible(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, false);
    return activity->is_app_header_visible;
}

void cos_activity_set_app_header_time_only(cos_activity_t *activity, bool time_only)
{
    COS_CHECK_PTR_RETURN(activity);

    activity->is_app_header_time_only = time_only;

    _update_app_header_if_needed(activity);
}

bool cos_activity_is_app_header_time_only(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, false);
    return activity->is_app_header_time_only;
}

void cos_activity_set_app_header_time_only_text_color(cos_activity_t *activity, lv_color_t color)
{
    COS_CHECK_PTR_RETURN(activity);

    activity->app_header_time_only_text_color = color;

    _update_app_header_if_needed(activity);
}

lv_color_t cos_activity_get_app_header_time_only_text_color(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, COS_COLOR_WHITE);
    return activity->app_header_time_only_text_color;
}

lv_color_t cos_activity_get_title_color(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN_VAL(activity, _DEFAULT_TITLE_COLOR);
    return activity->title.color;
}

void cos_activity_set_title_color(cos_activity_t *activity, lv_color_t color)
{
    COS_CHECK_PTR_RETURN(activity);
    activity->title.color = color;

    _update_app_header_if_needed(activity);
}

lv_obj_t *cos_view_active(void)
{
    cos_activity_t *current = cos_activity_get_current();
    if (!current || !current->view || !lv_obj_is_valid(current->view))
    {
        return NULL;
    }
    return current->view;
}

cos_result_t cos_activity_controller_init(cos_activity_t *root_activity)
{
    COS_CHECK_PTR_RETURN_VAL(root_activity, COS_FAILED);

    if (_controller_initialized())
    {
        COS_LOG_E("Activity controller already initialized, cannot reinitialize");
        return COS_FAILED;
    }

    // Lazy initialization: create root_screen and stack if not exist
    // This allows cos_activity_create_root() to be called before controller_init()
    if (!_activity_ctx.root_screen)
    {
        if (lv_screen_active())
        {
            lv_obj_delete(lv_screen_active());
        }
        _activity_ctx.root_screen = lv_obj_create(NULL);
        lv_obj_set_scrollbar_mode(_activity_ctx.root_screen, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(_activity_ctx.root_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_screen_load(_activity_ctx.root_screen);
    }

    if (!_activity_ctx.activity_stack)
    {
        _activity_ctx.activity_stack =
            cos_stack_create_with_mode(_ACTIVITY_STACK_INIT_CAPACITY, COS_STACK_CAPACITY_FIXED);
        if (!_activity_ctx.activity_stack)
        {
            if (_activity_ctx.root_screen)
            {
                lv_obj_delete(_activity_ctx.root_screen);
                _activity_ctx.root_screen = NULL;
            }
            return COS_FAILED;
        }
    }

    // Set root Activity
    _activity_ctx.root_activity = root_activity;

    // Root activity must have view created by cos_activity_create_root()
    if (!root_activity->view)
    {
        COS_LOG_E("controller_init: Root activity has no view (must use cos_activity_create_root)");
        _activity_controller_init_failed(root_activity);
        return COS_FAILED;
    }

    if (root_activity->view)
    {
        if (lv_obj_is_valid(root_activity->view))
        {
            lv_obj_set_parent(root_activity->view, _activity_ctx.root_screen);
        }
        else
        {
            // View is invalid (dangling pointer), reset to NULL so on_enter can recreate
            COS_LOG_W("controller_init: Detected invalid/dangling view pointer");
            root_activity->view = NULL;
        }
    }

    // Enter root Activity (this will call on_enter which creates the view)
    if (root_activity->lifecycle.on_enter)
    {
        root_activity->lifecycle.on_enter(root_activity);
        root_activity->has_started = true;
    }
    else
    {
        COS_LOG_E("controller_init: Root activity has no on_enter callback!");
    }

    // Verify view was created by on_enter
    if (!root_activity->view || !lv_obj_is_valid(root_activity->view))
    {
        COS_LOG_E("Root activity's on_enter failed to create a valid view (view=%p)", (void *)root_activity->view);
        _activity_controller_init_failed(root_activity);
        return COS_FAILED;
    }
    _activity_show(root_activity);
    _activity_ctx.current_activity = root_activity;
    _activity_ctx.visible_activity = root_activity;
    _activity_ctx.transition_in_progress = false;

    if (root_activity->is_app_header_visible)
    {
        cos_app_header_show(root_activity);
    }
    else
    {
        cos_app_header_hide();
    }

    /* Register the global left-swipe-back hook once. It catches gestures that
     * land on an app's full-screen child widgets (which never reach the
     * per-view gesture handler). */
    {
        lv_indev_t *indev = cos_touch_get_indev();
        if (indev)
        {
            lv_indev_add_event_cb(indev, _indev_swipe_back_cb, LV_EVENT_GESTURE, NULL);
        }
    }

    return COS_OK;
}

cos_activity_t *cos_activity_create(const cos_activity_lifecycle_t *lifecycle)
{
    cos_activity_t *activity = cos_malloc_zeroed(sizeof(cos_activity_t));
    if (!activity)
    {
        return NULL;
    }

    if (_activity_ctx.root_screen)
    {
        activity->view = _view_create(_activity_ctx.root_screen);
        if (!activity->view)
        {
            cos_free(activity);
            return NULL;
        }
    }
    else
    {
        activity->view = NULL;
    }
    if (lifecycle)
    {
        activity->lifecycle = *lifecycle;
    }
    else
    {
        memset(&activity->lifecycle, 0, sizeof(activity->lifecycle));
    }
    activity->type = COS_ACTIVITY_TYPE_APP;
    activity->snapshot_ref_count = 0;
    activity->is_app_header_visible = false;
    activity->is_app_header_time_only = false;
    activity->app_header_time_only_text_color = COS_COLOR_WHITE;
    activity->destroy_on_exit = false;
    activity->has_started = false;
    activity->title.color = _DEFAULT_TITLE_COLOR;
    activity->title.type = _TITLE_TYPE_INVALID;
    activity->title.string = NULL;
    activity->user_data = NULL;

    return activity;
}

cos_activity_t *cos_activity_create_root(const cos_activity_lifecycle_t *lifecycle)
{
    // Lazy initialization: auto-create root_screen if not exists
    // This allows create_root() to be called before controller_init()
    if (!_activity_ctx.root_screen)
    {
        if (lv_screen_active())
        {
            lv_obj_delete(lv_screen_active());
        }
        _activity_ctx.root_screen = lv_obj_create(NULL);
        lv_obj_set_scrollbar_mode(_activity_ctx.root_screen, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(_activity_ctx.root_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_screen_load(_activity_ctx.root_screen);
    }

    cos_activity_t *activity = cos_malloc_zeroed(sizeof(cos_activity_t));
    if (!activity)
    {
        return NULL;
    }

    // Always create view immediately with standard style
    activity->view = _view_create(_activity_ctx.root_screen);
    if (!activity->view)
    {
        cos_free(activity);
        return NULL;
    }

    if (lifecycle)
    {
        activity->lifecycle = *lifecycle;
    }
    else
    {
        memset(&activity->lifecycle, 0, sizeof(activity->lifecycle));
    }
    activity->type = COS_ACTIVITY_TYPE_WATCHFACE;
    activity->snapshot_ref_count = 0;
    activity->is_app_header_visible = false;
    activity->is_app_header_time_only = false;
    activity->app_header_time_only_text_color = COS_COLOR_WHITE;
    activity->destroy_on_exit = false;
    activity->has_started = false;
    activity->title.color = _DEFAULT_TITLE_COLOR;
    activity->title.type = _TITLE_TYPE_INVALID;
    activity->title.string = NULL;
    activity->user_data = NULL;

    return activity;
}

void cos_activity_enter(cos_activity_t *activity)
{
    COS_CHECK_PTR_RETURN(activity);
    COS_LOG_I("Activity enter request: activity=%p[%s] current=%p[%s] visible=%p[%s] transition=%d",
              (void *)activity,
              _activity_type_to_str(activity->type),
              (void *)_activity_ctx.current_activity,
              _activity_type_to_str(_activity_ctx.current_activity ? _activity_ctx.current_activity->type
                                                                   : COS_ACTIVITY_TYPE_NULL),
              (void *)_activity_ctx.visible_activity,
              _activity_type_to_str(_activity_ctx.visible_activity ? _activity_ctx.visible_activity->type
                                                                   : COS_ACTIVITY_TYPE_NULL),
              _activity_ctx.transition_in_progress);
    if (!_controller_initialized())
    {
        return;
    }

    if (_activity_ctx.transition_in_progress)
    {
        COS_LOG_W("Activity transition in progress");
        return;
    }

    if (_activity_ctx.current_activity == activity)
    {
        COS_LOG_I("Activity enter is no-op because activity is already current");
        _activity_show(activity);
        return;
    }

    // Prevent entering root Activity through normal enter (use replace_root instead)
    if (activity == _activity_ctx.root_activity)
    {
        COS_LOG_W("Use cos_activity_replace_root() to switch root Activity");
        return;
    }

    if (!cos_stack_push(_activity_ctx.activity_stack, activity))
    {
        return;
    }

    COS_LOG_I("Activity enter pushed to stack: activity=%p[%s] stack_size=%zu",
              (void *)activity,
              _activity_type_to_str(activity->type),
              cos_stack_get_size(_activity_ctx.activity_stack));

    _activity_switch_to(activity, false);
}

cos_result_t cos_activity_back(void)
{
    if (!_controller_initialized())
    {
        return COS_FAILED;
    }

    COS_LOG_I(
        "Activity back request: current=%p[%s] visible=%p[%s] root=%p[%s] transition=%d stack_size=%zu",
        (void *)_activity_ctx.current_activity,
        _activity_type_to_str(_activity_ctx.current_activity ? _activity_ctx.current_activity->type
                                                             : COS_ACTIVITY_TYPE_NULL),
        (void *)_activity_ctx.visible_activity,
        _activity_type_to_str(_activity_ctx.visible_activity ? _activity_ctx.visible_activity->type
                                                             : COS_ACTIVITY_TYPE_NULL),
        (void *)_activity_ctx.root_activity,
        _activity_type_to_str(_activity_ctx.root_activity ? _activity_ctx.root_activity->type : COS_ACTIVITY_TYPE_NULL),
        _activity_ctx.transition_in_progress,
        cos_stack_get_size(_activity_ctx.activity_stack));

    if (_activity_ctx.transition_in_progress)
    {
        COS_LOG_W("Activity transition in progress");
        return COS_FAILED;
    }

    if (_activity_ctx.current_activity != _activity_ctx.visible_activity)
    {
        COS_LOG_W("Activity state mismatch");
        return COS_FAILED;
    }

    // If stack is empty, we're at root - cannot go back further
    if (cos_stack_get_size(_activity_ctx.activity_stack) == 0)
    {
        if (_activity_ctx.current_activity == _activity_ctx.root_activity)
        {
            // Already at root, cannot back
            COS_LOG_I("Activity back no-op: already at root");
            return COS_FAILED;
        }
        // This shouldn't happen, but if current is not root and stack is empty, go to root
        _activity_ctx.current_activity->destroy_on_exit = true;
        COS_LOG_W("Activity back fallback to root: current marked destroy_on_exit");
        _activity_switch_to(_activity_ctx.root_activity, true);
        return COS_OK;
    }

    cos_activity_t *current = cos_stack_pop(_activity_ctx.activity_stack);
    COS_CHECK_PTR_RETURN_VAL(current, COS_FAILED);

    cos_activity_t *cur_activity = _activity_ctx.current_activity;
    if (cur_activity->keep_alive_on_back)
    {
        _activity_move_to_background(cur_activity);
    }
    else
    {
        cur_activity->destroy_on_exit = true;
    }

    cos_activity_t *prev = NULL;
    if (cos_stack_get_size(_activity_ctx.activity_stack) == 0)
    {
        // Stack empty, return to root
        prev = _activity_ctx.root_activity;
    }
    else
    {
        prev = cos_stack_peek(_activity_ctx.activity_stack);
    }

    COS_CHECK_PTR_RETURN_VAL(prev, COS_FAILED);

    COS_LOG_I("Activity back will destroy current after switch: current=%p[%s] prev=%p[%s]",
              (void *)cur_activity,
              _activity_type_to_str(cur_activity ? cur_activity->type : COS_ACTIVITY_TYPE_NULL),
              (void *)prev,
              _activity_type_to_str(prev ? prev->type : COS_ACTIVITY_TYPE_NULL));

    _activity_switch_to(prev, true);

    return COS_OK;
}

cos_result_t cos_activity_back_to_watchface(void)
{
    if (!_controller_initialized())
    {
        return COS_FAILED;
    }

    COS_LOG_I(
        "Back-to-watchface request: current=%p[%s] visible=%p[%s] root=%p[%s] transition=%d stack_size=%zu",
        (void *)_activity_ctx.current_activity,
        _activity_type_to_str(_activity_ctx.current_activity ? _activity_ctx.current_activity->type
                                                             : COS_ACTIVITY_TYPE_NULL),
        (void *)_activity_ctx.visible_activity,
        _activity_type_to_str(_activity_ctx.visible_activity ? _activity_ctx.visible_activity->type
                                                             : COS_ACTIVITY_TYPE_NULL),
        (void *)_activity_ctx.root_activity,
        _activity_type_to_str(_activity_ctx.root_activity ? _activity_ctx.root_activity->type : COS_ACTIVITY_TYPE_NULL),
        _activity_ctx.transition_in_progress,
        cos_stack_get_size(_activity_ctx.activity_stack));

    if (_activity_ctx.transition_in_progress)
    {
        COS_LOG_W("Activity transition in progress");
        return COS_FAILED;
    }

    cos_activity_t *root = _activity_ctx.root_activity;
    cos_activity_t *current = _activity_ctx.current_activity;
    if (!root || !current)
    {
        return COS_FAILED;
    }

    if (current == root)
    {
        // Already at root
        return COS_OK;
    }

    // Destroy stacked activities, but keep current alive until _activity_switch_to()
    // finishes any transition and destroys it through the normal path.
    while (cos_stack_get_size(_activity_ctx.activity_stack) > 0)
    {
        cos_activity_t *activity = cos_stack_pop(_activity_ctx.activity_stack);
        if (!activity)
        {
            continue;
        }

        COS_LOG_I("Back-to-watchface popped stack activity=%p[%s] current=%p[%s]",
                  (void *)activity,
                  _activity_type_to_str(activity->type),
                  (void *)current,
                  _activity_type_to_str(current->type));

        if (activity != current)
        {
            _activity_run_destroy(activity);
        }
    }

    // Current activity: keep-alive apps move to the background (view hidden +
    // on_pause), everything else is marked for destruction.
    if (current->keep_alive_on_back)
    {
        _activity_move_to_background(current);
        COS_LOG_I("Back-to-watchface moved current to background: current=%p[%s]",
                  (void *)current,
                  _activity_type_to_str(current->type));
    }
    else
    {
        current->destroy_on_exit = true;
        COS_LOG_I("Back-to-watchface marked current for destroy: current=%p[%s]",
                  (void *)current,
                  _activity_type_to_str(current->type));
    }

    // Switch to root (will call on_resume)
    _activity_switch_to(root, true);
    return COS_OK;
}

void cos_activity_back_cb(lv_event_t *e)
{
    cos_activity_back();
}

void cos_activity_set_keep_alive_on_back(cos_activity_t *activity, bool keep_alive)
{
    COS_CHECK_PTR_RETURN(activity);
    activity->keep_alive_on_back = keep_alive;
}

bool cos_activity_is_keep_alive_on_back(cos_activity_t *activity)
{
    return activity ? activity->keep_alive_on_back : false;
}

cos_result_t cos_activity_restore_background(cos_activity_t *activity)
{
    if (!_controller_initialized())
    {
        return COS_FAILED;
    }
    if (!activity)
    {
        return COS_FAILED;
    }

    if (_activity_ctx.transition_in_progress)
    {
        COS_LOG_W("Activity restore background: transition in progress");
        return COS_FAILED;
    }

    if (activity == _activity_ctx.current_activity)
    {
        COS_LOG_W("Activity restore background: activity already current");
        return COS_FAILED;
    }

    if (activity == _activity_ctx.root_activity)
    {
        COS_LOG_W("Activity restore background: cannot restore root activity");
        return COS_FAILED;
    }

    if (!cos_stack_push(_activity_ctx.activity_stack, activity))
    {
        return COS_FAILED;
    }

    COS_LOG_I("Activity restore background: activity=%p[%s] stack_size=%zu",
              (void *)activity,
              _activity_type_to_str(activity->type),
              cos_stack_get_size(_activity_ctx.activity_stack));

    /* is_returning=true triggers on_resume() (the app layer resumes its program). */
    _activity_switch_to(activity, true);
    return COS_OK;
}

cos_activity_t *cos_activity_get_current(void)
{
    if (!_controller_initialized())
    {
        return NULL;
    }
    if (_activity_ctx.current_activity)
        return _activity_ctx.current_activity;
    if (_activity_ctx.root_activity)
        return _activity_ctx.root_activity;
    return NULL;
}

cos_activity_t *cos_activity_get_visible(void)
{
    if (!_controller_initialized())
    {
        return NULL;
    }
    if (_activity_ctx.visible_activity)
        return _activity_ctx.visible_activity;
    return cos_activity_get_current();
}

cos_activity_t *cos_activity_get_previous(void)
{
    if (!_controller_initialized())
    {
        return NULL;
    }
    return _activity_ctx.previous_activity;
}

bool cos_activity_is_transition_in_progress(void)
{
    return _activity_ctx.transition_in_progress;
}

cos_activity_t *cos_activity_get_bottom(void)
{
    if (!_controller_initialized())
    {
        return NULL;
    }

    // If stack has items, return bottom of stack
    if (cos_stack_get_size(_activity_ctx.activity_stack) > 0)
    {
        return cos_stack_peek(_activity_ctx.activity_stack);
    }

    // Stack empty, return root activity
    return _activity_ctx.root_activity;
}
