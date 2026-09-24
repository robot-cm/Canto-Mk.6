/**
 * @file cos_input.h
 * @brief Input handling header file
 */

#ifndef COS_INPUT_H
#define COS_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Button press state
 */
typedef enum
{
    COS_BUTTON_STATE_CLICKED,
    COS_BUTTON_STATE_PRESSED,
    COS_BUTTON_STATE_LONG_PRESSED,
    COS_BUTTON_STATE_RELEASED,
    COS_BUTTON_STATE_DOUBLE_CLICKED
} cos_button_state_t;

/* Public function prototypes --------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* COS_INPUT_H */
