/**
 * @file cos_corner_radius.h
 * @brief Arbitrary corner radius background widget
 */

#ifndef COS_CORNER_RADIUS_H
#define COS_CORNER_RADIUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Set specified corner radius positions, used with `cos_obj_set_corner_radius_bg()`
 */
typedef enum
{
    COS_ROUND_TOP_LEFT = 0x1 << 0, /**< Top left corner rounded */
    COS_ROUND_TOP_RIGHT = 0x1 << 1, /**< Top right corner rounded */
    COS_ROUND_BOTTOM_RIGHT = 0x1 << 2, /**< Bottom right corner rounded */
    COS_ROUND_BOTTOM_LEFT = 0x1 << 3, /**< Bottom left corner rounded */
} cos_corner_round_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Set background with specified corner radius for object
 * @param obj Object
 * @param corners Specified corners, example: `COS_ROUND_TOP_LEFT | COS_ROUND_BOTTOM_LEFT`
 * @param radius Corner radius
 * @param color Background color
 */
void cos_obj_set_corner_radius_bg(lv_obj_t *obj, cos_corner_round_t corners, lv_coord_t radius, lv_color_t color);

/**
 * @brief Remove corner radius background from an object
 */
void cos_obj_remove_corner_radius_bg(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* COS_CORNER_RADIUS_H */
