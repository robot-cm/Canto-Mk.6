/**
 * @file cos_event.h
 * @brief Event broadcast system - global broadcast using event ID as index
 */

#ifndef COS_EVENT_H
#define COS_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* Public macros ----------------------------------------------*/

#define COS_EVENT_MAX (UINT32_MAX)

/* Public typedefs --------------------------------------------*/

/**
 * @brief Event structure (private, opaque to users)
 */
typedef struct _cos_event_t cos_event_t;

/**
 * @brief Event callback function type
 * @param e Event structure
 */
typedef void (*cos_event_cb_t)(cos_event_t *e);

/**
 * @brief Event type definitions
 * @note New events can be added here
 */
typedef enum
{
    COS_EVENT_UNKNOWN = 0,
    COS_EVENT_APP_UNINSTALLED, /**< Application has been uninstalled */
    COS_EVENT_APP_INSTALLED, /**< Application has been installed */
    COS_EVENT_SYSTEM_SLEEP, /**< System entered sleep mode */
    COS_EVENT_SYSTEM_DISPLAY_ON, /**< System has been awakened */
    COS_EVENT_SYSTEM_DISPLAY_AOD, /**< Screen always-on mode has been activated */
    COS_EVENT_SYSTEM_CONFIG_UPDATE, /**< Configuration file has been updated */
    COS_EVENT_SCRIPT_STARTED, /**< Script has started */
    COS_EVENT_SCRIPT_EXITED, /**< Script has exited */
    COS_EVENT_ACTIVITY_SCREEN_SWITCHED, /**< Activity page transition completed, param is current activity view */
    COS_EVENT_LANGUAGE_CHANGED, /**< Language has been changed */
    COS_EVENT_LAST
} cos_event_code_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Get event ID for user-defined events
 * @return New event ID starting from COS_EVENT_LAST
 */
cos_event_code_t cos_event_register_id(void);

/**
 * @brief Subscribe to an event (basic version without lv_obj)
 * @param event_id Event ID to subscribe
 * @param cb Callback function to be called when event occurs
 * @param user_data User data passed to callback
 */
void cos_event_subscribe(cos_event_code_t event_id, cos_event_cb_t cb, void *user_data);

/**
 * @brief Subscribe to an event with lv_obj association
 * @param event_id Event ID to subscribe
 * @param cb Callback function to be called when event occurs
 * @param user_data User data passed to callback
 * @param obj lv_obj associated with this subscription (can be NULL, used as payload only)
 */
void cos_event_subscribe_ex(cos_event_code_t event_id, cos_event_cb_t cb, void *user_data, lv_obj_t *obj);

/**
 * @brief Post/Publish an event
 * @param event_id Event ID to post
 * @param param Event parameter
 * @param obj Event target object (can be NULL)
 */
void cos_event_post(cos_event_code_t event_id, void *param, lv_obj_t *obj);

/**
 * @brief Unsubscribe from an event
 * @param event_id Event ID
 * @param cb Callback function to remove
 */
void cos_event_unsubscribe(cos_event_code_t event_id, cos_event_cb_t cb);

/**
 * @brief Unsubscribe from an event with user_data distinction
 * @param event_id Event ID
 * @param cb Callback function to remove
 * @param user_data User data to match
 */
void cos_event_unsubscribe_with_user_data(cos_event_code_t event_id, cos_event_cb_t cb, void *user_data);

/**
 * @brief Unsubscribe all registrations for a specific callback
 * @param cb Callback function
 */
void cos_event_unsubscribe_all(cos_event_cb_t cb);

/**
 * @brief Unsubscribe from an event with obj distinction
 * @param event_id Event ID
 * @param cb Callback function to remove
 * @param obj Event target object to match
 */
void cos_event_unsubscribe_with_obj(cos_event_code_t event_id, cos_event_cb_t cb, lv_obj_t *obj);

/**
 * @brief Allow active cleanup to be triggered externally (e.g., called during system idle)
 */
void cos_event_cleanup_now(void);

/**
 * @brief Get user_data from event
 * @param e Event structure
 * @return user_data
 */
void *cos_event_get_user_data(cos_event_t *e);

/**
 * @brief Get param from event
 * @param e Event structure
 * @return param
 */
void *cos_event_get_param(cos_event_t *e);

/**
 * @brief Get obj from event
 * @param e Event structure
 * @return obj (can be NULL)
 */
lv_obj_t *cos_event_get_obj(cos_event_t *e);

#ifdef __cplusplus
}
#endif

#endif /* COS_EVENT_H */
