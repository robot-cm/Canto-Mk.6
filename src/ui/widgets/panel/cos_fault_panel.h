/**
 * @file cos_fault_panel.h
 * @brief Fault panel component for error display
 */

#ifndef COS_FAULT_PANEL_H
#define COS_FAULT_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_lang.h"
#include "cos_activity.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Fault panel icon type
 */
typedef enum
{
    COS_FAULT_ICON_NONE = 0, /**< No icon */
    COS_FAULT_ICON_BUG, /**< Bug icon for error */
    COS_FAULT_ICON_WARNING, /**< Warning icon */
    COS_FAULT_ICON_INFO, /**< Info icon */
    COS_FAULT_ICON_CUSTOM_SYMBOL, /**< Custom symbol icon */
} cos_fault_icon_t;

/**
 * @brief Fault panel configuration
 */
typedef struct
{
    cos_fault_icon_t icon_type; /**< Icon type */
    const char *custom_icon; /**< Custom icon symbol (used if icon_type is CUSTOM_SYMBOL) */
    lang_string_id_t title_id; /**< Title string ID */
    const char *title_text; /**< Title text (used if title_id is 0) */
    lang_string_id_t message_id; /**< Message string ID */
    const char *message_text; /**< Message text (used if message_id is 0) */
    lang_string_id_t confirm_btn_id; /**< Confirm button string ID (0 for hidden) */
    const char *confirm_btn_text; /**< Confirm button text */
    lv_event_cb_t confirm_cb; /**< Confirm button callback (NULL for default: do nothing) */
    lang_string_id_t cancel_btn_id; /**< Cancel button string ID (0 for hidden) */
    const char *cancel_btn_text; /**< Cancel button text */
    lv_event_cb_t cancel_cb; /**< Cancel button callback (NULL for default: activity_back) */
    lv_color_t icon_color; /**< Icon background color */
} cos_fault_cfg_t;

/**
 * @brief Fault panel structure
 */
typedef struct
{
    void *panel; /**< Internal panel structure */
    lv_obj_t *extra_slot; /**< Extra content slot for backtrace */
} cos_fault_panel_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Create a fault panel on current activity
 * @param cfg Fault panel configuration
 * @return cos_fault_panel_t* Fault panel structure, NULL on failure
 */
cos_fault_panel_t *cos_fault_panel_create(const cos_fault_cfg_t *cfg);

/**
 * @brief Create a fault panel on given activity
 * @param activity Activity to create panel on
 * @param cfg Fault panel configuration
 * @return cos_fault_panel_t* Fault panel structure, NULL on failure
 */
cos_fault_panel_t *cos_fault_panel_create_on_activity(cos_activity_t *activity, const cos_fault_cfg_t *cfg);

/**
 * @brief Get fault panel's extra content slot for adding backtrace or custom content
 * @param panel Fault panel structure
 * @return lv_obj_t* Extra slot object
 */
lv_obj_t *cos_fault_panel_get_extra_slot(cos_fault_panel_t *panel);

/**
 * @brief Delete fault panel
 * @param panel Fault panel structure
 */
void cos_fault_panel_delete(cos_fault_panel_t *panel);

#ifdef __cplusplus
}
#endif

#endif /* COS_FAULT_PANEL_H */
