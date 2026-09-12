/**
 * @file eos_app_list.c
 * @brief App list page - using bubble_grid layout
 */

#include "eos_config.h"
#include "eos_app_list.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#define EOS_LOG_TAG "AppList"
#include "eos_log.h"
#include "eos_app.h"
#include "eos_basic_widgets.h"
#include "eos_pkg_mgr.h"
#include "eos_image.h"
#include "eos_port.h"
#include "eos_anim.h"
#include "spm.h"
#include "eos_service_config.h"
#include "eos_event.h"
#include "eos_lang.h"
#include "eos_settings.h"
#include "eos_flash_light.h"
#include "eos_album.h"
#include "eos_texthub.h"
#include "eos_dictionary.h"
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
#include "eos_usb_msc.h"
/* App 图标编译进 Flash(resources/images/icon/eos_icon_usb_msc.c) */
extern const lv_image_dsc_t eos_icon_usb_msc;
#endif
#include "eos_service_storage.h"
#include "eos_app_header.h"
#include "eos_mem.h"
#include "eos_crown.h"
#include "eos_theme.h"
#include "eos_icon.h"
#include "eos_font.h"
#include "eos_std_widgets.h"
#include "eos_activity.h"
#include "eos_bubble_grid.h"
#include "eos_card_pager.h"
#include "eos_accordion.h"
#include "eos_overlay_layer.h"
#include "eos_bg_indicator.h"
#include "ui/system/eos_round_clip.h"
#if EOS_ENABLE_TEST_APP
#include "eos_test.h"
#endif
/* When the simulator builds the adaptive framework Launcher (EOS_USE_CUSTOM_
 * LAUNCHER, defined in the simulator platform config), route the app-list page
 * to it instead of the default bubble_grid. The watchface stays the home. */
#ifdef EOS_USE_CUSTOM_LAUNCHER
#include "ui/launcher/eos_launcher.h"
#endif
/* Macros and Definitions -------------------------------------*/
#define _APP_ICON_ANIM_DURATION 200
#define _APP_ICON_ANIM_DELAY 75

#define _APP_LIST_ANIM_DURATION 240
#define _APP_LIST_ANIM_FOCUS_SCALE 512
#define _APP_LIST_ANIM_MIN_SACLE 128
#define _APP_LIST_ANIM_SPLIT_PCT 20
#define _APP_LIST_ANIM_FROM_OPA_START 255
#define _APP_LIST_ANIM_FROM_OPA_END 0
#define _APP_LIST_ANIM_TO_OPA_START 0
#define _APP_LIST_ANIM_TO_OPA_END 255
/* Variables --------------------------------------------------*/

const char *eos_sys_app_id_list[EOS_SYS_APP_LAST] = {"sys.settings",
                                                     "sys.flash_light",
#if EOS_ENABLE_TEST_APP
                                                     "sys.test"
#endif
};

const char *eos_sys_app_icon_list[EOS_SYS_APP_LAST] = {EOS_IMG_SETTINGS,
                                                       EOS_IMG_FLASH_LIGHT,
#if EOS_ENABLE_TEST_APP
                                                       EOS_IMG_APP
#endif
};

const eos_sys_app_entry_t eos_sys_app_entry_list[EOS_SYS_APP_LAST] = {eos_settings_enter,
                                                                      eos_flash_light_enter,
#if EOS_ENABLE_TEST_APP
                                                                      eos_test_start
#endif
};

/* Native C apps: registered with a real app id (like plugins), shown in the
 * App page, launched via a C entry function. */
const char *eos_native_app_id_list[EOS_NATIVE_APP_LAST] = {
    "com.cantomk6.album",
    "com.cantomk6.texthub",
    "com.cantomk6.dictionary",
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    "com.cantomk6.usb_msc",
#endif
};

const char *eos_native_app_icon_list[EOS_NATIVE_APP_LAST] = {
    EOS_IMG_ALBUM,
    EOS_IMG_TEXTHUB,
    EOS_IMG_DICTIONARY,
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    EOS_IMG_USB_MSC,
#endif
};

const eos_sys_app_entry_t eos_native_app_entry_list[EOS_NATIVE_APP_LAST] = {
    eos_album_enter,
    eos_texthub_enter,
    eos_dictionary_enter,
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    eos_usb_msc_enter,
#endif
};

static void _app_list_on_resueme(eos_activity_t *a);

static void _app_on_enter(eos_activity_t *a);
static eos_activity_lifecycle_t app_list_lifecycle = {
    .on_enter = NULL,
    .on_destroy = NULL,
    .on_pause = NULL,
    .on_resume = _app_list_on_resueme,
};

static void _app_on_destroy(eos_activity_t *a);
static void _app_on_pause(eos_activity_t *a);
static void _app_on_resume(eos_activity_t *a);

static eos_activity_lifecycle_t app_lifecycle = {
    .on_enter = _app_on_enter,
    .on_destroy = _app_on_destroy,
    .on_pause = _app_on_pause,
    .on_resume = _app_on_resume,
};

typedef struct
{
    script_pkg_t pkg;
    char *app_id;
    char icon_src[EOS_FS_PATH_MAX];
    bool background;
} app_launch_ctx_t;

/* Paged app grid: each page shows 6 icons; swipe left/right flips between
 * pages. Up to 4 pages (24 apps) are supported.
 * Layout is a true honeycomb: the 6 icons per page are staggered as 2-3-1
 * (top row 2 icons nested between the middle row's 3 icons, bottom row 1
 * centered). Row gap is kept smaller than the column gap so neighbours nest
 * vertically like a hex grid; every row is centered on the round 240x240
 * screen and never exceeds the circle clipping. */
#define _APP_GRID_PAGE_SIZE     7
#define _APP_GRID_COL_COUNT     3
#define _APP_GRID_ROW_COUNT     3
#define _APP_GRID_MAX_ICONS     24
#define _APP_GRID_ICON_SIZE     50
#define _APP_GRID_COL_GAP       58
#define _APP_GRID_ROW_GAP       50

/* Pager arrow buttons (RemixIcon glyphs, see eos_icon.h / RemixIcon.c). */
#define _APP_ARROW_SIZE         30
#define _APP_ARROW_PAD_X        6

typedef struct
{
    uint32_t index;
    char *app_id;
} _app_grid_icon_ctx_t;

static eos_card_pager_t *_app_list_pager = NULL;
static lv_obj_t *_app_icon_cache[_APP_GRID_MAX_ICONS];
static lv_obj_t *_app_arrow_prev = NULL;
static lv_obj_t *_app_arrow_next = NULL;

/* Function Implementations -----------------------------------*/
static void _app_grid_icon_clicked_cb(lv_event_t *e);
static lv_obj_t *_app_list_find_icon_by_index(uint32_t index);
static void _app_installed_cb(eos_event_t *e);
static void _app_uninstalled_cb(eos_event_t *e);
static void _container_delete_cb(lv_event_t *e);
static void _app_list_refresh(lv_obj_t *bubble_grid);
static void _app_list_open_app_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to);
static void _app_list_close_app_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to);
static void _register_anim_routes_once(void);
static const char *_app_list_get_launch_app_id(eos_activity_t *activity);
static void _app_list_set_last_launch_app_id(const char *app_id);
static lv_obj_t *_app_list_get_bubble_grid(eos_activity_t *activity);
static void _app_list_record_icon_center_point(int32_t x, int32_t y);
static bool _app_list_calc_focus_pivot(lv_obj_t *snapshot_obj, lv_obj_t *icon_obj, int32_t *pivot_x, int32_t *pivot_y);
static bool _app_list_calc_focus_pivot_by_global_center(lv_obj_t *obj, int32_t *pivot_x, int32_t *pivot_y);
static void _app_list_set_transform_scale_cb(void *var, int32_t value);
static void _app_list_set_image_scale_cb(void *var, int32_t value);
static void _app_list_set_translate_x_cb(void *var, int32_t value);
static void _app_list_set_translate_y_cb(void *var, int32_t value);
static void _app_list_set_opa_cb(void *var, int32_t value);
static void _app_list_init_scale_anim(lv_anim_t *anim, lv_obj_t *obj, int32_t start, int32_t end, uint32_t duration);
static void _app_list_init_image_scale_anim(lv_anim_t *anim,
                                            lv_obj_t *obj,
                                            int32_t start,
                                            int32_t end,
                                            uint32_t duration);
