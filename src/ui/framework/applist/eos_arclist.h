/**
 * @file eos_arclist.h
 * @brief Huawei-Watch style arc app list (req #2).
 *
 * Icons sit on an arc; the focused app is centered and enlarged, neighbours
 * shrink and fade with angular distance, scrolling is inertial with bounce.
 * Geometry is pure math (resolved against a DisplayProfile) so it works on any
 * screen shape and is fully headless-testable.
 */
#ifndef EOS_ARCLIST_H
#define EOS_ARCLIST_H

#include "eos_display_profile.h"
#include "eos_physics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_ARCLIST_MAX 64

typedef struct {
    int   index;     /**< app slot index                    */
    float x, y;      /**< resolved center (px)             */
    float scale;     /**< visual scale (1.0 = base)        */
    float opacity;   /**< 0..1                             */
    bool  visible;
} eos_arclist_item_t;

typedef struct {
    const eos_display_profile_t *profile;
    int    count;
    float  scroll;           /**< scroll offset in arc-degrees        */
    eos_scroller_t scroller;  /**< physics driver (pos == scroll)      */
    eos_arclist_item_t items[EOS_ARCLIST_MAX];
    float  arc_radius_factor; /**< R = safe_radius * factor            */
    float  step_deg;          /**< angle between adjacent apps         */
    float  focus_scale;       /**< scale at focus                      */
    float  min_scale;
    float  visible_deg;       /**< hide beyond this angular distance   */
    float  item_radius;       /**< base item radius (px at scale 1.0),
                                    used to clamp item centers inside the
                                    safe area; defaults to 22*dp_scale.    */
} eos_arclist_t;

void eos_arclist_init(eos_arclist_t *al, const eos_display_profile_t *p, int count);
/** @brief Override the item radius (px at scale 1.0) used for safe-area clamp. */
void eos_arclist_set_item_radius(eos_arclist_t *al, float radius_px);
/** @brief Recompute every item's geometry from the current scroll. */
void eos_arclist_layout(eos_arclist_t *al);
/** @brief Finger drag: dy_px (screen px) -> scroll change. */
void eos_arclist_drag(eos_arclist_t *al, float dy_px, float dt);
void eos_arclist_release(eos_arclist_t *al);
void eos_arclist_step(eos_arclist_t *al, float dt);
/** @brief Index of the app currently at the focus (center). */
int  eos_arclist_focus_index(const eos_arclist_t *al);

#ifdef __cplusplus
}
#endif
#endif /* EOS_ARCLIST_H */
