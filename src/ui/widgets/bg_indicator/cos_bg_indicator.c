/**
 * @file cos_bg_indicator.c
 * @brief Background App Indicator implementation
 */
#include "cos_bg_indicator.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "cos_log.h"
#include "cos_mem.h"
#include "cos_overlay_layer.h"
#include "cos_service_storage.h"
#include "cos_image_resuorces.h"

#define COS_LOG_TAG "BgIndicator"

/* Entry for one background-running app -----------------------*/
typedef struct cos_bg_indicator_entry
{
    char app_id[64];
    char icon_src[COS_FS_PATH_MAX];
    cos_activity_t *activity;
    bool running;
    struct cos_bg_indicator_entry *next;
} cos_bg_indicator_entry_t;

/* Static variables -------------------------------------------*/
static cos_bg_indicator_entry_t *_s_list = NULL;
static lv_obj_t *_s_icon_container = NULL;
static uint32_t _s_count = 0;
static bool _s_initialized = false;

/* Helpers ----------------------------------------------------*/
static void _rebuild_icons(void);

static void _icon_clicked_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    lv_obj_t *icon = lv_event_get_target(e);
    cos_bg_indicator_entry_t *entry = (cos_bg_indicator_entry_t *)lv_obj_get_user_data(icon);
    if (!entry)
        return;

    COS_LOG_I("BgIndicator icon clicked: app_id=%s", entry->app_id);

    cos_activity_t *activity = entry->activity;

    /* Remove from background list (this deletes the icon; LVGL defers the
     * actual object deletion, so deleting the clicked target here is safe). */
    cos_bg_indicator_unregister(entry->app_id);

    /* Bring the app back to the foreground. */
    if (activity)
        cos_activity_restore_background(activity);
}

