/**
 * @file cos_audio_feed.h
 * @brief Audio feed clock abstraction - decouples audio pipeline from LVGL timer
 *
 * Default implementation uses LVGL lv_timer as fallback.
 * Platform ports can override with DMA half-complete ISR or RTOS timer
 * via COS_WEAK function redefinition.
 */

#ifndef COS_AUDIO_FEED_H
#define COS_AUDIO_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef void (*cos_audio_feed_cb_t)(void *user_data);

typedef struct cos_audio_feed cos_audio_feed_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Create a periodic feed clock
 * @param period_ms Interval in milliseconds (recommended 10-30ms)
 * @param cb Callback invoked each period
 * @param user_data User data passed to callback
 * @return Feed handle, or NULL on failure
 *
 * Default implementation uses lv_timer_create (COS_WEAK).
 * Override to use DMA ISR / RTOS timer on real hardware.
 */
cos_audio_feed_t *cos_audio_feed_create(uint32_t period_ms, cos_audio_feed_cb_t cb, void *user_data);

/**
 * @brief Delete feed clock and stop callbacks
 */
void cos_audio_feed_delete(cos_audio_feed_t *feed);

/**
 * @brief Pause the feed clock (no callbacks)
 */
void cos_audio_feed_pause(cos_audio_feed_t *feed);

/**
 * @brief Resume the feed clock
 */
void cos_audio_feed_resume(cos_audio_feed_t *feed);

#ifdef __cplusplus
}
#endif

#endif /* COS_AUDIO_FEED_H */
