/**
 * @file cos_lock_page.h
 * @brief Lock screen security barrier on lv_layer_top()
 */

#ifndef COS_LOCK_PAGE_H
#define COS_LOCK_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>
#include "lvgl.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Show lock screen security barrier on lv_layer_top()
 */
void cos_lock_page_show(void);

/**
 * @brief Hide and destroy lock screen security barrier
 */
void cos_lock_page_hide(void);

/**
 * @brief Check if lock screen is currently visible
 * @return true if lock screen is showing
 */
bool cos_lock_page_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_LOCK_PAGE_H */
