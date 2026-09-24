/**
 * @file cos_side_button.h
 * @brief Side button
 */

#ifndef COS_SIDE_BUTTON_H
#define COS_SIDE_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_core.h"
#include "input/cos_input.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/
/**
 * @brief Report side button state
 * @param state State value
 */
void cos_side_button_report(cos_button_state_t state);
#ifdef __cplusplus
}
#endif

#endif /* COS_SIDE_BUTTON_H */
