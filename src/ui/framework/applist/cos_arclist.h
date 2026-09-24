/**
 * @file cos_arclist.h
 * @brief Huawei-Watch style arc app list (req #2).
 *
 * Icons sit on an arc; the focused app is centered and enlarged, neighbours
 * shrink and fade with angular distance, scrolling is inertial with bounce.
 * Geometry is pure math (resolved against a DisplayProfile) so it works on any
 * screen shape and is fully headless-testable.
 */
#ifndef COS_ARCLIST_H
#define COS_ARCLIST_H

#include "cos_display_profile.h"
#include "cos_physics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COS_ARCLIST_MAX 64

typedef struct {
    int   index;     /**< app slot index                    */
    float x, y;      /**< resolved center (px)             */
    float scale;     /**< visual scale (1.0 = base)        */
    float opacity;   /**< 0..1                             */
    bool  visible;
} cos_arclist_item_t;

typedef struct {
    const cos_display_profile_t *profile;
    int    count;
    float  scroll;           /**< scroll offset in arc-degrees        */
    cos_scroller_t scroller;  /**< physics driver (pos == scroll)      */
    cos_arclist_item_t items[COS_ARCLIST_MAX];
    float  arc_radius_factor; /**< R = safe_radius * factor            */
    float  step_deg;          /**< angle between adjacent apps         */
    float  focus_scale;       /**< scale at focus                      */
    float  min_scale;
    float  visible_deg;       /**< hide beyond this angular distance   */
    float  item_radius;       /**< base item radius (px at scale 1.0),
                                    used to clamp item centers inside the
                                    safe area; defaults to 22*dp_scale.    */
} cos_arclist_t;

void cos_arclist_init(cos_arclist_t *al, const cos_display_profile_t *p, int count);
/** @brief Override the item radius (px at scale 1.0) used for safe-area clamp. */
void cos_arclist_set_item_radius(cos_arclist_t *al, float radius_px);
/** @brief Recompute every item's geometry from the current scroll. */
void cos_arclist_layout(cos_arclist_t *al);
/** @brief Finger drag: dy_px (screen px) -> scroll change. */
void cos_arclist_drag(cos_arclist_t *al, float dy_px, float dt);
void cos_arclist_release(cos_arclist_t *al);
void cos_arclist_step(cos_arclist_t *al, float dt);
/** @brief Index of the app currently at the focus (center). */
int  cos_arclist_focus_index(const cos_arclist_t *al);

#ifdef __cplusplus
}
#endif
#endif /* COS_ARCLIST_H */
