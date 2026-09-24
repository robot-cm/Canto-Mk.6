/**
 * @file cos_ui_anim.h
 * @brief Animation system — smooth easing curves + LVGL page transitions (req #3).
 *
 * NOTE: named cos_ui_anim (not cos_anim) to avoid colliding with the existing
 * src/ui/widgets/anim/cos_anim.h once both directories sit on the include path.
 *
 * The easing functions are pure math and unit-testable without LVGL. The LVGL
 * bindings implement the spec'd page transition:
 *   old page: opacity 100 -> 0
 *   new page: scale 0.8 -> 1.0, opacity 0 -> 100  (smooth easing)
 */
#ifndef COS_UI_ANIM_H
#define COS_UI_ANIM_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure easing curves: map t in [0,1] -> eased [0,1]. */
float cos_ease_linear(float t);
float cos_ease_in_out_cubic(float t);
float cos_ease_out_cubic(float t);
float cos_ease_out_back(float t);     /**< overshoot, for a springy feel */
float cos_ease_out_bounce(float t);   /**< real bounce settle           */

/** @brief Interpolate from->to using an easing curve. */
float cos_ease_map(float from, float to, float t, float (*ease)(float));

/* LVGL page transitions (require a live lv_obj). */
void cos_anim_page_close(void *old_obj, unsigned int duration_ms);
void cos_anim_page_open(void *new_obj, unsigned int duration_ms);

/**
 * @brief Add a synchronized opacity + scale fade to an animation timeline.
 *        Used for page transitions (e.g. APP_LIST -> WATCHFACE close:
 *        opacity 255->0 and scale 1.0->0.8). opa/scale values are LVGL
 *        raw integers (255 = opaque, 256 = 100% scale).
 * @param at        timeline to add the anims to (created by the caller)
 * @param obj       target lv_obj
 * @param from_opa/to_opa   opacity range (0..255)
 * @param from_scale/to_scale scale range (256 == 1.0)
 * @param delay_ms  start offset inside the timeline
 * @param duration_ms anim duration
 */
void cos_anim_add_fade_scale(lv_anim_timeline_t *at, void *obj,
                             int from_opa, int to_opa,
                             int from_scale, int to_scale,
                             unsigned int delay_ms, unsigned int duration_ms);

#ifdef __cplusplus
}
#endif
#endif /* COS_UI_ANIM_H */