static void _app_list_init_translate_x_anim(lv_anim_t *anim,
                                            lv_obj_t *obj,
                                            int32_t start,
                                            int32_t end,
                                            uint32_t duration);
static void _app_list_init_translate_y_anim(lv_anim_t *anim,
                                            lv_obj_t *obj,
                                            int32_t start,
                                            int32_t end,
                                            uint32_t duration);
static void _app_list_init_opa_anim(lv_anim_t *anim, lv_obj_t *obj, int32_t start, int32_t end, uint32_t duration);
static void _app_list_play_transition_anim(lv_anim_timeline_t *at,
                                           eos_activity_t *from,
                                           eos_activity_t *to,
                                           bool opening);
static void _app_list_cleanup_extra_cb(lv_event_t *e);
static int32_t _app_list_find_sys_app(const char *app_id);
static eos_result_t _app_list_build_script_pkg(const char *app_id, script_pkg_t *pkg);
static eos_result_t _app_list_launch_script_app(const char *app_id);

static bool _anim_routes_registered = false;
static bool _app_list_last_icon_center_valid = false;
static int32_t _app_list_last_icon_center_x = 0;
static int32_t _app_list_last_icon_center_y = 0;
static int32_t _app_list_last_click_index = -1;
static char _app_list_last_launch_app_id[64] = {0};
static uint32_t _app_list_icon_count = 0;

/************************** Lifecycle **************************/

static void _app_on_destroy(eos_activity_t *a)
{
    app_launch_ctx_t *ctx = eos_activity_get_user_data(a);

    /* If this was a background app, drop its indicator icon first. */
    if (ctx && ctx->background)
    {
        eos_bg_indicator_unregister(ctx->app_id);
    }

    // Stop the app script (if any) before destroying context
    spm_app_stop();

    if (ctx)
    {
        eos_pkg_free(&ctx->pkg);
        eos_free(ctx->app_id);
        eos_free(ctx);
        eos_activity_set_user_data(a, NULL);
    }
}

/* JS 可写 eos.config "app.background"（bool）声明"是否应作为后台运行"。
 * true → 退出时挂后台指示器（计时中退出有标）；false/缺省 → 静默后台（不挂）。
 * 缺省按 false（保守）：config 读不到/解析失败 → 不挂标，保证"重置后无标"优先。
 * config 是扁平 JSON：{"app.background": true, ...}，key 含点号是字面对象键。 */
static bool _js_app_wants_background(const char *app_id)
{
    char path[EOS_FS_PATH_MAX];
    snprintf(path, sizeof(path), EOS_APP_DATA_DIR "%s/config.json", app_id);
    char *data = eos_storage_read_file(path);
    if (!data)
        return false; /* 无 config → 保守：不挂后台标 */
    cJSON *root = cJSON_Parse(data);
    eos_free(data);
    if (!root)
        return false;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "app.background");
    bool bg = (item && cJSON_IsBool(item)) ? cJSON_IsTrue(item) : false;
    cJSON_Delete(root);
    return bg;
}

static void _app_on_pause(eos_activity_t *a)
{
    app_launch_ctx_t *ctx = eos_activity_get_user_data(a);
    if (!ctx || !ctx->background)
        return;

    /* Suspend (not terminate) the app program so its realm and state are kept.
     * The keep-alive activity's view is hidden by the framework; we register it
     * in the Background App Indicator so a top-center icon appears. */
    spm_app_suspend();
    bool _wants = _js_app_wants_background(ctx->app_id);
    EOS_LOG_I("EOS-DBG app_on_pause: %s background-flag=%d", ctx->app_id, _wants);
    if (_wants)
    {
        eos_bg_indicator_register(ctx->app_id, ctx->icon_src, a);
    }
}

static void _app_on_resume(eos_activity_t *a)
{
    app_launch_ctx_t *ctx = eos_activity_get_user_data(a);
    if (!ctx || !ctx->background)
        return;

    /* Drop the indicator icon and resume the suspended program. */
    eos_bg_indicator_unregister(ctx->app_id);
    spm_app_resume();
}

static void _app_on_enter(eos_activity_t *a)
{
    app_launch_ctx_t *ctx = eos_activity_get_user_data(a);
    EOS_CHECK_PTR_RETURN(ctx);

    /* Apps exit via swipe-back (indev-level gesture), so the header's
     * back button is hidden while an app page is active. */
    eos_app_header_set_back_btn_visible(false);

    /* 重进 app（app 列表 launch 路径）时清除该 app 的后台指示器：
     * launch 不自动 unregister，否则旧标一直留在 watchface（重置后仍在）。 */
    eos_bg_indicator_unregister(ctx->app_id);

    eos_result_t ret = spm_app_run(&ctx->pkg);
    if (ret != EOS_OK)
    {
        // Determine error type based on error code
        eos_script_error_type_t error_type = EOS_SCRIPT_FAULT_ERROR_EXCEPTION;
        if (ret == EOS_ERR_TIMEOUT)
        {
            error_type = EOS_SCRIPT_FAULT_UNRESPONSIVE;
        }
        else
        {
            // Check error info for timeout if code doesn't indicate it
            const char *error_info = script_engine_get_error_info();
            if (error_info && strstr(error_info, "timeout"))
            {
                error_type = EOS_SCRIPT_FAULT_UNRESPONSIVE;
            }
        }

        // Only handle error if it hasn't been handled already (timeout is handled inside script_engine_run)
        if (ret != EOS_ERR_TIMEOUT)
        {
            eos_app_handle_script_error(error_type, ret, ctx->app_id, NULL);
        }
        EOS_LOG_E("Application encounter a fatal error");
    }
}

static int32_t _app_list_find_sys_app(const char *app_id)
{
    if (!app_id)
    {
        return -1;
    }

    for (int32_t i = 0; i < EOS_SYS_APP_LAST; i++)
    {
        if (strcmp(app_id, eos_sys_app_id_list[i]) == 0)
        {
            return i;
        }
    }

    return -1;
}

static int32_t _app_list_find_native_app(const char *app_id)
{
    if (!app_id)
    {
        return -1;
    }

    for (int32_t i = 0; i < EOS_NATIVE_APP_LAST; i++)
    {
        if (strcmp(app_id, eos_native_app_id_list[i]) == 0)
        {
            return i;
        }
    }

    return -1;
}

/* Resolve an app icon: prefer the installed app's own icon.bin, then the
 * native C app icon (if present), then the generic app icon. */
