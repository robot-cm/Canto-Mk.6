/**
 * @file eos_power_off_page.h
 * @brief Power-off confirmation page (overlay)
 *
 * Shown by an up-swipe on the home watchface. The user taps the power icon
 * to confirm shutdown, which requests deep sleep. Waking up from deep sleep
 * requires 5 taps on the screen.
 */

#ifndef EOS_POWER_OFF_PAGE_H
#define EOS_POWER_OFF_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>

/* Public function prototypes --------------------------------*/

/**
 * @brief Show the power-off confirmation page (full-screen overlay)
 */
void eos_power_off_page_show(void);

/**
 * @brief Hide the power-off confirmation page
 */
void eos_power_off_page_hide(void);

/**
 * @brief Check whether the power-off page is currently visible
 */
bool eos_power_off_page_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_POWER_OFF_PAGE_H */
