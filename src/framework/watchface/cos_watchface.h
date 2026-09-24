/**
 * @file cos_watchface.h
 * @brief Watchface
 */

#ifndef COS_WATCHFACE_H
#define COS_WATCHFACE_H

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
#include "cos_activity.h"
#include "script_engine_core.h"
/* Public macros ----------------------------------------------*/
#define COS_WATCHFACE_DATA_DIR COS_WATCHFACE_DIR "wf_data/"
#define COS_WATCHFACE_BUILTIN_FALLBACK_ID "builtin.fallback"
#define COS_WATCHFACE_MANIFEST_FILE_NAME "manifest.json"
#define COS_WATCHFACE_SNAPSHOT_FILE_NAME "snapshot.bin"
#define COS_WATCHFACE_SCRIPT_ENTRY_FILE_NAME "main.js"
#define COS_WATCHFACE_ID_LEN_MAX 256 /*< Watchface ID maximum length */
/* Public typedefs --------------------------------------------*/

/**
 * @brief Watchface type enumeration
 */
typedef enum
{
    COS_WATCHFACE_TYPE_BUILTIN, /**< Built-in fallback watchface */
    COS_WATCHFACE_TYPE_JS, /**< JavaScript script watchface */
} cos_watchface_type_t;

/**
 * @brief Watchface instance structure
 *
 * Each watchface instance owns its own Activity and manages its lifecycle.
 * Switching watchfaces = destroy old instance + create new instance.
 */
typedef struct cos_watchface_instance
{
    cos_watchface_type_t type; /**< Watchface type */
    char id[COS_WATCHFACE_ID_LEN_MAX]; /**< Watchface ID */

    cos_activity_t *activity; /**< Owned Activity (created and managed by instance) */
    const cos_activity_lifecycle_t *lifecycle; /**< Activity lifecycle callbacks */

    union
    {
        struct
        {
            lv_timer_t *time_update_timer;   /**< Time update timer for builtin watchface */
            lv_obj_t   *home_gesture_catcher; /**< Full-screen clickable layer that
                                                    receives LEFT/RIGHT/UP + LONG_PRESSED */
        } builtin;

        struct
        {
            script_pkg_t pkg; /**< Script package information for JS watchface */
        } js;
    } data;
} cos_watchface_instance_t;
/* Public function prototypes --------------------------------*/

/**
 * @brief Get the number of currently installed watchfaces, i.e., list size
 * @return size_t List size
 */
size_t cos_watchface_list_size(void);
/**
 * @brief Get watchface id by index
 * @param index Index value (0-based)
 * @return const char* id string
 */
const char *cos_watchface_list_get_id(size_t index);
/**
 * @brief Check if watchface with specified id exists in the list
 * @param watchface_id id string
 * @return true
 * @return false
 */
bool cos_watchface_list_contains(const char *watchface_id);
/**
 * @brief Install watchface
 * @param eapk_path eapk package path
 * @return cos_result_t Installation result
 */
cos_result_t cos_watchface_install(const char *eapk_path);
/**
 * @brief Uninstall watchface
 * @param watchface_id Watchface id
 * @return cos_result_t Uninstallation result
 */
cos_result_t cos_watchface_uninstall(const char *watchface_id);
/**
 * @brief Initialize watchface system
 * @return cos_result_t Initialization result
 */
cos_result_t cos_watchface_init(void);
/**
 * @brief Get watchface Activity created during initialization
 * @return cos_activity_t* Watchface Activity pointer, returns NULL on failure
 */
cos_activity_t *cos_watchface_get_activity(void);

/**
 * @brief Check if watchface configuration changed and reload if needed
 * @note This function should be called when returning to root activity (e.g., in on_resume)
 */
void cos_watchface_check_and_reload(void);

/**
 * @brief Switch to a specific watchface by ID
 * @param watchface_id Target watchface ID to switch to
 * @note This function will replace the root activity with the new watchface instance
 */
void cos_watchface_switch_to(const char *watchface_id);

/**
 * @brief Long press handler for watchface (enter watchface list)
 * @param e Event object
 */
void cos_watchface_long_pressed_handler(lv_event_t *e);
#ifdef __cplusplus
}
#endif

#endif /* COS_WATCHFACE_H */