static void _rebuild_icons(void)
{
    if (!_s_icon_container || !lv_obj_is_valid(_s_icon_container))
        return;

    /* Remove all existing icon children. */
    uint32_t child_count = lv_obj_get_child_count(_s_icon_container);
    for (uint32_t i = 0; i < child_count; i++)
    {
        lv_obj_t *child = lv_obj_get_child(_s_icon_container, 0);
        if (child)
            lv_obj_delete(child);
    }

    /* Re-add one icon per background entry, left-to-right. */
    cos_bg_indicator_entry_t *entry = _s_list;
    while (entry)
    {
        if (entry->running)
        {
            /* 圆形裁剪：fork 的 lv_image 自身 clip_corner 失效（方形）
             * → 用圆形 wrap 容器（obj radius+clip_corner）裁掉 image 四角 */
            lv_obj_t *wrap = lv_obj_create(_s_icon_container);
            lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_size(wrap, COS_BG_INDICATOR_ICON_SIZE, COS_BG_INDICATOR_ICON_SIZE);
            lv_obj_set_style_radius(wrap, COS_BG_INDICATOR_ICON_SIZE / 2, 0);
            lv_obj_set_style_clip_corner(wrap, true, 0);
            lv_obj_set_style_bg_opa(wrap, 0, 0);
            lv_obj_set_style_pad_all(wrap, 0, 0);
            lv_obj_set_style_border_width(wrap, 0, 0);
            lv_obj_add_flag(wrap, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(wrap, entry);
            lv_obj_add_event_cb(wrap, _icon_clicked_cb, LV_EVENT_CLICKED, NULL);

            lv_obj_t *icon = lv_image_create(wrap);
            lv_obj_set_size(icon, COS_BG_INDICATOR_ICON_SIZE, COS_BG_INDICATOR_ICON_SIZE);
            lv_obj_center(icon);

            /* Load the app icon image (icon.bin is a PNG). */
            if (entry->icon_src[0] && cos_storage_is_file(entry->icon_src))
            {
                lv_image_set_src(icon, entry->icon_src);
            }
            else
            {
                /* Fallback: default app icon. */
                lv_image_set_src(icon, COS_IMG_APP);
            }
        }
        entry = entry->next;
    }
}

/* Function Implementations -----------------------------------*/
void cos_bg_indicator_init(void)
{
    if (_s_initialized)
        return;

    lv_obj_t *layer = cos_overlay_get_indicator_layer();
    if (!layer)
    {
        COS_LOG_E("BgIndicator init: indicator layer not created yet");
        return;
    }

    _s_icon_container = lv_obj_create(layer);
    lv_obj_remove_style_all(_s_icon_container);
    lv_obj_set_size(_s_icon_container, LV_SIZE_CONTENT, COS_BG_INDICATOR_ICON_SIZE);
    lv_obj_set_style_bg_opa(_s_icon_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_s_icon_container, 0, 0);
    lv_obj_set_style_pad_all(_s_icon_container, 0, 0);
    lv_obj_set_style_pad_column(_s_icon_container, 4, 0);
    lv_obj_remove_flag(_s_icon_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_s_icon_container, LV_FLEX_FLOW_ROW);
    lv_obj_align(_s_icon_container, LV_ALIGN_TOP_MID, 0, 12);

    _s_initialized = true;
    COS_LOG_I("BgIndicator initialized");
}

cos_result_t cos_bg_indicator_register(const char *app_id,
                                       const char *icon_src,
                                       cos_activity_t *activity)
{
    if (!app_id || !app_id[0])
        return COS_FAILED;

    /* Update existing entry if already registered. */
    cos_bg_indicator_entry_t *entry = _s_list;
    while (entry)
    {
        if (strcmp(entry->app_id, app_id) == 0)
        {
            entry->activity = activity;
            entry->running = true;
            if (icon_src && icon_src[0])
                snprintf(entry->icon_src, sizeof(entry->icon_src), "%s", icon_src);
            _rebuild_icons();
            return COS_OK;
        }
        entry = entry->next;
    }

    if (_s_count >= COS_BG_INDICATOR_MAX_APPS)
    {
        COS_LOG_W("BgIndicator register: max background apps reached (%d)",
                  COS_BG_INDICATOR_MAX_APPS);
        return COS_FAILED;
    }

    entry = cos_malloc_zeroed(sizeof(cos_bg_indicator_entry_t));
    if (!entry)
        return COS_FAILED;

    snprintf(entry->app_id, sizeof(entry->app_id), "%s", app_id);
    if (icon_src && icon_src[0])
        snprintf(entry->icon_src, sizeof(entry->icon_src), "%s", icon_src);
    entry->activity = activity;
    entry->running = true;
    entry->next = _s_list;
    _s_list = entry;
    _s_count++;

    COS_LOG_I("BgIndicator register: app_id=%s icon=%s (count=%u)",
              entry->app_id, entry->icon_src, _s_count);

    _rebuild_icons();
    return COS_OK;
}

cos_result_t cos_bg_indicator_unregister(const char *app_id)
{
    if (!app_id || !app_id[0])
        return COS_FAILED;

    cos_bg_indicator_entry_t *prev = NULL;
    cos_bg_indicator_entry_t *entry = _s_list;
    while (entry)
    {
        if (strcmp(entry->app_id, app_id) == 0)
        {
            if (prev)
                prev->next = entry->next;
            else
                _s_list = entry->next;

            COS_LOG_I("BgIndicator unregister: app_id=%s (count=%u)",
                      entry->app_id, _s_count > 0 ? _s_count - 1 : 0);

            cos_free(entry);
            _s_count--;

            _rebuild_icons();
            return COS_OK;
        }
        prev = entry;
        entry = entry->next;
    }

    return COS_FAILED;
}

bool cos_bg_indicator_has_any(void)
{
    return _s_count > 0;
}

uint32_t cos_bg_indicator_count(void)
{
    return _s_count;
}

void cos_bg_indicator_set_visible(bool visible)
{
    if (!_s_icon_container || !lv_obj_is_valid(_s_icon_container))
        return;
    if (visible)
        lv_obj_remove_flag(_s_icon_container, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(_s_icon_container, LV_OBJ_FLAG_HIDDEN);
}
