/**
 * @file eos_watchface_builtin.h
 * @brief Built-in fallback watchface implementation
 */

#ifndef EOS_WATCHFACE_BUILTIN_H
#define EOS_WATCHFACE_BUILTIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "eos_watchface.h"
/* Public function prototypes --------------------------------*/

/**
 * @brief Create a built-in watchface instance with its own Activity
 * @return eos_watchface_instance_t* Instance pointer, NULL on failure
 *
 * The instance owns its Activity and manages its lifecycle independently.
 * Caller is responsible for destroying the instance when no longer needed.
 */
eos_watchface_instance_t *eos_watchface_builtin_create(void);

/**
 * @brief Simulator self-test hook: drive the home swipe-navigation logic with
 *        a synthetic (dx, dy) drag vector. Lets the regression suite assert the
 *        left/right/up wiring without needing a real touch indev. Not used in
 *        production.
 */
void eos_watchface_builtin_test_swipe(lv_coord_t dx, lv_coord_t dy);

#ifdef __cplusplus
}
#endif

#endif /* EOS_WATCHFACE_BUILTIN_H */
