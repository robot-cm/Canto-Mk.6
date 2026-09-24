/**
 * @file cos_settings.h
 * @brief System settings
 */

#ifndef COS_SETTINGS_H
#define COS_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_core.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Enter settings page
 * @return cos_activity_t* Settings page activity pointer, returns NULL on failure
 */
void cos_settings_enter(void);
/**
 * @brief Enable silent mode
 */
void cos_settings_slient_mode_on(void);
/**
 * @brief Disable silent mode
 */
void cos_settings_slient_mode_off(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_SETTINGS_H */
