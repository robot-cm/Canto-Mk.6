/**
 * @file cos_audio_feed.c
 * @brief Audio feed clock default implementation using LVGL lv_timer
 *
 * All functions are COS_WEAK. Platform ports may override with DMA ISR
 * or RTOS timer implementations by providing strong definitions.
 */

#include "cos_audio_feed.h"

/* Includes ---------------------------------------------------*/
#include <stdlib.h>
#include "cos_mem.h"
#define COS_LOG_TAG "AudioFeed"
#include "cos_log.h"
#include "cos_port.h"
#include "lvgl.h"

/* Macros and Definitions -------------------------------------*/

struct cos_audio_feed
{
    lv_timer_t *timer;
    cos_audio_feed_cb_t cb;
    void *user_data;
    bool paused;
};

/* Function Implementations -----------------------------------*/

static void _feed_timer_cb(lv_timer_t *timer)
{
    cos_audio_feed_t *feed = (cos_audio_feed_t *)lv_timer_get_user_data(timer);
    if (feed && feed->cb && !feed->paused)
    {
        feed->cb(feed->user_data);
    }
}

COS_WEAK cos_audio_feed_t *cos_audio_feed_create(uint32_t period_ms, cos_audio_feed_cb_t cb, void *user_data)
{
    cos_audio_feed_t *feed = cos_malloc_zeroed(sizeof(cos_audio_feed_t));
    if (!feed)
        return NULL;

    feed->cb = cb;
    feed->user_data = user_data;
    feed->paused = false;
    feed->timer = lv_timer_create(_feed_timer_cb, period_ms, feed);
    if (!feed->timer)
    {
        cos_free(feed);
        return NULL;
    }
    return feed;
}

COS_WEAK void cos_audio_feed_delete(cos_audio_feed_t *feed)
{
    if (!feed)
        return;
    if (feed->timer)
    {
        lv_timer_delete(feed->timer);
    }
    cos_free(feed);
}

COS_WEAK void cos_audio_feed_pause(cos_audio_feed_t *feed)
{
    if (!feed)
        return;
    feed->paused = true;
}

COS_WEAK void cos_audio_feed_resume(cos_audio_feed_t *feed)
{
    if (!feed)
        return;
    feed->paused = false;
}
