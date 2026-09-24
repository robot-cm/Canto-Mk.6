/**
 * @file cos_virtual_display.h
 * @brief Virtual display
 */
#ifndef COS_VIRTUAL_DISPLAY_H
#define COS_VIRTUAL_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "cos_config.h"
#if COS_USE_VIRTUAL_DISPLAY
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
typedef struct cos_virtual_display_t cos_virtual_display_t;
/* Public function prototypes --------------------------------*/

/**
 * @brief Create a virtual display that can be placed inside LVGL object.
 * @param parent Parent object
 * @param hor_res Display horizontal resolution
 * @param ver_res Display vertical resolution
 * @return lv_display_t* Returns display object if creation succeeds, otherwise returns NULL
 */
lv_display_t *cos_virtual_display_create(lv_obj_t *parent, lv_coord_t hor_res, lv_coord_t ver_res);
#endif /* COS_USE_VIRTUAL_DISPLAY */

#ifdef __cplusplus
}
#endif

#endif /* COS_VIRTUAL_DISPLAY_H */
