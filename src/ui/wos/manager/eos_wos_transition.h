/**
 * @file eos_wos_transition.h
 * @brief WOS page transitions (see design doc §6)
 *
 * Rules:
 *  - Open:  scale 0.85→1.0 + opacity 0→255 (200ms, ease-out)
 *  - Close: scale 1.0→0.85 + opacity 255→0 (150ms, ease-in)
 *  - Slide: old page slides -100%, new page slides +100%→0 (200ms)
 *
 * IMPORTANT (LVGL9 pitfall): transform_scale on a view that has
 * border-radius + clip_corner (round clip) renders garbage. The App root is
 * a plain container, so animating it is safe — do NOT round-clip the root.
 */
#ifndef EOS_WOS_TRANSITION_H
#define EOS_WOS_TRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Play the open animation (scale+fade) on a page root.
 */
void wos_transition_open(lv_obj_t *root);

/**
 * @brief Play the close animation (shrink+fade), then call on_done.
 * @param root    Page root
 * @param on_done Called after the animation finishes (cleanup hook), may be NULL
 */
void wos_transition_close(lv_obj_t *root, lv_anim_ready_cb_t on_done);

/**
 * @brief Slide-transition between two pages.
 * @param from Old page (slides out), may be NULL
 * @param to   New page (slides in)
 * @param dir  LV_DIR_LEFT / LV_DIR_RIGHT
 */
void wos_transition_slide(lv_obj_t *from, lv_obj_t *to, lv_dir_t dir);

/**
 * @brief Pull-down reveal animation (control center style).
 * @param panel Panel to reveal (starts translated -100% above the screen)
 */
void wos_transition_pull_down(lv_obj_t *panel);

#ifdef __cplusplus
}
#endif

#endif /* EOS_WOS_TRANSITION_H */
