/**
 * @file cos_app.c
 * @brief Application system
 */

#include "cos_app.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cos_port.h"
#define COS_LOG_TAG "App"
#include "cos_log.h"
#include "cos_pkg_mgr.h"
#include "cos_event.h"
#include "script_engine_core.h"
#include "cJSON.h"
#include "cos_app_list.h"
#include "cos_service_storage.h"
#include "cos_mem.h"
#include "cos_activity.h"
#include "cos_lang.h"
#include "cos_theme.h"
#include "cos_std_widgets.h"
#include "cos_accordion.h"
#include "cos_basic_widgets.h"
#include "cos_icon.h"
#include "cos_panel.h"
#include "cos_service_permission.h"
#include "cos_fault_panel.h"
#include "cos_font.h"
#include "sni_callback_runtime.h"
#include "spm.h"
#include "cos_version.h"

/* Macros and Definitions -------------------------------------*/
#define COS_APP_LIST_DEFAULT_CAPACITY 1 // Default capacity of application list
/**
 * @brief Application list structure
 *
 * Dynamic array
 */
typedef struct
{
    char **data; /**< Application unique ID */
    size_t size; /**< Number of IDs stored in application list */
    size_t capacity; /**< Capacity of application list */
} cos_app_list_t;
static cos_app_list_t app_list;
static bool app_list_initialized = false;
/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

// Application order functions
static cos_result_t _cos_app_order_save(void);
static cos_result_t _cos_app_order_load(void);
static cos_result_t _cos_app_order_add(const char *app_id);
static cos_result_t _cos_app_order_remove(const char *app_id);
static cos_result_t _cos_app_disabled_save(void);
static cos_result_t _cos_app_disabled_load(void);
static bool _cos_app_disabled_contains(const char *app_id);
static void _cos_app_disabled_remove(const char *app_id);

// App delete callbacks
static void _app_delete_cb(lv_event_t *e);
static void _app_delete_cos_cb(cos_event_t *e);

// Application order list
static cJSON *app_order_json = NULL;
static cJSON *app_disabled_json = NULL;

/**
 * @brief Add application order save function
 */
static cos_result_t _cos_app_order_save(void)
{
    if (!app_order_json)
    {
        return COS_FAILED;
    }

    // Create a copy of the array for saving to config
    cJSON *app_order_copy = cJSON_Duplicate(app_order_json, true);
    if (!app_order_copy)
    {
        return COS_FAILED;
    }

    cos_result_t ret = cos_config_set_json(COS_CONFIG_KEY_APP_ORDER_ARRAY, app_order_copy);

    return ret;
}

/**
 * @brief Add application order load function
 */
static cos_result_t _cos_app_order_load(void)
{
    if (app_order_json)
    {
        cJSON_Delete(app_order_json);
        app_order_json = NULL;
    }

    // Try to load app_order from config
    app_order_json = cos_config_get_json(COS_CONFIG_KEY_APP_ORDER_ARRAY);

    if (!app_order_json || !cJSON_IsArray(app_order_json))
    {
        // If not found or not an array, create default
        if (app_order_json)
        {
            cJSON_Delete(app_order_json);
        }

        app_order_json = cJSON_CreateArray();
        for (int i = 0; i < COS_SYS_APP_LAST; i++)
        {
            if (cos_sys_app_id_list[i])
                cJSON_AddItemToArray(app_order_json, cJSON_CreateString(cos_sys_app_id_list[i]));
        }
        return _cos_app_order_save();
    }

    // Ensure all system built-in apps exist in order list
    for (int si = 0; si < COS_SYS_APP_LAST; si++)
    {
        const char *sys_id = cos_sys_app_id_list[si];
        if (!sys_id)
            continue;
        bool has_sys = false;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, app_order_json)
        {
            if (cJSON_IsString(item) && strcmp(item->valuestring, sys_id) == 0)
            {
                has_sys = true;
                break;
            }
        }
        if (!has_sys)
        {
            cJSON_AddItemToArray(app_order_json, cJSON_CreateString(sys_id));
            _cos_app_order_save();
        }
    }

    return COS_OK;
}

