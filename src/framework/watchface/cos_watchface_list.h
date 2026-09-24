/**
 * @file cos_watchface_list.h
 * @brief Watchface list
 */

#ifndef COS_WATCHFACE_LIST_H
#define COS_WATCHFACE_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_core.h"
#include "cos_activity.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Enter watchface list page
 * @return cos_activity_t* Watchface list page Activity
 */
void cos_watchface_list_enter(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_WATCHFACE_LIST_H */
