/**
 * @file cos_watchface_js.c
 * @brief JavaScript script watchface implementation
 */

#include "cos_watchface_js.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#define COS_LOG_TAG "WFJS"
#include "cos_log.h"
#include "spm.h"
#include "cos_app.h"
#include "cos_theme.h"
#include "cos_mem.h"
#include "cos_service_storage.h"
#include "cos_watchface_list.h"
#include "cos_pkg_mgr.h"
#include "cos_msg_list.h"
#include "cos_control_center.h"

/* Function Prototypes ---------------------------------------*/
static void _js_on_enter(cos_activity_t *activity);
static void _js_on_pause(cos_activity_t *activity);
static void _js_on_resume(cos_activity_t *activity);
static void _js_on_destroy(cos_activity_t *activity);
static script_pkg_t _js_load_package_from_disk(const char *watchface_id);
static void _js_handle_error(cos_watchface_instance_t *self, int32_t error_code);
static void _js_long_pressed_cb(lv_event_t *e);

/* Static Variables ------------------------------------------*/

/**
 * @brief Activity lifecycle callbacks for JS watchface
 */
static const cos_activity_lifecycle_t _js_lifecycle = {
    .on_enter = _js_on_enter,
    .on_pause = _js_on_pause,
    .on_resume = _js_on_resume,
    .on_destroy = _js_on_destroy,
};

/* Function Implementations -----------------------------------*/

cos_watchface_instance_t *cos_watchface_js_create(const char *watchface_id)
{
    if (!watchface_id)
    {
        COS_LOG_E("watchface_id is NULL");
        return NULL;
    }

    script_pkg_t pkg = _js_load_package_from_disk(watchface_id);
    if (!pkg.script_str)
    {
        COS_LOG_E("Failed to load JS watchface package: %s", watchface_id);
        return NULL;
    }

    cos_watchface_instance_t *instance = cos_malloc(sizeof(cos_watchface_instance_t));
    if (!instance)
    {
        COS_LOG_E("Failed to allocate JS watchface instance");
        cos_pkg_free(&pkg);
        return NULL;
    }

    memset(instance, 0, sizeof(*instance));
    instance->type = COS_WATCHFACE_TYPE_JS;
    snprintf(instance->id, sizeof(instance->id), "%s", watchface_id);
    instance->lifecycle = &_js_lifecycle;
    instance->data.js.pkg = pkg;

    instance->activity = cos_activity_create_root(&_js_lifecycle);
    if (!instance->activity)
    {
        COS_LOG_E("Failed to create activity for JS watchface");
        cos_pkg_free(&instance->data.js.pkg);
        cos_free(instance);
        return NULL;
    }

    cos_activity_set_type(instance->activity, COS_ACTIVITY_TYPE_WATCHFACE);
    cos_activity_set_user_data(instance->activity, instance);

    COS_LOG_I("Created JS watchface instance: %s", watchface_id);
    return instance;
}

static void _js_on_enter(cos_activity_t *activity)
{
    COS_LOG_I("JS watchface '%s': enter", ((cos_watchface_instance_t *)cos_activity_get_user_data(activity))->id);

    cos_watchface_instance_t *self = cos_activity_get_user_data(activity);

    // View is auto-created by framework (cos_activity_create_root + controller_init/replace_root)
    lv_obj_t *view = cos_activity_get_view(activity);
    if (!view)
    {
        COS_LOG_E("JS watchface: view is NULL!");
        return;
    }

    lv_obj_add_event_cb(view, _js_long_pressed_cb, LV_EVENT_LONG_PRESSED, NULL);

    cos_result_t ret = spm_watchface_start(&self->data.js.pkg, view);
    if (ret != COS_OK)
    {
        _js_handle_error(self, ret);
    }

    cos_msg_list_show();
    cos_control_center_show();
}

