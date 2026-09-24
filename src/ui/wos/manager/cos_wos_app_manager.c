/**
 * @file cos_wos_app_manager.c
 * @brief WOS App Manager implementation
 */
#include "cos_wos_app_manager.h"

#include "cos_wos_transition.h"
#include "cos_overlay_layer.h"
#include "cos_touch.h"
#include "cos_log.h"
#include "cos_mem.h"
#include <string.h>

#define COS_LOG_TAG "WosMgr"

/* Registry --------------------------------------------------*/
static const cos_wos_app_desc_t *s_registry[WOS_MAX_REGISTERED_APPS];
static int s_reg_count = 0;

/* Active app -------------------------------------------------*/
static cos_wos_app_t *s_active = NULL;
static wos_app_state_t s_state = WOS_APP_STATE_IDLE;
static bool s_inited = false;

static void _cleanup_app(cos_wos_app_t *app);

/* ---- internal helpers ---- */

static const cos_wos_app_desc_t *_find(const char *id)
{
    int i;
    for (i = 0; i < s_reg_count; i++)
    {
        if (s_registry[i] && s_registry[i]->id && strcmp(s_registry[i]->id, id) == 0)
            return s_registry[i];
    }
    return NULL;
}

/* 上下滑关闭活动的 WOS app（用户要求 2026-08-17：退出手势由左滑改为上下滑，
 * 与 activity framework 的 _indev_swipe_back_cb 一致）。注册一次。 */
static void _indev_swipe_close_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE)
        return;
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev)
        return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_TOP && dir != LV_DIR_BOTTOM)
        return;
    if (s_active)
        wos_app_manager_close();
}

/* Deferred teardown after the close animation (150ms) finishes. */
static void _close_deferred_cb(lv_timer_t *tm)
{
    COS_LOG_I("wos: deferred cleanup fired (tick=%u)", (unsigned)lv_tick_get());
    if (tm)
        lv_timer_delete(tm);
    if (s_active)
    {
        cos_wos_app_t *app = s_active;
        s_active = NULL;
        _cleanup_app(app);
    }
    s_state = WOS_APP_STATE_IDLE;
    COS_LOG_I("wos: app closed (state=IDLE)");
}

static void _cleanup_app(cos_wos_app_t *app)
{
    if (!app)
        return;
    /* 1. kill ledger timers */
    uint8_t i;
    for (i = 0; i < app->timer_cnt; i++)
    {
        if (app->timers[i])
            lv_timer_delete(app->timers[i]);
    }
    app->timer_cnt = 0;
    /* 2. delete all UI children + the root itself */
    if (app->root && lv_obj_is_valid(app->root))
    {
        lv_obj_clean(app->root);      /* delete every child object */
        lv_obj_delete(app->root);     /* delete the root container  */
    }
    app->root = NULL;
    /* 3. app-owned destroy callback (JS stop, free user_data, ...) */
    if (app->desc && app->desc->destroy)
        app->desc->destroy(app);
    /* 4. cleanup audit — nothing may remain */
    if (app->root != NULL || app->timer_cnt != 0)
    {
        COS_LOG_E("wos: LEAK in app '%s' (root/timer audit failed)",
                  app->desc ? app->desc->id : "?");
    }
    cos_free(app);
}

/* ---- public API ---- */

void wos_app_manager_init(void)
{
    if (s_inited)
        return;
    s_inited = true;
    s_reg_count = 0;
    s_active = NULL;
    s_state = WOS_APP_STATE_IDLE;

    /* Left-swipe closes the active WOS app. */
    lv_indev_t *indev = cos_touch_get_indev();
    if (indev)
    {
        lv_indev_add_event_cb(indev, _indev_swipe_close_cb, LV_EVENT_GESTURE, NULL);
    }

    COS_LOG_I("wos: App Manager initialized (registry=%d)", WOS_MAX_REGISTERED_APPS);
}

bool wos_app_manager_register(const cos_wos_app_desc_t *desc)
{
    if (!desc || !desc->id || s_reg_count >= WOS_MAX_REGISTERED_APPS)
        return false;
    if (_find(desc->id))
        return true; /* already registered — idempotent */
    s_registry[s_reg_count++] = desc;
    COS_LOG_I("wos: registered app '%s' (%s)", desc->id, desc->name ? desc->name : "");
    return true;
}

bool wos_app_manager_open(const char *id)
{
    if (!s_inited)
        wos_app_manager_init();
    if (s_state == WOS_APP_STATE_LAUNCHING || s_state == WOS_APP_STATE_CLOSING)
    {
        COS_LOG_W("wos: busy (state=%d), ignoring open('%s')", s_state, id ? id : "?");
        return false;
    }
    const cos_wos_app_desc_t *desc = _find(id);
    if (!desc)
    {
        COS_LOG_E("wos: unknown app id '%s'", id ? id : "?");
        return false;
    }
    /* force-close the current app before opening a new one.
     * Synchronous teardown: the open animation of the new page covers the
     * switch visually, so we skip the shrink of the old page here. */
    if (s_active)
    {
        cos_wos_app_t *old = s_active;
        s_active = NULL;
        _cleanup_app(old);
        s_state = WOS_APP_STATE_IDLE;
    }

    cos_wos_app_t *app = cos_malloc(sizeof(cos_wos_app_t));
    if (!app)
        return false;
    memset(app, 0, sizeof(*app));
    app->desc = desc;

    /* app root: full-screen container on the dedicated app layer.
     * NOTE: not round-clipped here — transform anims break under clip_corner.
     * The root KEEPS LV_OBJ_FLAG_SCROLLABLE (horizontal only): LVGL only
     * generates LV_EVENT_GESTURE when the pressed object is scrollable, and
     * the App Manager's swipe-back gesture depends on it. Content never
     * overflows the screen, so no actual scrolling is visible. */
    lv_obj_t *layer = cos_overlay_get_app_layer();
    lv_obj_t *root = lv_obj_create(layer);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_scroll_dir(root, LV_DIR_HOR);
    app->root = root;

    s_active = app;
    s_state = WOS_APP_STATE_LAUNCHING;

    if (desc->build)
        desc->build(app);

    wos_transition_open(root);
    s_state = WOS_APP_STATE_ACTIVE;
    COS_LOG_I("wos: opened app '%s'", desc->id);
    return true;
}

void wos_app_manager_close(void)
{
    if (!s_active)
        return;
    if (s_state != WOS_APP_STATE_ACTIVE && s_state != WOS_APP_STATE_LAUNCHING)
        return;

    cos_wos_app_t *app = s_active;
    lv_obj_t *root = app->root;
    s_state = WOS_APP_STATE_CLOSING;
    COS_LOG_I("wos: closing app '%s' (shrink+fade)", app->desc->id);
    wos_transition_close(root, NULL);
    /* Defer teardown until the shrink+fade (150ms) has been visible. */
    lv_timer_t *t = lv_timer_create(_close_deferred_cb, 200, NULL);
    lv_timer_set_repeat_count(t, 1);
}

cos_wos_app_t *wos_app_manager_get_active(void)
{
    return s_active;
}

wos_app_state_t wos_app_manager_get_state(void)
{
    return s_state;
}

const char *wos_app_manager_active_id(void)
{
    return (s_active && s_active->desc) ? s_active->desc->id : "";
}

int wos_app_manager_registered_count(void)
{
    return s_reg_count;
}

const cos_wos_app_desc_t *wos_app_manager_registered(int i)
{
    if (i < 0 || i >= s_reg_count)
        return NULL;
    return s_registry[i];
}
