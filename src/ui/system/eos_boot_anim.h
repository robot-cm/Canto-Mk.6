/**
 * @file eos_boot_anim.h
 * @brief Boot self-test animation (plays once at startup, non-blocking)
 *
 * Timeline (total 4.0s):
 *   Stage 0 (0.0s - 2.0s): full-screen solid color flashes (blue/red/yellow/green)
 *   Stage 1 (2.0s - 4.0s): typewriter "INFILTRATING" (1.8s) on deep-space-grey
 *                          background with static scanlines, then whole overlay
 *                          fades out (~0.2s) and the done callback fires.
 *
 * Design notes:
 *   - Driven purely by lv_timer / lv_anim: never blocks the main loop.
 *   - Every stage frees its UI objects as soon as the stage completes.
 *   - The sequence plays exactly once per boot (re-entry guarded).
 *   - The main UI is NOT started by the caller until the done callback fires,
 *     so the watchface is never visible while the animation is still running.
 */

#ifndef EOS_BOOT_ANIM_H
#define EOS_BOOT_ANIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** Called from an lv_timer context once the whole sequence has finished. */
typedef void (*eos_boot_anim_done_cb_t)(void);

/**
 * @brief Start the boot self-test animation sequence.
 * @param done_cb Optional callback invoked (in lv_timer context) after the
 *                sequence completes. The caller should keep any heavy work
 *                (e.g. initializing the activity controller) out of the timer
 *                callback; a simple flag set is recommended.
 * @note Plays exactly one sequence (guarded against re-entry). The animation
 *       renders into the topmost LVGL layer and fully blocks the underlying
 *       screen until the final fade-out.
 */
void eos_boot_anim_start(eos_boot_anim_done_cb_t done_cb);

/**
 * @brief Check whether the boot animation is still running.
 * @return true if the sequence is in progress
 */
bool eos_boot_anim_running(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_BOOT_ANIM_H */
