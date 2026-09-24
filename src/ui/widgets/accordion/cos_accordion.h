/**
 * @file cos_accordion.h
 * @brief Accordion widget header
 */

#ifndef COS_ACCORDION_H
#define COS_ACCORDION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
/**
 * @brief Accordion state enumeration
 */
typedef enum
{
    COS_ACCORDION_STATE_CLOSED, /**< Accordion is closed */
    COS_ACCORDION_STATE_OPEN /**< Accordion is open */
} cos_accordion_state_t;

/**
 * @brief Accordion structure definition
 */
typedef struct
{
    lv_obj_t *container; /**< Main container object */
    lv_obj_t *title_bar; /**< Title bar object */
    lv_obj_t *title_label; /**< Title label object */
    lv_obj_t *arrow_label; /**< Arrow icon label object */
    lv_obj_t *content; /**< Content object */
    cos_accordion_state_t state; /**< Current accordion state */
    lv_coord_t content_height; /**< Cached content height */
} cos_accordion_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Create an accordion widget
 * @param parent Parent object
 * @param title Title text
 * @return cos_accordion_t* Accordion widget handle
 */
cos_accordion_t *cos_accordion_create(lv_obj_t *parent, const char *title);

/**
 * @brief Toggle accordion state
 * @param accordion Accordion handle
 * @param anim Whether to animate the transition
 */
void cos_accordion_toggle(cos_accordion_t *accordion, bool anim);

/**
 * @brief Set accordion state
 * @param accordion Accordion handle
 * @param state New state
 * @param anim Whether to animate the transition
 */
void cos_accordion_set_state(cos_accordion_t *accordion, cos_accordion_state_t state, bool anim);

/**
 * @brief Get accordion state
 * @param accordion Accordion handle
 * @return cos_accordion_state_t Current state
 */
cos_accordion_state_t cos_accordion_get_state(cos_accordion_t *accordion);

/**
 * @brief Delete accordion widget
 * @param accordion Accordion handle
 */
void cos_accordion_delete(cos_accordion_t *accordion);

#ifdef __cplusplus
}
#endif

#endif /* COS_ACCORDION_H */
