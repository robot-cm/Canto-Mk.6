/**
 * @file cos_crown.h
 * @brief Crown
 */

#ifndef COS_CROWN_H
#define COS_CROWN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_core.h"
#include "input/cos_input.h"
#include "cos_slide_widget.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
typedef int16_t cos_crown_encoder_diff_t;
/* Public function prototypes --------------------------------*/
/**
 * @brief Set encoder reverse
 * @param reverse true = reverse, false = forward
 */
void cos_crown_encoder_set_reverse(bool reverse);
/**
 * @brief Set encoder scroll object
 * @param obj Target scroll object
 */
void cos_crown_encoder_set_target_obj(lv_obj_t *obj);
/**
 * @brief Automatically set encoder scroll object based on active View, always gets the first created object
 * @param view Target View
 */
void cos_crown_encoder_set_target_view(lv_obj_t *view);
/**
 * @brief Activate scroll object of currently visible overlay, uniformly selected by overlay priority
 */
void cos_crown_encoder_activate_current_overlay_scrollable(void);
/**
 * @brief Report crown button state
 * @param state State value
 */
void cos_crown_button_report(cos_button_state_t state);
/**
 * @brief Report crown encoder increment
 * @param diff Increment
 */
void cos_crown_encoder_report(cos_crown_encoder_diff_t diff);
/**
 * @brief Initialize
 */
void cos_crown_init(void);
/**
 * @brief Register slide widget to enable automatic encoder targeting when the widget is opened
 * @param sw Target slide widget
 */
void cos_crown_encoder_register_slide_widget(cos_slide_widget_t *sw);
#ifdef __cplusplus
}
#endif

#endif /* COS_CROWN_H */
