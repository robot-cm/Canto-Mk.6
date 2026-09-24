/**
 * @file cos_watchface.c
 * @brief Watchface manager
 */

#include "cos_watchface.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COS_LOG_TAG "Watchface"
#include "cos_log.h"
#include "cos_pkg_mgr.h"
#include "cos_watchface_builtin.h"
#include "cos_watchface_js.h"
#include "cos_msg_list.h"
#include "cos_watchface_list.h"
#include "cos_theme.h"
#include "cos_control_center.h"
#include "cos_mem.h"
#include "cos_service_config.h"
#include "cos_service_storage.h"
#include "cos_activity.h"
#include "cos_version.h"

/* Macros and Definitions -------------------------------------*/
#define COS_WATCHFACE_LIST_DEFAULT_CAPACITY 1

/**
 * @brief Application structure
 */
typedef script_pkg_t cos_watchface_t;

typedef struct
{
    char **data;
    size_t size;
    size_t capacity;
} cos_watchface_list_t;

/* Variables --------------------------------------------------*/
static cos_watchface_list_t watchface_list;
static bool _is_watchface_initialized = false;
static cos_watchface_instance_t *_current_instance = NULL;
/* Function Implementations -----------------------------------*/
static void _switch_to_watchface(const char *watchface_id);

static void _show_global_ui(void)
{
    cos_msg_list_show();
    cos_control_center_show();
}

static void _hide_global_ui(void)
{
    cos_control_center_hide();
    cos_msg_list_hide();
}

size_t cos_watchface_list_size(void)
{
    return watchface_list.size;
}

const char *cos_watchface_list_get_id(size_t index)
{
    if (index >= watchface_list.size)
    {
        COS_LOG_E("Index out of bounds: %zu", index);
        return NULL;
    }
    return watchface_list.data[index];
}

bool cos_watchface_list_contains(const char *watchface_id)
{
    if (watchface_id && strcmp(watchface_id, COS_WATCHFACE_BUILTIN_FALLBACK_ID) == 0)
    {
        return true;
    }

    for (size_t i = 0; i < watchface_list.size; i++)
    {
        if (strcmp(watchface_list.data[i], watchface_id) == 0)
        {
            return true;
        }
    }
    return false;
}

void _cos_watchface_list_init(cos_watchface_list_t *list, size_t capacity)
{
    list->data = cos_malloc(capacity * sizeof(char *));
    list->size = 0;
    list->capacity = capacity;
}

void _cos_watchface_list_add(cos_watchface_list_t *list, const char *id)
{
    if (list->size == list->capacity)
    {
        list->capacity *= 2;
        list->data = cos_realloc(list->data, list->capacity * sizeof(char *));
    }
    list->data[list->size] = cos_strdup(id); // Copy string
    list->size++;
}

void _cos_watchface_list_free(cos_watchface_list_t *list)
{
    for (size_t i = 0; i < list->size; i++)
    {
        cos_free(list->data[i]);
    }
    cos_free(list->data);
}

