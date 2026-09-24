/**
 * @file cos_app.h
 * @brief Application system
 */

#ifndef COS_APP_H
#define COS_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cos_core.h"
#include "cos_service_config.h"
#include "cos_storage_paths.h"
#include "lvgl.h"
#include "script_engine_core.h"

/* Public macros ----------------------------------------------*/
#define COS_APP_ICON_FILE_NAME "icon.bin"
#define COS_APP_MANIFEST_FILE_NAME "manifest.json"
#define COS_APP_SCRIPT_ENTRY_FILE_NAME "main.js"
/* Public typedefs --------------------------------------------*/

/**
 * @brief Script error handler configuration
 */
typedef struct
{
    const char *title_text; /**< Error title text (NULL for default) */
    lang_string_id_t title_id; /**< Error title string ID (0 for default) */
    const char *button_text; /**< Button text (NULL for default) */
    lang_string_id_t button_id; /**< Button string ID (0 for default) */
    lv_event_cb_t button_callback; /**< Button click callback (NULL for default back) */
} cos_script_error_handler_cfg_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Handle script execution error
 * @param error_type Type of script error
 * @param error_code Error code from script engine
 * @param app_id Application ID that caused the error
 * @param cfg Optional configuration for customizing error page
 */
void cos_app_handle_script_error(cos_script_error_type_t error_type,
                                 cos_result_t error_code,
                                 const char *app_id,
                                 const cos_script_error_handler_cfg_t *cfg);

/**
 * @brief Move app with target ID to specified position for app_list sorting
 * @param app_id Target ID
 * @param new_index New index value
 * @return cos_result_t
 */
cos_result_t cos_app_order_move(const char *app_id, size_t new_index);
/**
 * @brief Get the number of currently installed apps
 */
uint32_t cos_app_get_installed(void);
/**
 * @brief Get the display name of an installed app from its manifest
 * @param id App id
 * @return const char* name string (static buffer, do not free). "App" if unknown.
 */
const char *cos_app_get_name(const char *id);
/**
 * @brief Get app id by index
 * @param index Index value (0-based)
 * @return const char* id string
 */
const char *cos_app_list_get_id(size_t index);
/**
 * @brief Check if app with specified id exists in the list
 * @param app_id id string
 * @return true
 * @return false
 */
bool cos_app_list_contains(const char *app_id);
/**
 * @brief Get existing ID from app list that matches input string (avoid duplicate memory allocation)
 * @param id Original ID to find (e.g., header.pkg_id)
 * @return Existing string pointer in the list (lifecycle managed by the list), returns NULL if not found
 */
const char *cos_app_list_get_existing_id(const char *id);
/**
 * @brief Install app
 * @param eapk_path eapk package path
 * @return cos_result_t Installation result
 */
cos_result_t cos_app_install(const char *eapk_path);
/**
 * @brief Uninstall app
 * @param app_id App id
 * @return cos_result_t Uninstallation result
 */
cos_result_t cos_app_uninstall(const char *app_id);
/**
 * @brief Disable an installed app.
 *
 * The app stays installed on disk but is hidden from the launcher and
 * cannot be launched until re-enabled. State is persisted in config
 * (COS_CONFIG_KEY_APP_DISABLED).
 * @param app_id App id
 * @return cos_result_t Result
 */
cos_result_t cos_app_disable(const char *app_id);
/**
 * @brief Re-enable a previously disabled app.
 * @param app_id App id
 * @return cos_result_t Result
 */
cos_result_t cos_app_enable(const char *app_id);
/**
 * @brief Query whether an app is currently disabled.
 * @param app_id App id
 * @return true if disabled
 */
bool cos_app_is_disabled(const char *app_id);
/**
 * @brief Automatically delete specified object when app is deleted
 * @param obj Target object
 * @param app_id Target app ID
 */
void cos_app_obj_auto_delete(lv_obj_t *obj, const char *app_id);
/**
 * @brief Initialize app system
 * @return cos_result_t Initialization result
 */
cos_result_t cos_app_init(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_APP_H */
