/**
 * @file eos_watchface_builtin.c
 * @brief Built-in fallback watchface implementation
 */

#include "eos_watchface_builtin.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#define EOS_LOG_TAG "WFBuiltin"
#include "eos_log.h"
#include "eos_service_time.h"
#include "eos_theme.h"
#include "eos_mem.h"
#include "eos_watchface_list.h"
#include "eos_msg_list.h"
#include "eos_control_center.h"
#include "eos_cards_page.h"   /* eos_cards_page_show/hide: up-swipe small cards */
#include "eos_swipe_panel.h"  /* eos_swipe_panel_slide_down to open overlay */
#include "eos_chrome_manager.h" /* notify_overlay_opened for right-swipe fallback */
#include "eos_activity.h"
#include "eos_app_list.h"   /* eos_app_list_enter(): open the app list page */
#include "eos_app_header.h" /* eos_app_header_set_back_btn_visible() */

/* Static Variables ------------------------------------------*/

static void _builtin_on_enter(eos_activity_t *activity);
static void _builtin_on_pause(eos_activity_t *activity);
static void _builtin_on_resume(eos_activity_t *activity);
static void _builtin_on_destroy(eos_activity_t *activity);

/**
 * @brief Activity lifecycle callbacks for built-in watchface
 */
static const eos_activity_lifecycle_t _builtin_lifecycle = {
    .on_enter = _builtin_on_enter,
    .on_pause = _builtin_on_pause,
    .on_resume = _builtin_on_resume,
    .on_destroy = _builtin_on_destroy,
};

/* Function Implementations -----------------------------------*/
static void _builtin_time_update_cb(lv_timer_t *timer);
static void _builtin_view_delete_cb(lv_event_t *e);
static void _builtin_long_pressed_cb(lv_event_t *e);
static void _builtin_pressed_cb(lv_event_t *e);
static void _builtin_released_cb(lv_event_t *e);
static void _builtin_gesture_cb(lv_event_t *e);
static void _builtin_swipe_navigate(lv_coord_t dx, lv_coord_t dy);

/* Press anchor for the home-gesture catcher. We deliberately use
 * LV_EVENT_PRESSED / LV_EVENT_RELEASED (the same press-drag path the swipe
 * panels use) instead of LV_EVENT_GESTURE: this fork's gesture events are not
 * reliably delivered to a full-screen catcher, whereas PRESS/RELEASE always
 * fire on a CLICKABLE object. */
static lv_point_t _catcher_press_pt;

eos_watchface_instance_t *eos_watchface_builtin_create(void)
{
    eos_watchface_instance_t *instance = eos_malloc(sizeof(eos_watchface_instance_t));
    if (!instance)
    {
        EOS_LOG_E("Failed to allocate builtin watchface instance");
        return NULL;
    }

    memset(instance, 0, sizeof(*instance));
    instance->type = EOS_WATCHFACE_TYPE_BUILTIN;
    snprintf(instance->id, sizeof(instance->id), "%s", EOS_WATCHFACE_BUILTIN_FALLBACK_ID);
    instance->lifecycle = &_builtin_lifecycle;

    instance->activity = eos_activity_create_root(&_builtin_lifecycle);
    if (!instance->activity)
    {
        EOS_LOG_E("Failed to create activity for builtin watchface");
        eos_free(instance);
        return NULL;
    }

    eos_activity_set_type(instance->activity, EOS_ACTIVITY_TYPE_WATCHFACE);
    eos_activity_set_user_data(instance->activity, instance);

    EOS_LOG_I("Created builtin watchface instance");
    return instance;
}