static const void *_app_list_resolve_icon(const char *app_id, char *icon_path, size_t icon_path_size)
{
    snprintf(icon_path, icon_path_size, EOS_APP_INSTALLED_DIR "%s/" EOS_APP_ICON_FILE_NAME, app_id);
    if (eos_storage_is_file(icon_path))
    {
        return icon_path;
    }

    int32_t native_index = _app_list_find_native_app(app_id);
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    /* USB MSC:图标编译进 Flash(eos_icon_usb_msc),不依赖 SD 上的 .bin。
     * 直接返回 lv_image_dsc_t 指针,lv_image_set_src() 原生支持。 */
    if (native_index == EOS_NATIVE_APP_USB_MSC)
    {
        return &eos_icon_usb_msc;
    }
#endif
    if (native_index >= 0 && eos_native_app_icon_list[native_index] &&
        eos_storage_is_file(eos_native_app_icon_list[native_index]))
    {
        snprintf(icon_path, icon_path_size, "%s", eos_native_app_icon_list[native_index]);
        return icon_path;
    }

    snprintf(icon_path, icon_path_size, "%s", EOS_IMG_APP);
    return icon_path;
}

static eos_result_t _app_list_build_script_pkg(const char *app_id, script_pkg_t *pkg)
{
    if (!(app_id && pkg))
    {
        return EOS_ERR_SCRIPT_NULL_PACKAGE;
    }

    char manifest_path[EOS_FS_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_MANIFEST_FILE_NAME, app_id);

    pkg->type = SCRIPT_TYPE_APPLICATION;
    if (script_engine_get_manifest(manifest_path, pkg) != EOS_OK)
    {
        EOS_LOG_E("Read manifest failed: %s", manifest_path);
        return EOS_FAILED;
    }

    char script_path[EOS_FS_PATH_MAX];
    snprintf(script_path, sizeof(script_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_SCRIPT_ENTRY_FILE_NAME, app_id);

    char base_path[EOS_FS_PATH_MAX];
    snprintf(base_path, sizeof(base_path), EOS_APP_INSTALLED_DIR "%s/", app_id);
    pkg->base_path = eos_strdup(base_path);
    if (!pkg->base_path)
    {
        eos_pkg_free(pkg);
        return EOS_ERR_MEM;
    }

    if (!eos_storage_is_file(script_path))
    {
        EOS_LOG_E("Can't find script: %s", script_path);
        eos_pkg_free(pkg);
        return EOS_FAILED;
    }

    pkg->script_str = eos_storage_read_file(script_path);
    if (!pkg->script_str)
    {
        EOS_LOG_E("Failed to read script: %s", script_path);
        eos_pkg_free(pkg);
        return EOS_FAILED;
    }

    return EOS_OK;
}

static eos_result_t _app_list_launch_script_app(const char *app_id)
{
    script_pkg_t pkg = {0};
    if (_app_list_build_script_pkg(app_id, &pkg) != EOS_OK)
    {
        return EOS_FAILED;
    }

    app_launch_ctx_t *ctx = eos_malloc_zeroed(sizeof(app_launch_ctx_t));
    if (!ctx)
    {
        EOS_LOG_E("Failed to allocate app launch context");
        eos_pkg_free(&pkg);
        return EOS_FAILED;
    }

    ctx->pkg = pkg;
    ctx->app_id = eos_strdup(app_id);
    if (!ctx->app_id)
    {
        EOS_LOG_E("Failed to copy app id");
        eos_pkg_free(&ctx->pkg);
        eos_free(ctx);
        return EOS_FAILED;
    }

    /* Background App Indicator support: read the manifest background flag and
     * the app icon path so the app can keep running in the background. */
    ctx->background = pkg.background;
    snprintf(ctx->icon_src, sizeof(ctx->icon_src),
             EOS_APP_INSTALLED_DIR "%s/" EOS_APP_ICON_FILE_NAME, app_id);

    eos_activity_t *a = eos_activity_create(&app_lifecycle);
    if (!a)
    {
        EOS_LOG_E("Failed to create activity");
        eos_pkg_free(&ctx->pkg);
        eos_free(ctx->app_id);
        eos_free(ctx);
        return EOS_FAILED;
    }

    lv_obj_t *app_view = eos_activity_get_view(a);
    lv_obj_set_size(app_view, EOS_DISPLAY_WIDTH, EOS_DISPLAY_HEIGHT);
    eos_activity_set_type(a, EOS_ACTIVITY_TYPE_APP);
    eos_activity_set_user_data(a, ctx);
    eos_activity_set_title(a, pkg.name);
    /* WOS framework owns the unified status bar (layer 5). The legacy
     * app_header (layer 3, 120px tall, with its own clock + title label)
     * would stack on top of it and visually collide with both the
     * statusbar AND the app content below. The app's name is exposed
     * through the WOS statusbar title instead (see _app_on_enter). */
    eos_activity_set_app_header_visible(a, false);

    /* Background apps stay alive (view + program kept) when the user backs out,
     * and are restored by tapping the Background App Indicator icon. */
    if (ctx->background)
        eos_activity_set_keep_alive_on_back(a, true);

    EOS_LOG_D("view_size: %d, %d", lv_obj_get_width(app_view), lv_obj_get_height(app_view));

    eos_activity_enter(a);
    return EOS_OK;
}

/**
 * @brief Return to app list from within an app and any sub-activities
 * @return eos_result_t EOS_OK success, EOS_FAILED failed or not in app
 * @note If not in app context, returns EOS_FAILED; otherwise clears the activity stack
 */
static eos_result_t _app_list_pop_to_app_list(void)
{
    eos_activity_t *current = eos_activity_get_current();
    if (!current)
    {
        return EOS_FAILED;
    }

    eos_activity_type_t current_type = eos_activity_get_type(current);

    // If already at app list, no need to pop
    if (current_type == EOS_ACTIVITY_TYPE_APP_LIST)
    {
        return EOS_OK;
    }

    // If not in app context (not in app or app list), return failure
    // This prevents unexpected navigation from watchface or other contexts
    if (current_type != EOS_ACTIVITY_TYPE_APP)
    {
        return EOS_FAILED;
    }

    // Currently in app (possibly with sub-activities), return to watchface
    // This will clean up all app-related activities on the stack
    eos_activity_back_to_watchface();
    return EOS_OK;
}

eos_result_t eos_app_launch_immediately(const char *app_id)
{
    if (!(app_id && app_id[0]))
    {
        EOS_LOG_E("Invalid app id");
        return EOS_FAILED;
    }

    if (eos_activity_is_transition_in_progress())
    {
        EOS_LOG_W("Cannot launch app while activity transition is in progress");
        return EOS_FAILED;
    }

    _register_anim_routes_once();
    _app_list_set_last_launch_app_id(app_id);

    int32_t sys_app_index = _app_list_find_sys_app(app_id);
    if (sys_app_index >= 0)
    {
        if (eos_sys_app_entry_list[sys_app_index])
        {
            eos_sys_app_entry_list[sys_app_index]();
            return EOS_OK;
        }
        return EOS_FAILED;
    }

    int32_t native_app_index = _app_list_find_native_app(app_id);
    if (native_app_index >= 0)
    {
        if (eos_native_app_entry_list[native_app_index])
        {
            eos_native_app_entry_list[native_app_index]();
            return EOS_OK;
        }
        return EOS_FAILED;
    }

    if (!eos_app_list_contains(app_id))
    {
        EOS_LOG_E("App not found: %s", app_id);
        return EOS_FAILED;
    }

    // If currently inside an app (or app sub-activity), pop back to watchface first
    // This ensures all app-internal activities are cleaned up before launching new app
    eos_activity_type_t current_type = eos_activity_get_type(eos_activity_get_current());
    if (current_type == EOS_ACTIVITY_TYPE_APP)
    {
        EOS_LOG_I("Returning to app list before launching new app");
        _app_list_pop_to_app_list();
    }

    return _app_list_launch_script_app(app_id);
}

static const char *_app_list_get_launch_app_id(eos_activity_t *activity)
{
    app_launch_ctx_t *ctx = eos_activity_get_user_data(activity);
    if (ctx && ctx->app_id)
    {
        return ctx->app_id;
    }

    return _app_list_last_launch_app_id[0] ? _app_list_last_launch_app_id : NULL;
}

static void _app_list_set_last_launch_app_id(const char *app_id)
{
    if (!app_id)
    {
        _app_list_last_launch_app_id[0] = '\0';
        return;
    }

    snprintf(_app_list_last_launch_app_id, sizeof(_app_list_last_launch_app_id), "%s", app_id);
}

/* 公开查询"最近一次启动的 App id"(供系统层在深睡/待机前快照恢复目标)。
 * 从未启动过任何 App(整机刚开)返回 NULL;是否前台需调用方结合当前
 * activity 类型判断(退出到表盘后该 id 仍保留最近一次值)。 */
const char *eos_app_list_get_last_launch_app_id(void)
{
    return _app_list_last_launch_app_id[0] ? _app_list_last_launch_app_id : NULL;
}

static lv_obj_t *_app_list_get_bubble_grid(eos_activity_t *activity)
{
    if (!activity)
    {
        return NULL;
    }

    lv_obj_t *bubble_grid = (lv_obj_t *)eos_activity_get_user_data(activity);
    if (bubble_grid)
    {
        return bubble_grid;
    }

    lv_obj_t *view = eos_activity_get_view(activity);
    if (!view)
    {
        return NULL;
    }

    return lv_obj_get_child(view, 0);
}

static void _app_list_record_icon_center_point(int32_t x, int32_t y)
{
    _app_list_last_icon_center_x = x;
    _app_list_last_icon_center_y = y;
    _app_list_last_icon_center_valid = true;
}

static bool _app_list_calc_focus_pivot_by_global_center(lv_obj_t *obj, int32_t *pivot_x, int32_t *pivot_y)
{
    if (!(obj && pivot_x && pivot_y && _app_list_last_icon_center_valid))
    {
        return false;
    }

    int32_t max_x = lv_obj_get_width(obj);
    int32_t max_y = lv_obj_get_height(obj);
    int32_t local_x = _app_list_last_icon_center_x;
    int32_t local_y = _app_list_last_icon_center_y;

    if (local_x < 0)
        local_x = 0;
    if (local_y < 0)
        local_y = 0;

    /* Defensive: if the object reports zero width/height (e.g. snapshot
     * image not yet laid out), use the known display dimensions. */
    if (max_x <= 0)
        max_x = EOS_DISPLAY_WIDTH;
    if (max_y <= 0)
        max_y = EOS_DISPLAY_HEIGHT;

    if (local_x > max_x)
        local_x = max_x;
    if (local_y > max_y)
        local_y = max_y;

    *pivot_x = local_x;
    *pivot_y = local_y;
    return true;
}

static bool _app_list_calc_focus_pivot(lv_obj_t *snapshot_obj, lv_obj_t *icon_obj, int32_t *pivot_x, int32_t *pivot_y)
{
    if (!(snapshot_obj && pivot_x && pivot_y))
    {
        return false;
    }

    lv_area_t snapshot_area;
    lv_obj_get_coords(snapshot_obj, &snapshot_area);

    if (!icon_obj)
    {
        *pivot_x = lv_area_get_width(&snapshot_area) / 2;
        *pivot_y = lv_area_get_height(&snapshot_area) / 2;
        return false;
    }

    lv_area_t icon_area;
    lv_obj_get_coords(icon_obj, &icon_area);

    int32_t icon_mid_x = icon_area.x1 + (lv_area_get_width(&icon_area) / 2);
    int32_t icon_mid_y = icon_area.y1 + (lv_area_get_height(&icon_area) / 2);

    *pivot_x = icon_mid_x - snapshot_area.x1;
    *pivot_y = icon_mid_y - snapshot_area.y1;
    return true;
}

static void _app_list_set_transform_scale_cb(void *var, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)var, value, 0);
}