// Add application to order list
static cos_result_t _cos_app_order_add(const char *app_id)
{
    if (!app_order_json || !app_id)
    {
        return COS_FAILED;
    }

    // Check if already exists
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, app_order_json)
    {
        if (cJSON_IsString(item) && strcmp(item->valuestring, app_id) == 0)
        {
            return COS_OK; // Already exists
        }
    }

    // Add to end of array
    cJSON_AddItemToArray(app_order_json, cJSON_CreateString(app_id));
    return _cos_app_order_save();
}

// Remove application from order list
static cos_result_t _cos_app_order_remove(const char *app_id)
{
    if (!app_order_json || !app_id)
    {
        return COS_FAILED;
    }

    // System built-in apps cannot be removed
    for (int si = 0; si < COS_SYS_APP_LAST; si++)
    {
        if (cos_sys_app_id_list[si] && strcmp(app_id, cos_sys_app_id_list[si]) == 0)
        {
            return COS_OK;
        }
    }

    // Native C apps cannot be removed
    for (int ni = 0; ni < COS_NATIVE_APP_LAST; ni++)
    {
        if (cos_native_app_id_list[ni] && strcmp(app_id, cos_native_app_id_list[ni]) == 0)
        {
            return COS_OK;
        }
    }

    // Find application position in array
    int index = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, app_order_json)
    {
        if (cJSON_IsString(item) && strcmp(item->valuestring, app_id) == 0)
        {
            // Delete element using index
            cJSON_DeleteItemFromArray(app_order_json, index);
            return _cos_app_order_save();
        }
        index++;
    }

    return COS_OK; // Not found is also considered success
}

/* ---- App disabled set (persisted in config COS_CONFIG_KEY_APP_DISABLED) ---- */

static cos_result_t _cos_app_disabled_save(void)
{
    if (!app_disabled_json)
        return COS_OK;
    cJSON *copy = cJSON_Duplicate(app_disabled_json, true);
    if (!copy)
        return COS_FAILED;
    /* cos_config_set_json takes ownership of the copy and frees it */
    return cos_config_set_json(COS_CONFIG_KEY_APP_DISABLED, copy);
}

static cos_result_t _cos_app_disabled_load(void)
{
    if (app_disabled_json)
    {
        cJSON_Delete(app_disabled_json);
        app_disabled_json = NULL;
    }
    app_disabled_json = cos_config_get_json(COS_CONFIG_KEY_APP_DISABLED);
    if (!app_disabled_json || !cJSON_IsArray(app_disabled_json))
    {
        if (app_disabled_json)
            cJSON_Delete(app_disabled_json);
        app_disabled_json = cJSON_CreateArray();
    }
    return COS_OK;
}

static bool _cos_app_disabled_contains(const char *app_id)
{
    if (!app_disabled_json || !app_id)
        return false;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, app_disabled_json)
    {
        if (cJSON_IsString(item) && strcmp(item->valuestring, app_id) == 0)
            return true;
    }
    return false;
}

static void _cos_app_disabled_remove(const char *app_id)
{
    if (!app_disabled_json || !app_id)
        return;
    cJSON *item = NULL;
    int index = 0;
    cJSON_ArrayForEach(item, app_disabled_json)
    {
        if (cJSON_IsString(item) && strcmp(item->valuestring, app_id) == 0)
        {
            cJSON_DeleteItemFromArray(app_disabled_json, index);
            _cos_app_disabled_save();
            return;
        }
        index++;
    }
}

cos_result_t cos_app_disable(const char *app_id)
{
    if (!app_id)
        return COS_ERR_VAR_NULL;
    if (_cos_app_disabled_contains(app_id))
        return COS_OK;
    if (!app_disabled_json)
        app_disabled_json = cJSON_CreateArray();
    cJSON_AddItemToArray(app_disabled_json, cJSON_CreateString(app_id));
    cos_result_t r = _cos_app_disabled_save();
    COS_LOG_I("App disabled: %s", app_id);
    return r;
}

cos_result_t cos_app_enable(const char *app_id)
{
    if (!app_id)
        return COS_ERR_VAR_NULL;
    _cos_app_disabled_remove(app_id);
    COS_LOG_I("App enabled: %s", app_id);
    return COS_OK;
}