static void _builtin_on_enter(eos_activity_t *activity)
{
    EOS_LOG_I("Builtin watchface: enter");

    /* Watch face is time-only mode: restore the header back-button master
     * switch so subsequent non-app pages can show it again. */
    eos_app_header_set_back_btn_visible(true);

    eos_watchface_instance_t *self = eos_activity_get_user_data(activity);

    // View is auto-created by framework (eos_activity_create_root + controller_init/replace_root)
    lv_obj_t *view = eos_activity_get_view(activity);
    if (!view)
    {
        EOS_LOG_E("Builtin watchface: view is NULL!");
        return;
    }

    lv_obj_t *title = lv_label_create(view);
    lv_label_set_text(title, "ElenixOS");
    lv_obj_set_style_text_color(title, lv_color_hex(0xD7E2F2), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *time_label = lv_label_create(view);
    lv_label_set_text(time_label, "00:00");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *hint = lv_label_create(view);
    lv_label_set_text(hint, "Hello World!");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x91A4BF), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 36);

    self->data.builtin.time_update_timer = lv_timer_create(_builtin_time_update_cb, 1000, time_label);

    if (self->data.builtin.time_update_timer)
    {
        lv_obj_add_event_cb(view, _builtin_view_delete_cb, LV_EVENT_DELETE, self->data.builtin.time_update_timer);
        lv_timer_ready(self->data.builtin.time_update_timer);
        _builtin_time_update_cb(self->data.builtin.time_update_timer);
    }

    /* Home gesture catcher: a full-screen, transparent, CLICKABLE layer placed
     * on top of the watchface's labels. This is the object that actually
     * receives LEFT / RIGHT / UP gestures and LONG_PRESSED.
     *
     * WHY this is required: the watchface view itself (and its label children)
     * are NOT clickable, so LVGL has no gesture target when the user presses
     * the bare watchface — the gesture event is never generated and every
     * swipe is silently dropped. Making the view clickable would also make it
     * eat taps meant for overlays, so instead we use a dedicated catcher that
     * lives BELOW the overlay-layer strips (control center / msg / cards).
     * Edge swipes still reach those strips; the catcher handles the large
     * central area where the watchface is visible. */
    lv_obj_t *home = lv_obj_create(view);
    lv_obj_remove_style_all(home);
    lv_obj_set_size(home, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(home, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home, 0, 0);
    lv_obj_add_flag(home, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(home, _builtin_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(home, _builtin_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(home, _builtin_long_pressed_cb, LV_EVENT_LONG_PRESSED, NULL);
    /* Gesture callback: LVGL now reliably delivers LV_EVENT_GESTURE to the
     * pressed object (indev_gesture sends it to the act_obj), so swipes work
     * even when the RELEASED point lands on a different object (e.g. the
     * left-edge touch strip at the end of a left swipe). */
    lv_obj_add_event_cb(home, _builtin_gesture_cb, LV_EVENT_GESTURE, NULL);
    self->data.builtin.home_gesture_catcher = home;

    eos_msg_list_show();
    /* Control center starts CLOSED by default so the 'C' hotkey (and the
     * home right-swipe) is a genuine "one-key open". Previously it was
     * auto-shown on enter, so the first 'C' press only closed it and felt
     * like "no response". */
    eos_control_center_hide();
    eos_cards_page_show();
}

static void _builtin_on_pause(eos_activity_t *activity)
{
    EOS_LOG_I("Builtin watchface: pause");

    eos_watchface_instance_t *self = eos_activity_get_user_data(activity);

    if (self->data.builtin.time_update_timer)
    {
        lv_timer_pause(self->data.builtin.time_update_timer);
    }

    eos_control_center_hide();
    eos_msg_list_hide();
    eos_cards_page_hide();
}

static void _builtin_on_resume(eos_activity_t *activity)
{
    EOS_LOG_I("Builtin watchface: resume");

    eos_watchface_instance_t *self = eos_activity_get_user_data(activity);

    if (self->data.builtin.time_update_timer)
    {
        lv_timer_resume(self->data.builtin.time_update_timer);
    }

    eos_msg_list_show();
    /* Keep the control center CLOSED when returning to the watchface, so the
     * 'C' hotkey remains a consistent one-key open (see _builtin_on_enter). */
    eos_control_center_hide();
    eos_cards_page_show();
}

static void _builtin_on_destroy(eos_activity_t *activity)
{
    EOS_LOG_I("Builtin watchface: destroy");
}

static void _builtin_time_update_cb(lv_timer_t *timer)
{
    lv_obj_t *time_label = lv_timer_get_user_data(timer);
    if (!time_label || !lv_obj_is_valid(time_label))
    {
        return;
    }

    eos_datetime_t now = eos_time_get();
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.min);
    lv_label_set_text(time_label, buf);
}

static void _builtin_view_delete_cb(lv_event_t *e)
{
    lv_timer_t *timer = lv_event_get_user_data(e);
    if (timer)
    {
        lv_timer_delete(timer);
    }
}

static void _builtin_long_pressed_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    /* A long press must be STATIONARY.
     *
     * LVGL raises LV_EVENT_LONG_PRESSED purely on elapsed press time
     * (LV_INDEV_DEF_LONG_PRESS_TIME, 400 ms by default) and does NOT cancel it
     * when the finger travels. Without the movement check below, ANY deliberate
     * swipe across the 240 px screen that takes longer than 400 ms opened the
     * watchface list mid-gesture. The watchface list then became the current
     * activity and swallowed the rest of the gesture, which produced exactly
     * these intermittent symptoms:
     *   - slow right-swipe never opened the control center (the list ate it);
     *   - slow left-swipe landed on the watchface list instead of the app list,
     *     and that page does not exit on a right-swipe -> "can't get out";
     *   - fast swipes (< 400 ms) worked, so the failure looked random.
     * Requiring the finger to still be within a small radius of the press point
     * keeps the real long-press (press and hold to change watchface) working
     * while making swipes immune. */
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_coord_t dx = p.x - _catcher_press_pt.x;
    lv_coord_t dy = p.y - _catcher_press_pt.y;
    if (LV_ABS(dx) > 15 || LV_ABS(dy) > 15)
    {
        EOS_LOG_D("home catcher long-press IGNORED (moved dx=%d dy=%d -> this is a swipe)",
                  dx, dy);
        return;
    }

    EOS_LOG_D("home catcher long-press accepted (dx=%d dy=%d) -> watchface list", dx, dy);
    eos_watchface_list_enter();
}

static void _builtin_swipe_navigate(lv_coord_t dx, lv_coord_t dy)
{
    /* Ignore sub-threshold movement (a tap, not a swipe). */
    if (LV_ABS(dx) < 30 && LV_ABS(dy) < 30)
        return;

    if (LV_ABS(dx) > LV_ABS(dy))
    {
        if (dx < 0)
        {
            /* Left-swipe opens the app-list page. */
            eos_app_list_enter();
        }
        else
        {
            /* Right-swipe opens the control center (panel slides in from the
             * LEFT edge, Redmi-Watch style). */
            eos_control_center_t *cc = eos_control_center_get_instance();
            if (cc && cc->swipe_panel)
                eos_swipe_panel_slide_down(cc->swipe_panel);
        }
    }
    else
    {
        if (dy < 0)
        {
            /* Up-swipe reveals the small-cards (smart-stack) page. */
            eos_cards_page_slide_up();
        }
        /* Down-swipe on the home screen: nothing to close here. */
    }
}

static void _builtin_pressed_cb(lv_event_t *e)
{
    lv_indev_get_point(lv_indev_active(), &_catcher_press_pt);
}

static void _builtin_gesture_cb(lv_event_t *e)
{
    /* Only the watch face (root activity) owns the home swipe navigation.
     * On app pages the App Manager / activity framework handle gestures. */
    if (eos_activity_get_current() != eos_activity_get_root())
        return;
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev)
        return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)
    {
        _builtin_swipe_navigate((dir == LV_DIR_LEFT) ? -240 : 240, 0);
    }
}

static void _builtin_released_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_coord_t dx = p.x - _catcher_press_pt.x;
    lv_coord_t dy = p.y - _catcher_press_pt.y;
    EOS_LOG_D("home catcher release: dx=%d dy=%d (right-swipe->control center, left-swipe->app list)",
              dx, dy);
    _builtin_swipe_navigate(dx, dy);
}

void eos_watchface_builtin_test_swipe(lv_coord_t dx, lv_coord_t dy)
{
    _builtin_swipe_navigate(dx, dy);
}
