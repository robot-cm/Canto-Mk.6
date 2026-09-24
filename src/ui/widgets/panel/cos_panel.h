/**
 * @file cos_panel.h
 * @brief Generic panel component
 */

#ifndef COS_PANEL_H
#define COS_PANEL_H

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

#define COS_PANEL_CONTENT_WIDTH lv_pct(90) /**< Panel content width (percentage) */
#define COS_PANEL_ICON_SIZE 50 /**< Panel icon size */

/* Public typedefs --------------------------------------------*/

/**
 * @brief Panel icon type
 */
typedef enum
{
    COS_PANEL_ICON_TYPE_NONE = 0, /**< No icon */
    COS_PANEL_ICON_TYPE_SYMBOL, /**< Symbol icon (text) */
    COS_PANEL_ICON_TYPE_IMAGE, /**< Image icon */
} cos_panel_icon_type_t;

/**
 * @brief Panel configuration
 */
typedef struct
{
    lv_color_t icon_bg_color; /**< Icon background color */
    cos_panel_icon_type_t icon_type; /**< Icon type */
    const void *icon_src; /**< Icon source (symbol string or image path) */
    lang_string_id_t title_id; /**< Title string ID (0 for none) */
    const char *title_text; /**< Title text (used if title_id is 0) */
    lang_string_id_t message_id; /**< Message string ID (0 for none) */
    const char *message_text; /**< Message text (used if message_id is 0) */
    lang_string_id_t confirm_btn_id; /**< Confirm button string ID (0 for hidden) */
    const char *confirm_btn_text; /**< Confirm button text */
    lv_event_cb_t confirm_cb; /**< Confirm button callback (NULL for default: do nothing) */
    lang_string_id_t cancel_btn_id; /**< Cancel button string ID (0 for hidden) */
    const char *cancel_btn_text; /**< Cancel button text */
    lv_event_cb_t cancel_cb; /**< Cancel button callback (NULL for default: activity_back) */
} cos_panel_cfg_t;

/**
 * @brief Panel structure
 */
typedef struct
{
    lv_obj_t *container; /**< Main container */
    lv_obj_t *icon; /**< Icon object (NULL if no icon) */
    lv_obj_t *title; /**< Title label */
    lv_obj_t *message; /**< Message label */
    lv_obj_t *extra_slot; /**< Extra content slot */
    lv_obj_t *actions; /**< Actions container */
    lv_obj_t *confirm_btn; /**< Confirm button (NULL if hidden) */
    lv_obj_t *cancel_btn; /**< Cancel button (NULL if hidden) */
} cos_panel_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Create a panel on given activity
 * @param activity Activity to create panel on
 * @param cfg Panel configuration
 * @return cos_panel_t* Panel structure, NULL on failure
 */
cos_panel_t *cos_panel_create_on_activity(cos_activity_t *activity, const cos_panel_cfg_t *cfg);

/**
 * @brief Create a panel on given parent object
 * @param parent Parent object
 * @param cfg Panel configuration
 * @return cos_panel_t* Panel structure, NULL on failure
 */
cos_panel_t *cos_panel_create(lv_obj_t *parent, const cos_panel_cfg_t *cfg);

/**
 * @brief Get panel's extra content slot
 * @param panel Panel structure
 * @return lv_obj_t* Extra slot object for adding custom content
 */
lv_obj_t *cos_panel_get_extra_slot(cos_panel_t *panel);

/**
 * @brief Delete panel
 * @param panel Panel structure
 */
void cos_panel_delete(cos_panel_t *panel);

#ifdef __cplusplus
}
#endif

#endif /* COS_PANEL_H */
