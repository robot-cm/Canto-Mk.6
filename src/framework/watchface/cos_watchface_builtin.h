/**
 * @file cos_watchface_builtin.h
 * @brief Built-in fallback watchface implementation
 */

#ifndef COS_WATCHFACE_BUILTIN_H
#define COS_WATCHFACE_BUILTIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "cos_watchface.h"
/* Public function prototypes --------------------------------*/

/**
 * @brief Create a built-in watchface instance with its own Activity
 * @return cos_watchface_instance_t* Instance pointer, NULL on failure
 *
 * The instance owns its Activity and manages its lifecycle independently.
 * Caller is responsible for destroying the instance when no longer needed.
 */
cos_watchface_instance_t *cos_watchface_builtin_create(void);

/**
 * @brief Simulator self-test hook: drive the home swipe-navigation logic with
 *        a synthetic (dx, dy) drag vector. Lets the regression suite assert the
 *        left/right/up wiring without needing a real touch indev. Not used in
 *        production.
 */
void cos_watchface_builtin_test_swipe(lv_coord_t dx, lv_coord_t dy);

#ifdef __cplusplus
}
#endif

#endif /* COS_WATCHFACE_BUILTIN_H */