cos_result_t _cos_watchface_list_get_installed()
{
    cos_dir_t dir;
    char name_buf[256];

    // Open application installation directory
    dir = cos_storage_dir_open(COS_WATCHFACE_INSTALLED_DIR);
    if (!dir)
    {
        COS_LOG_E("Failed to open watchface directory: %s", COS_WATCHFACE_INSTALLED_DIR);
        return COS_OK;
    }

    // Traverse all entries in the directory
    while (cos_storage_dir_read(dir, name_buf, sizeof(name_buf)) == COS_OK)
    {
        // Skip "." and ".." directories
        if (strcmp(name_buf, ".") == 0 || strcmp(name_buf, "..") == 0)
        {
            continue;
        }

        // Build full path
        char full_path[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
        snprintf(full_path, sizeof(full_path), COS_WATCHFACE_INSTALLED_DIR "%s", name_buf);

        // Check if it is a directory
        if (cos_storage_is_dir(full_path))
        {
            COS_LOG_D("Found installed watchface: %s", name_buf);
            // Add to application list
            _cos_watchface_list_add(&watchface_list, name_buf);
        }
    }

    cos_storage_dir_close(dir);
    COS_LOG_I("Loaded %zu installed watchfaces", watchface_list.size);
    return COS_OK;
}

cos_result_t _cos_watchface_list_refresh()
{
    memset(&watchface_list, 0, sizeof(watchface_list));
    _cos_watchface_list_init(&watchface_list, COS_WATCHFACE_LIST_DEFAULT_CAPACITY);
    _cos_watchface_list_add(&watchface_list, COS_WATCHFACE_BUILTIN_FALLBACK_ID);
    if (_cos_watchface_list_get_installed() != COS_OK)
    {
        COS_LOG_E("Get installed watchfaces failed");
    }
    return COS_OK;
}

cos_result_t cos_watchface_install(const char *eapk_path)
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
    if (strcmp(header.pkg_id, COS_WATCHFACE_BUILTIN_FALLBACK_ID) == 0)
    {
        COS_LOG_E("Builtin fallback watchface cannot be installed over");
        return COS_FAILED;
    }
    if (header.min_api_level > CANTOMK6_OS_API_LEVEL)
    {
        COS_LOG_E("Watchface '%s' requires API level %d, OS supports %d",
                  header.pkg_id,
                  header.min_api_level,
                  CANTOMK6_OS_API_LEVEL);
        return COS_ERR_SDK_VERSION;
    }
    // Construct path
    char path[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
    snprintf(path, sizeof(path), COS_WATCHFACE_INSTALLED_DIR "%s", header.pkg_id);
    char data_path[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
    snprintf(data_path, sizeof(data_path), COS_WATCHFACE_DATA_DIR "%s", header.pkg_id);
    COS_LOG_D("WATCHFACE_PATH: %s", path);
    // Check if application exists
    if (cos_storage_is_dir(path))
    {
        // If exists, delete it
        cos_storage_rm_recursive(path);
    }
    // Create folder with application name
    if (cos_storage_mkdir_if_not_exist(path) == COS_OK)
    {
        COS_LOG_I("Created dir: %s\n", path);
    }
    else
    {
        return COS_ERR_FILE_ERROR;
    }
    // Install watchface
    script_pkg_type_t type = SCRIPT_TYPE_WATCHFACE;
    cos_result_t ret = cos_pkg_mgr_unpack(eapk_path, path, type);
    if (ret != COS_OK)
    {
        COS_LOG_E("Watchface unpack failed. Code: %d", ret);
        cos_storage_rm_recursive(path);
        return COS_FAILED;
    }
    cos_storage_mkdir_if_not_exist(data_path);
    _cos_watchface_list_refresh();
    COS_LOG_D("Watchface installed successfully: %s", header.pkg_name);
    return COS_OK;
}

cos_result_t cos_watchface_uninstall(const char *watchface_id)
{
    if (!watchface_id || strcmp(watchface_id, COS_WATCHFACE_BUILTIN_FALLBACK_ID) == 0)
    {
        COS_LOG_E("Builtin fallback watchface cannot be uninstalled");
        return COS_FAILED;
    }

    // Uninstall watchface
    char path[COS_FS_PATH_MAX];
    snprintf(path, sizeof(path), COS_WATCHFACE_INSTALLED_DIR "%s", watchface_id);
    char data_path[COS_FS_PATH_MAX];
    snprintf(data_path, sizeof(data_path), COS_WATCHFACE_DATA_DIR "%s", watchface_id);
    if (!cos_storage_is_dir(path))
    {
        COS_LOG_E("Watchface does not exist: %s", watchface_id);
        return COS_FAILED;
    }

    cos_result_t ret = cos_storage_rm_recursive(path);

    if (ret != COS_OK)
    {
        COS_LOG_E("Uninstall failed, code: %d", ret);
        return COS_FAILED;
    }

    // Clean up watchface data
    if (cos_storage_is_dir(data_path))
    {
        ret = cos_storage_rm_recursive(data_path);
    }

    if (ret != COS_OK)
    {
        COS_LOG_E("Uninstall data failed, code: %d", ret);
        return COS_FAILED;
    }
    _cos_watchface_list_refresh();
    COS_LOG_D("Watchface uninstalled successfully: %s", watchface_id);
    return COS_OK;
}

void cos_watchface_long_pressed_handler(lv_event_t *e)
{
    cos_watchface_list_enter();
}

static void _switch_to_watchface(const char *watchface_id)
{
    COS_CHECK_PTR_RETURN(watchface_id);

    COS_LOG_I("Switching to watchface: %s", watchface_id);

    // Create new instance
    cos_watchface_instance_t *new_instance = NULL;

    if (strcmp(watchface_id, COS_WATCHFACE_BUILTIN_FALLBACK_ID) == 0)
    {
        new_instance = cos_watchface_builtin_create();
    }
    else
    {
        new_instance = cos_watchface_js_create(watchface_id);
    }

    if (!new_instance || !new_instance->activity)
    {
        COS_LOG_E("Failed to create watchface instance: %s", watchface_id);
        return;
    }

    // Save old instance pointer before replacing
    cos_watchface_instance_t *old_instance = _current_instance;

    // Replace root Activity (this will completely destroy old activity: on_destroy + view + free)
    cos_result_t ret = cos_activity_replace_root(new_instance->activity);
    if (ret != COS_OK)
    {
        COS_LOG_E("Failed to replace root activity");
        cos_free(new_instance); // Free new instance on failure
        return;
    }

    // Update current instance and free old instance (activity already freed by replace_root)
    _current_instance = new_instance;

    // Free old instance now (safe because replace_root already cleaned up its activity)
    if (old_instance)
    {
        cos_free(old_instance);
    }
}

cos_activity_t *cos_watchface_get_activity(void)
{
    if (_current_instance)
    {
        return _current_instance->activity;
    }
    return NULL;
}

void cos_watchface_switch_to(const char *watchface_id)
{
    COS_CHECK_PTR_RETURN(watchface_id);

    COS_LOG_I("Watchface switching to %s", watchface_id);

    // Save configuration first
    cos_config_set_string(COS_CONFIG_KEY_WATCHFACE_ID_STR, watchface_id);

    COS_LOG_I("Watchface Popping watchface list from stack");
    cos_activity_back();

    // Replace root activity with new watchface
    _switch_to_watchface(watchface_id);
}

void cos_watchface_check_and_reload(void)
{
    COS_CHECK_PTR_RETURN(_current_instance);

    char new_wf_id[COS_WATCHFACE_ID_LEN_MAX];
    char *selected_wf_id = cos_config_get_string(COS_CONFIG_KEY_WATCHFACE_ID_STR, "cn.sab1e.clock");
    snprintf(new_wf_id, sizeof(new_wf_id), "%s", selected_wf_id);
    cos_free(selected_wf_id);

    bool changed = strcmp(new_wf_id, _current_instance->id) != 0;

    if (changed)
    {
        COS_LOG_D("Watchface changed to %s, reloading", new_wf_id);
        _switch_to_watchface(new_wf_id);
    }
}

cos_result_t cos_watchface_init(void)
{
    COS_LOG_D("Initializing watchface");

    if (!_is_watchface_initialized)
    {
        _cos_watchface_list_refresh();
        _is_watchface_initialized = true;
    }
    else
    {
        COS_LOG_E("Watchface already initialized");
        return COS_FAILED;
    }

    // Get configured watchface ID
    char wf_id[COS_WATCHFACE_ID_LEN_MAX];
    char *selected_wf_id = cos_config_get_string(COS_CONFIG_KEY_WATCHFACE_ID_STR, "cn.sab1e.clock");
    snprintf(wf_id, sizeof(wf_id), "%s", selected_wf_id);
    cos_free(selected_wf_id);

    // Validate watchface ID
    if (!cos_watchface_list_contains(wf_id))
    {
        COS_LOG_W("Watchface not found, fallback to builtin: %s", wf_id);
        snprintf(wf_id, sizeof(wf_id), "%s", COS_WATCHFACE_BUILTIN_FALLBACK_ID);
    }

    // Create initial watchface instance
    if (strcmp(wf_id, COS_WATCHFACE_BUILTIN_FALLBACK_ID) == 0)
    {
        _current_instance = cos_watchface_builtin_create();
    }
    else
    {
        _current_instance = cos_watchface_js_create(wf_id);
    }

    if (!_current_instance || !_current_instance->activity)
    {
        COS_LOG_E("Failed to create initial watchface instance");
        return COS_FAILED;
    }

    COS_LOG_I("Watchface initialized successfully: %s", wf_id);
    return COS_OK;
}
