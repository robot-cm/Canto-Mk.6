/**
 * @file eos_round_clip.h
 * @brief Shared helper to clip a full-screen view to the round display.
 *
 * Every full-screen activity root (Home, Apps, Gallery, Files, and plugin
 * views) should call eos_round_clip() on its view so the square SDL/MCU
 * framebuffer corners are clipped to a circle and painted black — giving a
 * consistent circular-watch look instead of each page re-implementing the
 * rounding (previously only the Launcher did this locally).
 */

#ifndef EOS_ROUND_CLIP_H
#define EOS_ROUND_CLIP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clip a view to the round display.
 *
 * Rounds the view's corners (radius = EOS_DISPLAY_RADIUS) and clips drawing
 * to that circle, so the four square corners become a black bezel. Also
 * disables self-scrolling (the page/carousel handles paging).
 *
 * @param view The activity view (or any full-screen container) to clip.
 */
void eos_round_clip(lv_obj_t *view);

#ifdef __cplusplus
}
#endif

#endif /* EOS_ROUND_CLIP_H */