bool cos_app_is_disabled(const char *app_id)
{
    return _cos_app_disabled_contains(app_id);
}

// Move application to specified position
cos_result_t cos_app_order_move(const char *app_id, size_t new_index)
{
    if (!app_order_json || !app_id)
    {
        COS_LOG_E("input NULL");
        return COS_FAILED;
    }

    // Get current array size
    size_t array_size = cJSON_GetArraySize(app_order_json);
    if (new_index >= array_size)
    {
        COS_LOG_E("Out of index");
        return COS_FAILED; // Index out of range
    }

    // Find current position of application in array
    int current_index = -1;
    cJSON *item = NULL;
    for (int i = 0; i < array_size; i++)
    {
        item = cJSON_GetArrayItem(app_order_json, i);
        if (cJSON_IsString(item) && strcmp(item->valuestring, app_id) == 0)
        {
            current_index = i;
            break;
        }
    }

    if (current_index == -1)
    {
        COS_LOG_E("App not found");
        return COS_FAILED; // Application not found
    }

    // If already at specified position, return directly
    if (current_index == new_index)
    {
        COS_LOG_D("App already in target index");
        return COS_OK;
    }

    // Remove application
    cJSON *app_item = cJSON_DetachItemFromArray(app_order_json, current_index);

    // Insert at new position
    if (new_index < array_size - 1)
    {
        cJSON_InsertItemInArray(app_order_json, new_index, app_item);
    }
    else
    {
        cJSON_AddItemToArray(app_order_json, app_item);
    }

    return _cos_app_order_save();
}

uint32_t cos_app_get_installed(void)
{
    return app_list.size;
}

const char *cos_app_list_get_id(size_t index)
{
    if (index >= app_list.size)
    {
        COS_LOG_E("Index out of bounds: %zu", index);
        return NULL;
    }
    return app_list.data[index];
}

bool cos_app_list_contains(const char *app_id)
{
    for (size_t i = 0; i < app_list.size; i++)
    {
        if (strcmp(app_list.data[i], app_id) == 0)
        {
            return true;
        }
    }
    return false;
}

const char *cos_app_list_get_existing_id(const char *id)
{
    for (size_t i = 0; i < app_list.size; i++)
    {
        if (strcmp(app_list.data[i], id) == 0)
        {
            return app_list.data[i];
        }
    }
    return NULL;
}

/**
 * @brief Initialize application list
 */
void _cos_app_list_init(cos_app_list_t *list, size_t capacity)
{
    list->data = cos_malloc(capacity * sizeof(char *));
    list->size = 0;
    list->capacity = capacity;
}

/**
 * @brief Add new application to application list
 */
void _cos_app_list_add(cos_app_list_t *list, const char *id)
{
    if (list->size == list->capacity)
    {
        list->capacity *= 2;
        list->data = cos_realloc(list->data, list->capacity * sizeof(char *));
    }
    list->data[list->size] = cos_strdup(id); // Copy string
    list->size++;
}

/**
 * @brief Free list data
 */
void _cos_app_list_free(cos_app_list_t *list)
{
    for (size_t i = 0; i < list->size; i++)
    {
        cos_free(list->data[i]);
    }
    cos_free(list->data);
}

/**
 * @brief Get installed applications from Flash
 */