static void _app_list_set_image_scale_cb(void *var, int32_t value)
{
    lv_image_set_scale((lv_obj_t *)var, value);
}

static void _app_list_set_translate_x_cb(void *var, int32_t value)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, value, 0);
}

static void _app_list_set_translate_y_cb(void *var, int32_t value)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, value, 0);
}

static void _app_list_set_opa_cb(void *var, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}

static void _app_list_init_scale_anim(lv_anim_t *anim, lv_obj_t *obj, int32_t start, int32_t end, uint32_t duration)
{
    lv_anim_init(anim);
    lv_anim_set_var(anim, obj);
    lv_anim_set_values(anim, start, end);
    lv_anim_set_exec_cb(anim, _app_list_set_transform_scale_cb);
    lv_anim_set_path_cb(anim, lv_anim_path_ease_in_out);
    lv_anim_set_duration(anim, duration);
}

static void _app_list_init_image_scale_anim(lv_anim_t *anim,
                                            lv_obj_t *obj,
                                            int32_t start,
                                            int32_t end,
                                            uint32_t duration)
{
    lv_anim_init(anim);
    lv_anim_set_var(anim, obj);
    lv_anim_set_values(anim, start, end);
    lv_anim_set_exec_cb(anim, _app_list_set_image_scale_cb);
    lv_anim_set_path_cb(anim, lv_anim_path_ease_in_out);
    lv_anim_set_duration(anim, duration);
}

static void _app_list_init_translate_x_anim(lv_anim_t *anim,
                                            lv_obj_t *obj,
                                            int32_t start,
                                            int32_t end,
                                            uint32_t duration)
{
    lv_anim_init(anim);
    lv_anim_set_var(anim, obj);
    lv_anim_set_values(anim, start, end);
    lv_anim_set_exec_cb(anim, _app_list_set_translate_x_cb);
    lv_anim_set_path_cb(anim, lv_anim_path_ease_in_out);
    lv_anim_set_duration(anim, duration);
}

static void _app_list_init_translate_y_anim(lv_anim_t *anim,
                                            lv_obj_t *obj,
                                            int32_t start,
                                            int32_t end,
                                            uint32_t duration)
{
    lv_anim_init(anim);
    lv_anim_set_var(anim, obj);
    lv_anim_set_values(anim, start, end);
    lv_anim_set_exec_cb(anim, _app_list_set_translate_y_cb);
    lv_anim_set_path_cb(anim, lv_anim_path_ease_in_out);
    lv_anim_set_duration(anim, duration);
}

static void _app_list_init_opa_anim(lv_anim_t *anim, lv_obj_t *obj, int32_t start, int32_t end, uint32_t duration)
{
    lv_anim_init(anim);
    lv_anim_set_var(anim, obj);
    lv_anim_set_values(anim, start, end);
    lv_anim_set_exec_cb(anim, _app_list_set_opa_cb);
    lv_anim_set_path_cb(anim, lv_anim_path_ease_in_out);
    lv_anim_set_duration(anim, duration);
}

static void _app_list_cleanup_extra_cb(lv_event_t *e)
{
    lv_obj_t *extra = (lv_obj_t *)lv_event_get_user_data(e);
    if (extra && lv_obj_is_valid(extra))
    {
        lv_obj_delete(extra);
    }
}

