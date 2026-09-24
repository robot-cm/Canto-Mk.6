/**
 * @file cos_service_permission.h
 * @brief Permission service - Android-like runtime permission model
 */
#ifndef COS_SERVICE_PERMISSION_H
#define COS_SERVICE_PERMISSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_lang.h"

/* Public typedefs --------------------------------------------*/

/**
 * @brief Permission categories
 */
typedef enum
{
    COS_PERM_CATEGORY_LOCATION = 0, /**< Location information */
    COS_PERM_CATEGORY_SENSOR, /**< Sensor */
    COS_PERM_CATEGORY_NOTIFICATION, /**< Notification */
    COS_PERM_CATEGORY_STORAGE, /**< Storage */
    COS_PERM_CATEGORY_BLUETOOTH, /**< Bluetooth */
    COS_PERM_CATEGORY_AUDIO, /**< Audio/Mic */
    COS_PERM_CATEGORY_HEALTH, /**< Health */
    COS_PERM_CATEGORY_CONTACTS, /**< Contacts */
    COS_PERM_CATEGORY_CALENDAR, /**< Calendar */
    COS_PERM_CATEGORY_COUNT /**< Sentinel - must be last */
} cos_perm_category_t;

/**
 * @brief Permission grant states (Android-like)
 */
typedef enum
{
    COS_PERM_STATE_DENIED = 0, /**< Not allowed */
    COS_PERM_STATE_ALLOW_ONCE, /**< Allow once */
    COS_PERM_STATE_ALLOW_FOREGROUND, /**< Allow while using the app */
    COS_PERM_STATE_ALLOW_ALWAYS, /**< Always allow */
} cos_perm_state_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize the permission service
 * @note Must be called during system init after config service is ready
 */
void cos_service_permission_init(void);

/**
 * @brief Get current grant state for an app's permission
 * @param app_id Application ID (e.g., "cn.sab1e.calculator")
 * @param cat Permission category
 * @return cos_perm_state_t Current grant state (defaults to DENIED if never set)
 */
cos_perm_state_t cos_permission_get(const char *app_id, cos_perm_category_t cat);

/**
 * @brief Set grant state for an app's permission
 * @param app_id Application ID
 * @param cat Permission category
 * @param state New grant state
 * @return true on success, false on failure
 */
bool cos_permission_set(const char *app_id, cos_perm_category_t cat, cos_perm_state_t state);

/**
 * @brief Revoke all permissions for an app (used on uninstall)
 * @param app_id Application ID
 */
void cos_permission_revoke_all(const char *app_id);

/**
 * @brief Get the human-readable display name for a permission category
 * @param cat Permission category
 * @return const char* Localized name string
 */
const char *cos_permission_category_name(cos_perm_category_t cat);

/**
 * @brief Get the description text for a permission category
 *        (used as body text in the permission request panel)
 * @param cat Permission category
 * @return const char* Localized description string
 */
const char *cos_permission_category_desc(cos_perm_category_t cat);

/**
 * @brief Convert a permission name string (from manifest) to category enum
 * @param name Permission name string (e.g., "location", "sensor")
 * @return cos_perm_category_t Category enum, or COS_PERM_CATEGORY_COUNT if unknown
 */
cos_perm_category_t cos_permission_name_to_category(const char *name);

/**
 * @brief Get the string key name for a permission category
 * @param cat Permission category
 * @return const char* Key name (e.g., "location") or NULL if invalid
 */
const char *cos_permission_category_key(cos_perm_category_t cat);

/**
 * @brief Get the i18n string ID for a permission category name
 * @param cat Permission category
 * @return lang_string_id_t String ID
 */
lang_string_id_t cos_permission_category_name_id(cos_perm_category_t cat);

/**
 * @brief Get the i18n string ID for a permission category description
 * @param cat Permission category
 * @return lang_string_id_t String ID
 */
lang_string_id_t cos_permission_category_desc_id(cos_perm_category_t cat);

/**
 * @brief Get the i18n string ID for a grant state label
 * @param state Grant state
 * @return lang_string_id_t String ID
 */
lang_string_id_t cos_permission_state_label_id(cos_perm_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_PERMISSION_H */