static void _js_on_pause(cos_activity_t *activity)
{
    COS_LOG_I("JS watchface: pause");

    cos_control_center_hide();
    cos_msg_list_hide();

    spm_watchface_pause();
}

static void _js_on_resume(cos_activity_t *activity)
{
    COS_LOG_I("JS watchface: resume");

    cos_watchface_instance_t *self = cos_activity_get_user_data(activity);

    cos_result_t ret = spm_watchface_resume();
    if (ret != COS_OK)
    {
        COS_LOG_W("watchface_resume failed (%d), falling back to full reload", ret);

        lv_obj_t *view = cos_activity_get_view(activity);
        script_pkg_t pkg = _js_load_package_from_disk(self->id);
        if (pkg.script_str)
        {
            ret = spm_watchface_start(&pkg, view);
            if (ret != COS_OK)
            {
                _js_handle_error(self, ret);
            }
            cos_pkg_free(&pkg);
        }
        else
        {
            COS_LOG_E("JS watchface: failed to reload package on resume");
        }
    }

    cos_msg_list_show();
    cos_control_center_show();
}

static void _js_on_destroy(cos_activity_t *activity)
{
    COS_LOG_I("JS watchface: destroy");

    cos_watchface_instance_t *self = cos_activity_get_user_data(activity);

    spm_watchface_destroy();

    if (self)
    {
        cos_pkg_free(&self->data.js.pkg);
    }
}

static script_pkg_t _js_load_package_from_disk(const char *watchface_id)
{
    script_pkg_t pkg = {0};
    pkg.type = SCRIPT_TYPE_WATCHFACE;

    char manifest_path[COS_FS_PATH_MAX];
    snprintf(manifest_path,
             sizeof(manifest_path),
             COS_WATCHFACE_INSTALLED_DIR "%s/" COS_WATCHFACE_MANIFEST_FILE_NAME,
             watchface_id);

    if (script_engine_get_manifest(manifest_path, &pkg) != COS_OK)
    {
        COS_LOG_E("Read manifest failed: %s", manifest_path);
        return pkg;
    }

    char script_path[COS_FS_PATH_MAX];
    snprintf(script_path,
             sizeof(script_path),
             COS_WATCHFACE_INSTALLED_DIR "%s/" COS_WATCHFACE_SCRIPT_ENTRY_FILE_NAME,
             watchface_id);

    char base_path[COS_FS_PATH_MAX];
    snprintf(base_path, sizeof(base_path), COS_WATCHFACE_INSTALLED_DIR "%s/", watchface_id);
    pkg.base_path = cos_strdup(base_path);

    if (!cos_storage_is_file(script_path))
    {
        COS_LOG_E("Can't find script: %s", script_path);
        cos_pkg_free(&pkg);
        return (script_pkg_t){0};
    }

    pkg.script_str = cos_storage_read_file(script_path);
    return pkg;
}

static void _js_handle_error(cos_watchface_instance_t *self, int32_t error_code)
{
    const char *error_info = script_engine_get_error_info();
    cos_script_error_type_t error_type = COS_SCRIPT_FAULT_ERROR_EXCEPTION;

    if (error_info && strstr(error_info, "Engine crash"))
    {
        error_type = COS_SCRIPT_FAULT_ENGINE_CRASH;
    }
    else if (error_info && strstr(error_info, "timeout"))
    {
        error_type = COS_SCRIPT_FAULT_UNRESPONSIVE;
    }

    cos_script_error_handler_cfg_t cfg = {
        .title_id = STR_ID_WATCHFACE_RUN_ERR_TITLE,
        .button_id = STR_ID_WATCHFACE_SWITCH,
        .button_callback = _js_long_pressed_cb,
    };

    cos_app_handle_script_error(error_type, error_code, self->id, &cfg);
    COS_LOG_E("Watchface encountered a fatal error");
}

static void _js_long_pressed_cb(lv_event_t *e)
{
    cos_watchface_list_enter();
}
