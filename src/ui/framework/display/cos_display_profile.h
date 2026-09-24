/**
 * @file cos_display_profile.h
 * @brief Display Adaptation Layer — shape-agnostic screen description.
 *
 * The single source of truth for "what shape/size is the screen". Every layout
 * decision (polar vs grid, safe area, dp scaling) derives from this struct so
 * that App code never hard-codes x/y or assumes a particular resolution.
 */
#ifndef COS_DISPLAY_PROFILE_H
#define COS_DISPLAY_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Display shape. Apps must not assume any of these. */
typedef enum {
    COS_DISPLAY_SHAPE_CIRCLE = 0,   /**< round panel, layout via polar coords   */
    COS_DISPLAY_SHAPE_SQUARE,       /**< near-square panel, grid/flex layout     */
    COS_DISPLAY_SHAPE_RECTANGLE,    /**< tall rectangular panel, grid + expand   */
} cos_display_shape_t;

/** @brief Reference width used by the dp scaling system. */
#define COS_UI_REF_WIDTH 240.0f

/**
 * @brief A resolved description of the physical display.
 *
 * For CIRCLE: safe_radius is the usable inscribed circle; positions are
 * expressed in polar form (center + radius). For SQUARE/RECTANGLE: the safe
 * rectangle (safe_x/y/w/h) is the padded content area for grid layouts.
 */
typedef struct {
    cos_display_shape_t shape;
    int  width;
    int  height;
    float center_x;          /**< geometric center (px)                  */
    float center_y;
    float radius;            /**< outer radius (min(w,h)/2)              */
    float safe_radius;       /**< CIRCLE usable radius (inscribed)       */
    float safe_x;            /**< SQUARE/RECT: safe rect top-left        */
    float safe_y;
    float safe_w;
    float safe_h;
    float aspect_ratio;      /**< width / height                         */
    float dp_scale;          /**< width / COS_UI_REF_WIDTH               */
} cos_display_profile_t;

/** @brief Build a profile and compute center/radius/safe area from shape+size. */
cos_display_profile_t cos_display_profile_create(cos_display_shape_t shape, int w, int h);

/** @brief Convert a logical dp value to physical px for this profile. */
float cos_dp(const cos_display_profile_t *p, float dp);

/**
 * @brief Convert polar (angle_deg, radius_px) to cartesian.
 * Angle convention: 0°=right(+x), 90°=down(+y), -90°=up. y grows downward.
 */
void cos_display_profile_polar(const cos_display_profile_t *p,
                               float angle_deg, float radius_px,
                               float *x, float *y);

/** @brief Is a circle (center x,y radius r) fully inside the safe area? */
bool cos_display_profile_is_inside(const cos_display_profile_t *p,
                                   float x, float y, float r);

/**
 * @brief Pull a circle (x,y,r) back inside the safe area if it overflows.
 * @return true if the point was moved.
 */
bool cos_display_profile_clamp_inside(const cos_display_profile_t *p,
                                      float *x, float *y, float r);

#ifdef __cplusplus
}
#endif
#endif /* COS_DISPLAY_PROFILE_H */
