/**
 * @file cos_ww_clock_hand.h
 * @brief Clock hand
 */

#ifndef COS_WW_CLOCK_HAND_H
#define COS_WW_CLOCK_HAND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
typedef enum
{
    COS_CLOCK_HAND_HOUR,
    COS_CLOCK_HAND_MINUTE,
    COS_CLOCK_HAND_SECOND,
} cos_clock_hand_type_t;
/* Public function prototypes --------------------------------*/

/**
 * @brief Place pointer at specified position (control anchor point as rotation center)
 * @param hand Pointer object
 * @param target_x x
 * @param target_y y
 */
void cos_clock_hand_place_pivot(lv_obj_t *hand, lv_coord_t target_x, lv_coord_t target_y);

/**
 * @brief Center relative to parent object using pointer rotation center as anchor
 * @param hand Pointer object
 */
void cos_clock_hand_center(lv_obj_t *hand);

/**
 * @brief Create a clock hand
 * @param parent Pointer parent object
 * @param src Pointer image source
 * @param t Pointer type `cos_clock_hand_type_t`
 * @param hand_pivot_x Rotation center x coordinate (relative to image)
 * @param hand_pivot_y Rotation center y coordinate (relative to image)
 * @return lv_obj_t* Returns created pointer image object
 */
lv_obj_t *cos_clock_hand_create(lv_obj_t *parent,
                                const char *src,
                                cos_clock_hand_type_t t,
                                lv_coord_t hand_pivot_x,
                                lv_coord_t hand_pivot_y);

/**
 * @brief Attach clock-hand timer behavior to an existing LVGL object.
 *
 * Uses style-transform rotation (lv_obj_set_style_transform_rotation) so
 * ANY LVGL widget (obj, canvas, image, …) can serve as a hand.
 * The caller should set the transform pivot via
 * lv_obj_set_style_transform_pivot_x / _y before attaching.
 *
 * @param hand  Existing LVGL object to animate
 * @param t     Hand type (hour / minute / second)
 * @return      The created LVGL timer (or NULL on failure)
 */
lv_timer_t *cos_clock_hand_attach(lv_obj_t *hand, cos_clock_hand_type_t t);

/**
 * @brief Center a style-driven hand relative to its parent.
 *
 * Like cos_clock_hand_center() but uses an explicit pivot value instead
 * of reading lv_image_get_pivot().  Call this AFTER setting the
 * transform pivot on the hand.
 *
 * @param hand     Hand object
 * @param pivot_x  Rotation-centre X in the object's local coordinates
 * @param pivot_y  Rotation-centre Y in the object's local coordinates
 */
void cos_clock_hand_center_style(lv_obj_t *hand, lv_coord_t pivot_x, lv_coord_t pivot_y);

#ifdef __cplusplus
}
#endif

#endif /* COS_WW_CLOCK_HAND_H */