cos_result_t _cos_app_list_get_installed(void)
{
    cos_dir_t dir;
    char name_buf[256];

    // Open application installation directory
    dir = cos_storage_dir_open(COS_APP_INSTALLED_DIR);
    if (dir == NULL)
    {
        COS_LOG_E("Failed to open app dir: %s", COS_APP_INSTALLED_DIR);
        return COS_FAILED;
    }

    // Iterate through all entries in the directory
    while (cos_storage_dir_read(dir, name_buf, sizeof(name_buf)) == COS_OK)
    {
        // Skip "." and ".." directories
        if (strcmp(name_buf, ".") == 0 || strcmp(name_buf, "..") == 0)
        {
            continue;
        }

        // Build full path
        char full_path[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
        snprintf(full_path, sizeof(full_path), COS_APP_INSTALLED_DIR "%s", name_buf);

        // Check if it is a directory
        if (cos_storage_is_dir(full_path))
        {
            COS_LOG_D("Found installed app: %s", name_buf);
            _cos_app_list_add(&app_list, name_buf);
        }
    }

    cos_storage_dir_close(dir);

    COS_LOG_I("Loaded %zu installed apps", app_list.size);
    return COS_OK;
}

/**
 * @brief Refresh application list
 */
cos_result_t _cos_app_list_refresh()
{
    memset(&app_list, 0, sizeof(app_list));
    _cos_app_list_init(&app_list, COS_APP_LIST_DEFAULT_CAPACITY);
    if (_cos_app_list_get_installed() != COS_OK)
    {
        COS_LOG_E("Get installed app failed");
        return COS_FAILED;
    }

    // Add system built-in apps to app_list
    for (int i = 0; i < COS_SYS_APP_LAST; i++)
    {
        const char *sys_id = cos_sys_app_id_list[i];
        if (!cos_app_list_contains(sys_id))
        {
            _cos_app_list_add(&app_list, sys_id);
        }
    }

    // Add native C apps (compiled into firmware) to app_list
    for (int i = 0; i < COS_NATIVE_APP_LAST; i++)
    {
        const char *native_id = cos_native_app_id_list[i];
        if (!cos_app_list_contains(native_id))
        {
            _cos_app_list_add(&app_list, native_id);
        }
    }

    return COS_OK;
}

cos_result_t cos_app_install(const char *eapk_path)
{
    COS_CHECK_PTR_RETURN_VAL(eapk_path, COS_ERR_VAR_NULL);
    // Get package header
    cos_pkg_header_t header;
    if (cos_pkg_read_header(eapk_path, &header) != COS_OK)
    {
        COS_LOG_E("Read header failed: %s", eapk_path);
        return COS_FAILED;
    }
    if (!cos_storage_is_valid_filename(header.pkg_id))
    {
        COS_LOG_E("Invalid package id");
        return COS_FAILED;
    }
    if (header.min_api_level > CANTOMK6_OS_API_LEVEL)
    {
        COS_LOG_E("App '%s' requires API level %d, OS supports %d",
                  header.pkg_id,
                  header.min_api_level,
                  CANTOMK6_OS_API_LEVEL);
        return COS_ERR_SDK_VERSION;
    }
    // Concatenate path
    char path[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
    snprintf(path, sizeof(path), COS_APP_INSTALLED_DIR "%s", header.pkg_id);
    char data_path[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
    snprintf(data_path, sizeof(data_path), COS_APP_DATA_DIR "%s", header.pkg_id);
    COS_LOG_D("APP_PATH: %s", path);
    // Check if app exists
    if (cos_storage_is_dir(path))
    {
        // If exists, delete it
        cos_storage_rm_recursive(path);
    }
    // Create app directory
    if (cos_storage_mkdir_if_not_exist(path) == COS_OK)
    {
        COS_LOG_I("Created dir: %s\n", path);
    }
    else
    {
        COS_LOG_E("Failed mkdir: %s\n", path);
    }
    // Install application
    script_pkg_type_t type = SCRIPT_TYPE_APPLICATION;
    cos_result_t ret = cos_pkg_mgr_unpack(eapk_path, path, type);
    if (ret != COS_OK)
    {
        COS_LOG_E("App unpack failed. Code: %d", ret);
        cos_storage_rm_recursive(path);
        return COS_FAILED;
    }
    cos_storage_mkdir_if_not_exist(data_path);
    // Add to order list
    _cos_app_order_add(header.pkg_id);
    _cos_app_list_refresh();
    COS_LOG_D("App installed successfully: %s", header.pkg_name);
    const char *app_id = cos_app_list_get_existing_id(header.pkg_id);
    COS_LOG_D("app_id=%s\npkg_id=%s", app_id, header.pkg_id);
    cos_event_post(COS_EVENT_APP_INSTALLED, (void *)app_id, NULL);
    return COS_OK;
}

cos_result_t cos_app_uninstall(const char *app_id)
{
    COS_LOG_D("Uninstall: %s", app_id);

    // Native C apps (compiled into firmware) cannot be uninstalled
    for (int ni = 0; ni < COS_NATIVE_APP_LAST; ni++)
    {
        if (strcmp(app_id, cos_native_app_id_list[ni]) == 0)
        {
            COS_LOG_W("Native app cannot be uninstalled: %s", app_id);
            return COS_FAILED;
        }
    }

    char path[COS_FS_PATH_MAX];
    snprintf(path, sizeof(path), COS_APP_INSTALLED_DIR "%s", app_id);
    char data_path[COS_FS_PATH_MAX];
    snprintf(data_path, sizeof(data_path), COS_APP_DATA_DIR "%s", app_id);

    if (!cos_storage_is_dir(path))
    {
        COS_LOG_E("App does not exist: %s", app_id);
        return COS_FAILED;
    }

    cos_result_t ret = cos_storage_rm_recursive(path);

    if (ret != COS_OK)
    {
        COS_LOG_E("Uninstall failed, code: %d", ret);
        return COS_FAILED;
    }

    if (cos_storage_is_dir(data_path))
    {
        ret = cos_storage_rm_recursive(data_path);
        if (ret != COS_OK)
        {
            COS_LOG_E("Remove data failed, code: %d", ret);
        }
    }

    _cos_app_order_remove(app_id);

    /* Drop from disabled set if present */
    _cos_app_disabled_remove(app_id);
    _cos_app_list_refresh();

    /* Clean up permission grants for this app */
    cos_permission_revoke_all(app_id);

    cos_event_post(COS_EVENT_APP_UNINSTALLED, (void *)app_id, NULL);

    COS_LOG_D("App uninstalled successfully: %s", app_id);
    return COS_OK;
}

static void _app_delete_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    const char *obj_app_id = lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(obj);
    COS_LOG_D("_app_delete_cb target obj=%p", obj);
    lv_obj_remove_event_cb(obj, _app_delete_cb);
    cos_event_unsubscribe(COS_EVENT_APP_UNINSTALLED, _app_delete_cos_cb);
}

static void _app_delete_cos_cb(cos_event_t *e)
{
    const char *deleted_app_id = cos_event_get_param(e);
    lv_obj_t *obj = cos_event_get_obj(e);
    const char *obj_app_id = cos_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(obj);
    COS_LOG_D("_app_delete_cos_cb target obj=%p", obj);
    if (strcmp(deleted_app_id, obj_app_id) == 0)
    {
        lv_obj_remove_event_cb(obj, _app_delete_cb);
        cos_event_unsubscribe(COS_EVENT_APP_UNINSTALLED, _app_delete_cos_cb);
        lv_obj_delete_async(obj);
    }
}

void cos_app_obj_auto_delete(lv_obj_t *obj, const char *app_id)
{
    COS_CHECK_PTR_RETURN(obj);
    COS_LOG_D("Auto del regesited: %s, ptr: %p", app_id, obj);
    lv_obj_add_event_cb(obj, _app_delete_cb, LV_EVENT_DELETE, (void *)app_id);
    cos_event_subscribe_ex(COS_EVENT_APP_UNINSTALLED, _app_delete_cos_cb, (void *)app_id, obj);
}

const char *cos_app_get_name(const char *id)
{
    static char buf[64];
    if (!id)
        return "App";
    char path[COS_FS_PATH_MAX];
    snprintf(path, sizeof(path), COS_APP_INSTALLED_DIR "%s/" COS_APP_MANIFEST_FILE_NAME, id);
    script_pkg_t pkg = {0};
    if (script_engine_get_manifest(path, &pkg) != COS_OK)
        return "App";
    const char *n = pkg.name ? pkg.name : "App";
    snprintf(buf, sizeof(buf), "%s", n);
    cos_pkg_free(&pkg);
    return buf;
}

cos_result_t cos_app_init(void)
{
    COS_LOG_D("Init cos_app");
    // Initialize - read application list from file system
    _cos_app_list_refresh();

    // Load application order
    _cos_app_order_load();

    // Load disabled-app set
    _cos_app_disabled_load();

    // Clean up non-existent applications in JSON
    if (app_order_json)
    {
        cJSON *item = NULL;
        int index = 0;
        cJSON_ArrayForEach(item, app_order_json)
        {
            if (cJSON_IsString(item))
            {
                const char *app_id = item->valuestring;
                // Skip all system built-in applications
                bool is_sys = false;
                for (int si = 0; si < COS_SYS_APP_LAST; si++)
                {
                    if (cos_sys_app_id_list[si] && strcmp(app_id, cos_sys_app_id_list[si]) == 0)
                    {
                        is_sys = true;
                        break;
                    }
                }

                if (!is_sys && !cos_app_list_contains(app_id))
                {
                    // Application does not exist, remove from JSON
                    cJSON_DeleteItemFromArray(app_order_json, index);
                    _cos_app_order_save();
                    // Since an element was deleted, need to re-traverse
                    break;
                }
            }
            index++;
        }

        // Automatically add installed applications not in app_order to order list (append to end)
        for (size_t i = 0; i < app_list.size; i++)
        {
            const char *installed_id = app_list.data[i];
            if (installed_id == NULL)
                continue;
            // _cos_app_order_add will skip existing ids internally
            _cos_app_order_add(installed_id);
        }
    }

    return COS_OK;
}

static const char *_cos_script_error_get_reason(cos_script_error_type_t error_type)
{
    switch (error_type)
    {
        case COS_SCRIPT_FAULT_ERROR_EXCEPTION:
            return "Runtime exception";
        case COS_SCRIPT_FAULT_ERROR_PARSE:
            return "Parse error";
        case COS_SCRIPT_FAULT_ERROR_MODULE_LINK:
            return "Module link error";
        case COS_SCRIPT_FAULT_UNRESPONSIVE:
            return "Unresponsive";
        case COS_SCRIPT_FAULT_ENGINE_CRASH:
            return "Engine crash";
        default:
            return "Unknown error";
    }
}

void cos_app_handle_script_error(cos_script_error_type_t error_type,
                                 cos_result_t error_code,
                                 const char *app_id,
                                 const cos_script_error_handler_cfg_t *cfg)
{
    COS_LOG_E("Script error handling - type:%d, code:%d, app_id:%s", error_type, error_code, app_id ? app_id : "NULL");

    const char *reason = _cos_script_error_get_reason(error_type);

    cos_fault_cfg_t fault_cfg = {
        .icon_type = COS_FAULT_ICON_BUG,
        .icon_color = COS_COLOR_RED,
        .title_id = STR_ID_ERROR,
        .message_text = cos_lang_get_text(STR_ID_APP_RUN_ERR),
        .confirm_btn_id = 0,
        .confirm_btn_text = NULL,
        .confirm_cb = NULL,
        .cancel_btn_id = cfg && cfg->button_id != 0 ? cfg->button_id : STR_ID_BACK,
        .cancel_btn_text = cfg && cfg->button_text ? cfg->button_text : NULL,
        .cancel_cb = cfg && cfg->button_callback ? cfg->button_callback : NULL,
    };

    cos_activity_t *current = cos_activity_get_current();
    if (!current)
    {
        COS_LOG_E("No current activity for fault panel");
        return;
    }

    cos_fault_panel_t *old_fp = (cos_fault_panel_t *)cos_activity_get_fault_panel(current);
    if (old_fp)
    {
        COS_LOG_W("Replacing existing fault panel on activity");
        cos_fault_panel_delete(old_fp);
        cos_activity_set_fault_panel(current, NULL);
    }

    cos_fault_panel_t *fault_panel = cos_fault_panel_create_on_activity(current, &fault_cfg);
    if (!fault_panel)
    {
        COS_LOG_E("Failed to create fault panel");
        return;
    }

    cos_activity_set_fault_panel(current, fault_panel);

    lv_obj_t *extra_slot = cos_fault_panel_get_extra_slot(fault_panel);
    if (extra_slot)
    {
        const spm_error_t *last_error = spm_get_last_error();
        const char *error_info = last_error ? last_error->error_info : script_engine_get_error_info();
        const char *id_text = app_id ? app_id : "Unknown";

        /* Build the message on the heap with the exact required size so it is
         * never truncated regardless of the error string lengths. */
        char *info_str = NULL;
        if (error_info && error_info[0] != '\0')
        {
            int info_len = snprintf(NULL, 0,
                                    "Code: %d\nID: %s\nErrorType: %s\nError: %s",
                                    error_code, id_text, reason, error_info);
            if (info_len > 0)
            {
                info_str = (char *)cos_malloc((size_t)info_len + 1);
                if (info_str)
                {
                    snprintf(info_str, (size_t)info_len + 1,
                             "Code: %d\nID: %s\nErrorType: %s\nError: %s",
                             error_code, id_text, reason, error_info);
                }
            }
        }
        else
        {
            int info_len = snprintf(NULL, 0,
                                    "Code: %d\nID: %s\nErrorType: %s",
                                    error_code, id_text, reason);
            if (info_len > 0)
            {
                info_str = (char *)cos_malloc((size_t)info_len + 1);
                if (info_str)
                {
                    snprintf(info_str, (size_t)info_len + 1,
                             "Code: %d\nID: %s\nErrorType: %s",
                             error_code, id_text, reason);
                }
            }
        }

        lv_obj_t *err_label = lv_label_create(extra_slot);
        if (info_str)
        {
            lv_label_set_text(err_label, info_str);
            cos_free(info_str);
        }
        lv_obj_set_width(err_label, COS_PANEL_CONTENT_WIDTH);
        lv_label_set_long_mode(err_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(err_label, COS_COLOR_GREY_1, 0);
        cos_label_set_font_size(err_label, COS_FONT_SIZE_SMALL);

        const script_error_location_t *error_location = NULL;
        uint32_t backtrace_count = 0;
        const script_error_location_t *backtrace = NULL;

        if (last_error)
        {
            error_location = &last_error->error_location;
            backtrace_count = last_error->backtrace_count;
            backtrace = last_error->backtrace;
        }
        else
        {
            error_location = script_engine_get_error_location();
            backtrace_count = script_engine_get_backtrace_count();
            if (backtrace_count > 0)
            {
                backtrace = script_engine_get_error_backtrace(NULL);
            }
        }

        if (error_location && error_location->source_name[0] != '\0')
        {
            char location_str[256];
            snprintf(location_str,
                     sizeof(location_str),
                     "Location: %s:%u:%u",
                     error_location->source_name,
                     error_location->line,
                     error_location->column);

            lv_obj_t *location_label = lv_label_create(extra_slot);
            lv_label_set_text(location_label, location_str);
            lv_obj_set_width(location_label, COS_PANEL_CONTENT_WIDTH);
            lv_label_set_long_mode(location_label, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(location_label, COS_COLOR_GREY_1, 0);
            cos_label_set_font_size(location_label, COS_FONT_SIZE_SMALL);
        }

        if (backtrace_count > 0)
        {
            if (backtrace)
            {
                cos_accordion_t *accordion =
                    cos_accordion_create(extra_slot, cos_lang_get_text(STR_ID_APP_RUN_ERR_BACKTRACE));
                lv_obj_t *accordion_content = accordion->content;
                lv_obj_t *backtrace_label = lv_label_create(accordion_content);

                lv_obj_set_style_text_color(accordion->title_label, COS_COLOR_GREY_1, 0);
                lv_obj_set_style_text_color(accordion->arrow_label, COS_COLOR_GREY_1, 0);
                lv_obj_set_style_text_color(backtrace_label, COS_COLOR_GREY_1, 0);

                cos_label_set_font_size(accordion->title_label, COS_FONT_SIZE_SMALL);
                cos_label_set_font_size(accordion->arrow_label, COS_FONT_SIZE_SMALL);
                cos_label_set_font_size(backtrace_label, COS_FONT_SIZE_SMALL);

                char backtrace_str[1024] = {0};
                char temp_str[256];
                for (uint32_t i = 0; i < backtrace_count; i++)
                {
                    snprintf(temp_str,
                             sizeof(temp_str),
                             "#%u: %s:%u:%u\n",
                             i,
                             backtrace[i].source_name,
                             backtrace[i].line,
                             backtrace[i].column);
                    strncat(backtrace_str, temp_str, sizeof(backtrace_str) - strlen(backtrace_str) - 1);
                }
                lv_label_set_text(backtrace_label, backtrace_str);
            }
        }
    }
}
