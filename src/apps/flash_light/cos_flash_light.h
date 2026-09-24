/**
 * @file cos_flash_light.h
 * @brief Flashlight
 */

#ifndef COS_FLASH_LIGHT_H
#define COS_FLASH_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_chrome_manager.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Quick display flashlight, mainly used for `control_center`.
 */
void cos_flash_light_show(void);
/**
 * @brief Enter flashlight page, used for app list.
 */
void cos_flash_light_enter(void);

/**
 * @brief Check if flashlight overlay is currently open.
 * @return true if open, false otherwise.
 */
bool cos_flash_light_is_open(void);

/**
 * @brief Pull back (close with animation) the flashlight overlay.
 */
void cos_flash_light_pull_back(void);

/**
 * @brief Hide the flashlight overlay immediately (no animation).
 */
void cos_flash_light_hide(void);
const cos_chrome_overlay_t *cos_flash_light_get_overlay_descriptor(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_FLASH_LIGHT_H */