static lv_obj_t *_app_list_create_icon_clone(lv_obj_t *focus_icon)
{
    if (!focus_icon)
    {
        return NULL;
    }

    lv_obj_t *icon_img = lv_obj_get_child(focus_icon, 0);
    if (!icon_img)
    {
        return NULL;
    }

    const void *img_src = lv_image_get_src(icon_img);
    if (!img_src)
    {
        return NULL;
    }

    lv_area_t icon_coords;
    lv_obj_get_coords(focus_icon, &icon_coords);
    int32_t bw = lv_area_get_width(&icon_coords);
    int32_t bh = lv_area_get_height(&icon_coords);

    lv_obj_t *icon_clone = lv_obj_create(eos_overlay_get_snapshot_layer());
    lv_obj_set_size(icon_clone, bw, bh);
    lv_obj_set_pos(icon_clone, icon_coords.x1, icon_coords.y1);
    lv_obj_set_style_radius(icon_clone, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(icon_clone, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(icon_clone, lv_obj_get_style_bg_color(focus_icon, 0), 0);
    lv_obj_set_style_border_width(icon_clone, 0, 0);
    lv_obj_set_style_pad_all(icon_clone, 0, 0);
    lv_obj_set_style_clip_corner(icon_clone, true, 0);
    lv_obj_set_style_transform_pivot_x(icon_clone, bw / 2, 0);
    lv_obj_set_style_transform_pivot_y(icon_clone, bh / 2, 0);
    lv_obj_remove_flag(icon_clone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(icon_clone, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *icon_img_clone = lv_image_create(icon_clone);
    lv_image_set_src(icon_img_clone, img_src);
    lv_image_set_scale_x(icon_img_clone, lv_image_get_scale_x(icon_img));
    lv_image_set_scale_y(icon_img_clone, lv_image_get_scale_y(icon_img));
    lv_obj_set_size(icon_img_clone, bw, bh);
    lv_image_set_inner_align(icon_img_clone, LV_IMAGE_ALIGN_CENTER);
    lv_obj_center(icon_img_clone);
    lv_obj_set_style_bg_opa(icon_img_clone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_img_clone, 0, 0);
    lv_obj_remove_flag(icon_img_clone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(icon_img_clone, LV_OBJ_FLAG_CLICKABLE);

    return icon_clone;
}

static void _app_list_play_transition_anim(lv_anim_timeline_t *at,
                                           eos_activity_t *from,
                                           eos_activity_t *to,
                                           bool opening)
{
    if (!(at && from && to))
    {
        return;
    }

    lv_obj_t *list_view = opening ? eos_activity_get_view(from) : eos_activity_get_view(to);
    lv_obj_t *focus_icon = NULL;
    if (_app_list_last_click_index >= 0)
    {
        focus_icon = _app_list_find_icon_by_index((uint32_t)_app_list_last_click_index);
    }

    /* Closing animation should not render app header on top. */
    bool include_header_in_snapshot = opening;

    lv_obj_t *list_snapshot = NULL;
    bool focus_icon_hidden_flag = false;
    if (focus_icon && lv_obj_has_flag(focus_icon, LV_OBJ_FLAG_HIDDEN))
    {
        focus_icon_hidden_flag = true;
    }

    lv_obj_t *icon_clone = NULL;
    lv_obj_t *app_snapshot = NULL;

    if (opening)
    {
        icon_clone = _app_list_create_icon_clone(focus_icon);

        if (focus_icon)
        {
            lv_obj_add_flag(focus_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        app_snapshot = eos_activity_take_snapshot(from, include_header_in_snapshot);
        if (!app_snapshot)
        {
            return;
        }
        lv_obj_move_foreground(app_snapshot);

        if (list_view)
        {
            lv_obj_remove_flag(list_view, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(list_view);
        }

        if (focus_icon)
        {
            lv_area_t area;
            lv_obj_get_coords(focus_icon, &area);
            _app_list_record_icon_center_point(area.x1 + lv_area_get_width(&area) / 2,
                                               area.y1 + lv_area_get_height(&area) / 2);
        }

        icon_clone = _app_list_create_icon_clone(focus_icon);
        if (icon_clone)
        {
            lv_obj_set_style_opa(icon_clone, LV_OPA_TRANSP, 0);
        }

        if (focus_icon)
        {
            lv_obj_add_flag(focus_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    eos_activity_t *list_activity = opening ? from : to;
    if (list_activity)
    {
        list_snapshot = eos_activity_take_snapshot(list_activity, false);
    }

    if (!opening && app_snapshot)
    {
        lv_obj_move_foreground(app_snapshot);
    }

    if (icon_clone)
    {
        lv_obj_move_foreground(icon_clone);
    }

    if (focus_icon && !focus_icon_hidden_flag)
    {
        lv_obj_remove_flag(focus_icon, LV_OBJ_FLAG_HIDDEN);
    }

    if (opening)
    {
        app_snapshot = eos_activity_take_snapshot(to, include_header_in_snapshot);
        if (!app_snapshot)
        {
            if (icon_clone)
            {
                lv_obj_delete(icon_clone);
                icon_clone = NULL;
            }
            return;
        }
    }

    int32_t list_pivot_x = 0;
    int32_t list_pivot_y = 0;
    int32_t app_pivot_x = 0;
    int32_t app_pivot_y = 0;

    if (list_snapshot)
    {
        if (!_app_list_calc_focus_pivot_by_global_center(list_snapshot, &list_pivot_x, &list_pivot_y))
        {
            _app_list_calc_focus_pivot(list_snapshot, focus_icon, &list_pivot_x, &list_pivot_y);
        }
        lv_image_set_pivot(list_snapshot, list_pivot_x, list_pivot_y);
    }

    if (!_app_list_calc_focus_pivot_by_global_center(app_snapshot, &app_pivot_x, &app_pivot_y))
    {
        _app_list_calc_focus_pivot(app_snapshot, focus_icon, &app_pivot_x, &app_pivot_y);
    }
    lv_image_set_pivot(app_snapshot, app_pivot_x, app_pivot_y);

    if (icon_clone)
    {
        lv_obj_move_foreground(icon_clone);
    }
    lv_obj_move_foreground(app_snapshot);

    uint32_t total_duration = (uint32_t)_APP_LIST_ANIM_DURATION;
    if (total_duration == 0U)
    {
        total_duration = 1U;
    }

    uint32_t split_delay = (total_duration * (uint32_t)_APP_LIST_ANIM_SPLIT_PCT) / 100U;
    if (split_delay >= total_duration)
    {
        split_delay = total_duration > 1U ? total_duration - 1U : 0U;
    }

    uint32_t from_scale_duration = total_duration;
    uint32_t to_duration = (total_duration > split_delay) ? (total_duration - split_delay) : 1U;

    int32_t focus_translate_x = 0;
    int32_t focus_translate_y = 0;
    if (_app_list_last_icon_center_valid)
    {
        int32_t view_center_x = EOS_DISPLAY_WIDTH / 2;
        int32_t view_center_y = EOS_DISPLAY_HEIGHT / 2;
        focus_translate_x = view_center_x - _app_list_last_icon_center_x;
        focus_translate_y = view_center_y - _app_list_last_icon_center_y;
    }

    lv_anim_t list_scale_anim;
    lv_anim_t list_translate_x_anim;
    lv_anim_t list_translate_y_anim;
    lv_anim_t icon_scale_anim;
    lv_anim_t icon_translate_x_anim;
    lv_anim_t icon_translate_y_anim;
    lv_anim_t icon_opa_anim;
    lv_anim_t app_scale_anim;
    lv_anim_t app_opa_anim;

    if (opening)
    {
        if (list_snapshot)
        {
            lv_image_set_scale(list_snapshot, 256);
            lv_obj_set_style_translate_x(list_snapshot, 0, 0);
            lv_obj_set_style_translate_y(list_snapshot, 0, 0);
            _app_list_init_image_scale_anim(&list_scale_anim,
                                            list_snapshot,
                                            256,
                                            _APP_LIST_ANIM_FOCUS_SCALE,
                                            from_scale_duration);
            _app_list_init_translate_x_anim(&list_translate_x_anim,
                                            list_snapshot,
                                            0,
                                            focus_translate_x,
                                            from_scale_duration);
            _app_list_init_translate_y_anim(&list_translate_y_anim,
                                            list_snapshot,
                                            0,
                                            focus_translate_y,
                                            from_scale_duration);
            lv_anim_timeline_add(at, 0, &list_scale_anim);
            lv_anim_timeline_add(at, 0, &list_translate_x_anim);
            lv_anim_timeline_add(at, 0, &list_translate_y_anim);
        }

        if (icon_clone)
        {
            lv_obj_set_style_transform_scale(icon_clone, 256, 0);
            lv_obj_set_style_translate_x(icon_clone, 0, 0);
            lv_obj_set_style_translate_y(icon_clone, 0, 0);
            lv_obj_set_style_opa(icon_clone, (lv_opa_t)_APP_LIST_ANIM_FROM_OPA_START, 0);
            _app_list_init_scale_anim(&icon_scale_anim,
                                      icon_clone,
                                      256,
                                      _APP_LIST_ANIM_FOCUS_SCALE,
                                      from_scale_duration);
            _app_list_init_translate_x_anim(&icon_translate_x_anim,
                                            icon_clone,
                                            0,
                                            focus_translate_x,
                                            from_scale_duration);
            _app_list_init_translate_y_anim(&icon_translate_y_anim,
                                            icon_clone,
                                            0,
                                            focus_translate_y,
                                            from_scale_duration);
            _app_list_init_opa_anim(&icon_opa_anim,
                                    icon_clone,
                                    _APP_LIST_ANIM_FROM_OPA_START,
                                    _APP_LIST_ANIM_FROM_OPA_END,
                                    total_duration);
            lv_anim_timeline_add(at, 0, &icon_scale_anim);
            lv_anim_timeline_add(at, 0, &icon_translate_x_anim);
            lv_anim_timeline_add(at, 0, &icon_translate_y_anim);
            lv_anim_timeline_add(at, 0, &icon_opa_anim);
        }

        lv_image_set_scale(app_snapshot, _APP_LIST_ANIM_MIN_SACLE);
        lv_obj_set_style_opa(app_snapshot, (lv_opa_t)_APP_LIST_ANIM_TO_OPA_START, 0);
        _app_list_init_image_scale_anim(&app_scale_anim, app_snapshot, _APP_LIST_ANIM_MIN_SACLE, 256, to_duration);
        _app_list_init_opa_anim(&app_opa_anim,
                                app_snapshot,
                                _APP_LIST_ANIM_TO_OPA_START,
                                _APP_LIST_ANIM_TO_OPA_END,
                                to_duration);
        lv_anim_timeline_add(at, split_delay, &app_scale_anim);
        lv_anim_timeline_add(at, split_delay, &app_opa_anim);
    }
    else
    {
        if (list_snapshot)
        {
            lv_image_set_scale(list_snapshot, _APP_LIST_ANIM_FOCUS_SCALE);
            lv_obj_set_style_translate_x(list_snapshot, focus_translate_x, 0);
            lv_obj_set_style_translate_y(list_snapshot, focus_translate_y, 0);
            _app_list_init_image_scale_anim(&list_scale_anim,
                                            list_snapshot,
                                            _APP_LIST_ANIM_FOCUS_SCALE,
                                            256,
                                            to_duration);
            _app_list_init_translate_x_anim(&list_translate_x_anim, list_snapshot, focus_translate_x, 0, to_duration);
            _app_list_init_translate_y_anim(&list_translate_y_anim, list_snapshot, focus_translate_y, 0, to_duration);
            lv_anim_timeline_add(at, split_delay, &list_scale_anim);
            lv_anim_timeline_add(at, split_delay, &list_translate_x_anim);
            lv_anim_timeline_add(at, split_delay, &list_translate_y_anim);
        }

        if (icon_clone)
        {
            lv_obj_set_style_transform_scale(icon_clone, _APP_LIST_ANIM_FOCUS_SCALE, 0);
            lv_obj_set_style_translate_x(icon_clone, focus_translate_x, 0);
            lv_obj_set_style_translate_y(icon_clone, focus_translate_y, 0);
            lv_obj_set_style_opa(icon_clone, (lv_opa_t)_APP_LIST_ANIM_FROM_OPA_END, 0);
            _app_list_init_scale_anim(&icon_scale_anim, icon_clone, _APP_LIST_ANIM_FOCUS_SCALE, 256, to_duration);
            _app_list_init_translate_x_anim(&icon_translate_x_anim, icon_clone, focus_translate_x, 0, to_duration);
            _app_list_init_translate_y_anim(&icon_translate_y_anim, icon_clone, focus_translate_y, 0, to_duration);
            _app_list_init_opa_anim(&icon_opa_anim,
                                    icon_clone,
                                    _APP_LIST_ANIM_FROM_OPA_END,
                                    _APP_LIST_ANIM_FROM_OPA_START,
                                    to_duration);
            lv_anim_timeline_add(at, split_delay, &icon_scale_anim);
            lv_anim_timeline_add(at, split_delay, &icon_translate_x_anim);
            lv_anim_timeline_add(at, split_delay, &icon_translate_y_anim);
            lv_anim_timeline_add(at, split_delay, &icon_opa_anim);
        }

        lv_image_set_scale(app_snapshot, 256);
        lv_obj_set_style_opa(app_snapshot, (lv_opa_t)_APP_LIST_ANIM_TO_OPA_END, 0);
        _app_list_init_image_scale_anim(&app_scale_anim, app_snapshot, 256, _APP_LIST_ANIM_MIN_SACLE, to_duration);
        _app_list_init_opa_anim(&app_opa_anim,
                                app_snapshot,
                                _APP_LIST_ANIM_TO_OPA_END,
                                _APP_LIST_ANIM_TO_OPA_START,
                                to_duration);
        lv_anim_timeline_add(at, 0, &app_scale_anim);
        lv_anim_timeline_add(at, 0, &app_opa_anim);
    }

    if (icon_clone && app_snapshot)
    {
        lv_obj_add_event_cb(app_snapshot, _app_list_cleanup_extra_cb, LV_EVENT_DELETE, icon_clone);
    }
}

static void _app_list_on_resueme(eos_activity_t *a)
{
    // Initialize app list
    lv_obj_t *bubble_grid = _app_list_get_bubble_grid(a);
    EOS_CHECK_PTR_RETURN(bubble_grid);
    _app_list_refresh(bubble_grid);
}

/************************** App Entry **************************/
/**
 * @brief Delete callback for a grid icon: releases the duplicated app id.
 */
static void _app_grid_icon_delete_cb(lv_event_t *e)
{
    _app_grid_icon_ctx_t *ctx = (_app_grid_icon_ctx_t *)lv_event_get_user_data(e);
    if (!ctx)
    {
        return;
    }
    if (ctx->app_id)
    {
        eos_free(ctx->app_id);
    }
    eos_free(ctx);
}

/**
 * @brief App click event callback (handles system apps and script apps)
 * @note The grid icon owns a duplicated app id, so the id stays valid even if
 *       the plugin manager's internal list is rebuilt later.
 */
static void _app_grid_icon_clicked_cb(lv_event_t *e)
{
    _app_grid_icon_ctx_t *ctx = (_app_grid_icon_ctx_t *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ctx && ctx->app_id);

    _app_list_set_last_launch_app_id(ctx->app_id);
    _app_list_last_click_index = (int32_t)ctx->index;

    /* Use the icon's center, not the click point, so animation pivot/translate
     * is consistent regardless of where the user touches. */
    lv_obj_t *icon = lv_event_get_target(e);
    if (icon)
    {
        lv_area_t area;
        lv_obj_get_coords(icon, &area);
        _app_list_record_icon_center_point(area.x1 + lv_area_get_width(&area) / 2,
                                           area.y1 + lv_area_get_height(&area) / 2);
    }

    if (eos_app_launch_immediately(ctx->app_id) != EOS_OK)
    {
        EOS_LOG_E("Launch app failed: %s", ctx->app_id);
    }
}

/**
 * @brief Find the icon object by its global index (used by transition anims).
 */
static lv_obj_t *_app_list_find_icon_by_index(uint32_t index)
{
    if (index >= _APP_GRID_MAX_ICONS)
    {
        return NULL;
    }
    return _app_icon_cache[index];
}

static void _register_anim_routes_once(void)
{
    if (_anim_routes_registered)
    {
        return;
    }

    eos_activity_register_anim_route(EOS_ACTIVITY_TYPE_APP_LIST, EOS_ACTIVITY_TYPE_APP, _app_list_open_app_anim_cb);
    eos_activity_register_anim_route(EOS_ACTIVITY_TYPE_APP, EOS_ACTIVITY_TYPE_APP_LIST, _app_list_close_app_anim_cb);
    _anim_routes_registered = true;
}

/************************** Refresh App List **************************/
/**
 * @brief Refresh app list - using bubble_grid
 * @param bubble_grid App list's bubble_grid object
 */
/**
 * @brief Append one icon to the paged grid, starting a new page every 6 icons.
 */
static void _app_list_append_icon(eos_card_pager_t *cp,
                                  lv_obj_t **cur_page,
                                  uint32_t *icon_index,
                                  const void *icon_src,
                                  const char *app_id)
{
    if (!(cp && cur_page && icon_index && icon_src && app_id))
    {
        return;
    }

    /* Start a new page every 7 icons (3 rows: 2-3-2). */
    if (*icon_index % _APP_GRID_PAGE_SIZE == 0)
    {
        *cur_page = eos_card_pager_create_page(cp);
        if (*cur_page)
        {
            lv_obj_set_style_bg_color(*cur_page, EOS_COLOR_BLACK, 0);
            lv_obj_set_style_bg_opa(*cur_page, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(*cur_page, 0, 0);
        }
    }
    if (!*cur_page)
    {
        return;
    }

    uint32_t slot = *icon_index % _APP_GRID_PAGE_SIZE;
    /* Icon slot table (rows 2-3-2, columns 0-based per row):
     *   top    : [0][1]
     *   middle : [0][1][2]
     *   bottom : [0][1]
     * Every row is horizontally centered so the cluster reads as a balanced
     * 2-3-2 grid instead of a skewed honeycomb. */
    static const int8_t _hc_row_of[7] = {0, 0, 1, 1, 1, 2, 2};
    static const int8_t _hc_col_of[7] = {0, 1, 0, 1, 2, 0, 1};
    static const int8_t _hc_row_icons[3] = {2, 3, 2};
    int32_t row = _hc_row_of[slot];
    int32_t col = _hc_col_of[slot];
    int32_t row_icons = _hc_row_icons[row];
    /* Center the row horizontally: each row's icon centers are spread by the
     * column gap and the whole row is shifted so its midpoint sits at 120. */
    int32_t cx = EOS_DISPLAY_WIDTH / 2
                 + col * _APP_GRID_COL_GAP
                 - (row_icons * _APP_GRID_COL_GAP / 2 - _APP_GRID_COL_GAP / 2);
    int32_t cy = (EOS_DISPLAY_HEIGHT - (2 * _APP_GRID_ROW_GAP)) / 2 + row * _APP_GRID_ROW_GAP;

    /* Icon: round tile + child image loaded from the icon_src path, so the
     * launch transition anim can clone the child image and pivot around the
     * tile center. */
    lv_obj_t *icon = lv_obj_create(*cur_page);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, _APP_GRID_ICON_SIZE, _APP_GRID_ICON_SIZE);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0x242424), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_pos(icon, cx - _APP_GRID_ICON_SIZE / 2, cy - _APP_GRID_ICON_SIZE / 2);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_t *img = lv_image_create(icon);
    lv_image_set_src(img, icon_src);
    lv_image_set_scale_x(img, 256);
    lv_image_set_scale_y(img, 256);
    lv_obj_center(img);

    _app_grid_icon_ctx_t *ctx = (_app_grid_icon_ctx_t *)eos_malloc(sizeof(_app_grid_icon_ctx_t));
    if (!ctx)
    {
        return;
    }
    ctx->index = *icon_index;
    ctx->app_id = eos_strdup(app_id);
    if (!ctx->app_id)
    {
        eos_free(ctx);
        return;
    }
    lv_obj_add_event_cb(icon, _app_grid_icon_clicked_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(icon, _app_grid_icon_delete_cb, LV_EVENT_DELETE, ctx);

    if (*icon_index < _APP_GRID_MAX_ICONS)
    {
        _app_icon_cache[*icon_index] = icon;
    }
    (*icon_index)++;
}

/*------------------- Pager arrow navigation -------------------*/

static void _app_list_update_arrows(void)
{
    eos_card_pager_t *cp = _app_list_pager;
    if (!cp)
    {
        return;
    }
    bool multi = cp->page_count > 1;
    if (_app_arrow_prev)
    {
        bool show = multi && cp->current_page_index > 0;
        lv_obj_set_style_opa(_app_arrow_prev, show ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(_app_arrow_prev, LV_OBJ_FLAG_CLICKABLE);
        if (show)
            lv_obj_add_flag(_app_arrow_prev, LV_OBJ_FLAG_CLICKABLE);
    }
    if (_app_arrow_next)
    {
        bool show = multi && cp->current_page_index < cp->page_count - 1;
        lv_obj_set_style_opa(_app_arrow_next, show ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(_app_arrow_next, LV_OBJ_FLAG_CLICKABLE);
        if (show)
            lv_obj_add_flag(_app_arrow_next, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void _app_list_page_changed_cb(eos_card_pager_t *cp, uint8_t page_index, void *user_data)
{
    (void)cp;
    (void)page_index;
    (void)user_data;
    _app_list_update_arrows();
}

static void _app_arrow_clicked_cb(lv_event_t *e)
{
    eos_card_pager_t *cp = _app_list_pager;
    if (!cp)
    {
        return;
    }
    bool next = (bool)(intptr_t)lv_event_get_user_data(e);
    if (next && cp->current_page_index < cp->page_count - 1)
    {
        eos_card_pager_move_page(cp, cp->current_page_index + 1);
    }
    else if (!next && cp->current_page_index > 0)
    {
        eos_card_pager_move_page(cp, cp->current_page_index - 1);
    }
}

static lv_obj_t *_app_arrow_create(lv_obj_t *parent, bool next)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, _APP_ARROW_SIZE, _APP_ARROW_SIZE);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(btn, next ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, -_APP_ARROW_PAD_X, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, next ? RI_ARROW_RIGHT_S_LINE : RI_ARROW_LEFT_S_LINE);
    lv_obj_set_style_text_font(label, &EOS_FONT_ICON, 0);
    lv_obj_set_style_text_color(label, EOS_COLOR_WHITE, 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, _app_arrow_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)next);
    return btn;
}

/**
 * @brief Refresh the paged app list.
 * @param bubble_grid App list's page container object (card pager container)
 */
static void _app_list_refresh(lv_obj_t *bubble_grid)
{
    if (!bubble_grid)
    {
        return;
    }

    eos_card_pager_t *cp = _app_list_pager;
    if (!cp)
    {
        return;
    }

    /* Clear all existing pages (last -> first) and the icon cache. */
    while (cp->page_count > 0)
    {
        eos_card_pager_remove_page(cp, cp->page_count - 1);
    }
    for (uint32_t i = 0; i < _APP_GRID_MAX_ICONS; i++)
    {
        _app_icon_cache[i] = NULL;
    }

    uint32_t icon_index = 0;
    lv_obj_t *cur_page = NULL;

    // Load application order from config
    cJSON *app_order = eos_config_get_json(EOS_CONFIG_KEY_APP_ORDER_ARRAY);

    // Add icons according to JSON order
    if (app_order && cJSON_IsArray(app_order))
    {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, app_order)
        {
            if (!cJSON_IsString(item))
            {
                continue;
            }
            const char *order_id = item->valuestring;

            // System apps are shown in the control center, not in the launcher.
            bool is_sys = false;
            for (int si = 0; si < EOS_SYS_APP_LAST; si++)
            {
                if (strcmp(order_id, eos_sys_app_id_list[si]) == 0)
                {
                    is_sys = true;
                    break;
                }
            }
            if (is_sys)
            {
                continue;
            }

            // Non-system app: look up existing ID in installed list
            const char *app_id = eos_app_list_get_existing_id(order_id);
            if (!app_id)
            {
                continue;
            }

            char icon_path[EOS_FS_PATH_MAX];
            const void *resolved_icon = _app_list_resolve_icon(app_id, icon_path, sizeof(icon_path));
            _app_list_append_icon(cp, &cur_page, &icon_index, resolved_icon, app_id);
        }
        cJSON_Delete(app_order);
    }
    else
    {
        // If no JSON order file is found, add apps in default order
        size_t app_list_size = eos_app_get_installed();
        for (size_t i = 0; i < app_list_size; i++)
        {
            const char *app_id = eos_app_list_get_id(i);
            if (!app_id)
            {
                continue;
            }

            // System apps are shown in the control center, not in the launcher.
            bool is_sys = false;
            for (int si = 0; si < EOS_SYS_APP_LAST; si++)
            {
                if (strcmp(app_id, eos_sys_app_id_list[si]) == 0)
                {
                    is_sys = true;
                    break;
                }
            }
            if (is_sys)
            {
                continue;
            }

            // Non-system app
            char icon_path[EOS_FS_PATH_MAX];
            const void *resolved_icon = _app_list_resolve_icon(app_id, icon_path, sizeof(icon_path));
            _app_list_append_icon(cp, &cur_page, &icon_index, resolved_icon, app_id);
        }
    }

    _app_list_icon_count = icon_index;
    _app_list_update_arrows();
}

/************************** Animation **************************/

static void _app_list_open_app_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to)
{
    _app_list_play_transition_anim(at, from, to, true);
}

static void _app_list_close_app_anim_cb(lv_anim_timeline_t *at, eos_activity_t *from, eos_activity_t *to)
{
    _app_list_play_transition_anim(at, from, to, false);
}

/************************** Helper Functions **************************/

/**
 * @brief This callback is automatically called when an app is installed to display the new app
 */
static void _app_installed_cb(eos_event_t *e)
{
    lv_obj_t *bubble_grid = eos_event_get_obj(e);
    EOS_CHECK_PTR_RETURN(bubble_grid);
    _app_list_refresh(bubble_grid);
}

static void _app_uninstalled_cb(eos_event_t *e)
{
    lv_obj_t *bubble_grid = eos_event_get_obj(e);
    EOS_CHECK_PTR_RETURN(bubble_grid);
    _app_list_refresh(bubble_grid);
}

static void _container_delete_cb(lv_event_t *e)
{
    lv_obj_t *bubble_grid = lv_event_get_target(e);
    EOS_CHECK_PTR_RETURN(bubble_grid);
    eos_event_unsubscribe_with_obj(EOS_EVENT_APP_INSTALLED, _app_installed_cb, bubble_grid);
    eos_event_unsubscribe_with_obj(EOS_EVENT_APP_UNINSTALLED, _app_uninstalled_cb, bubble_grid);
    _app_list_pager = NULL;
    _app_arrow_prev = NULL;
    _app_arrow_next = NULL;
    for (uint32_t i = 0; i < _APP_GRID_MAX_ICONS; i++)
    {
        _app_icon_cache[i] = NULL;
    }
}

/**
 * @brief Swipe-back handler for the app list activity.
 * @return true (consume): pager pages handle right-swipe internally; on the
 *         first page the framework would otherwise pop the activity.
 */
static bool _app_list_on_swipe_back(eos_activity_t *activity, lv_dir_t dir)
{
    if (dir != LV_DIR_RIGHT)
    {
        return false;
    }

    eos_card_pager_t *cp = _app_list_pager;
    if (cp && cp->current_page_index > 0)
    {
        /* Swipe back to the previous page: the card pager already animated
         * the page move on release, just consume the gesture. */
        return true;
    }

    /* First page: go back to the watch face. */
    eos_activity_back();
    return true;
}

void eos_app_list_enter(void)
{
#ifdef EOS_USE_CUSTOM_LAUNCHER
    /* The adaptive framework Home (ArcList on round, LayoutManager grid /
       Apple bubble on square) replaces the default bubble_grid app list. The
       watchface remains the home/root activity. */
    eos_launcher_enter();
    return;
#endif
    _register_anim_routes_once();
    _app_list_icon_count = 0;

    eos_activity_t *a = eos_activity_create(&app_list_lifecycle);
    if (!a)
    {
        EOS_LOG_E("Failed to create activity");
        return;
    }
    eos_activity_set_type(a, EOS_ACTIVITY_TYPE_APP_LIST);

    lv_obj_t *view = eos_activity_get_view(a);
    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    /* Round-display clip: keep the bubble grid inside the circular watch face
     * and paint the off-circle corners black (otherwise icons spill past the
     * round edge on the square framebuffer). */
    eos_round_clip(view);
    /* Opaque background: the app-list view sits on top of the watchface.
     * Without a solid bg the watchface shows through and the page looks
     * "half black / half white". */
    lv_obj_set_style_bg_color(view, EOS_COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);

    /* Paged app grid (card pager) as the app list container. Every page shows
     * up to 6 icons (2 rows x 3 cols); swiping left reveals the next page. */
    eos_card_pager_t *pager = eos_card_pager_create(view, EOS_CARD_PAGER_DIR_HOR);
    if (!pager)
    {
        EOS_LOG_E("Failed to create card pager");
        eos_activity_back();
        return;
    }
    lv_obj_t *bubble_grid = pager->container;
    lv_obj_set_size(bubble_grid, EOS_DISPLAY_WIDTH, EOS_DISPLAY_HEIGHT);
    lv_obj_center(bubble_grid);
    _app_list_pager = pager;
    eos_activity_set_user_data(a, bubble_grid);

    /* Pager arrow navigation (left/right, created last so they sit on top of
     * the pages). */
    _app_arrow_prev = _app_arrow_create(view, false);
    _app_arrow_next = _app_arrow_create(view, true);
    eos_card_pager_set_page_changed_cb(pager, _app_list_page_changed_cb, NULL);
    _app_list_update_arrows();

    // Swipe-back: page 1+ consumes right-swipe (go back one page, handled by
    // the pager itself); page 0 lets the framework exit to the watch face.
    eos_activity_set_swipe_back_handler(a, _app_list_on_swipe_back);

    // Set callback
    lv_obj_add_event_cb(bubble_grid, _container_delete_cb, LV_EVENT_DELETE, NULL);
    eos_event_subscribe_ex(EOS_EVENT_APP_INSTALLED, _app_installed_cb, NULL, bubble_grid);
    eos_event_subscribe_ex(EOS_EVENT_APP_UNINSTALLED, _app_uninstalled_cb, NULL, bubble_grid);

    // Refresh app list
    _app_list_refresh(bubble_grid);

    eos_activity_enter(a);
}
