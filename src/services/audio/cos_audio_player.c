/**
 * @file cos_audio_player.c
 * @brief Audio player implementation
 */
#include "cos_audio_player.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_mem.h"
#define COS_LOG_TAG "AudioPlayer"
#include "cos_log.h"
#include "cos_dev_speaker.h"
#include "cos_audio_feed.h"

/* Macros and Definitions -------------------------------------*/

#define FEED_TIMER_PERIOD_MS 30
#define PRE_FILL_BUFFERS 8

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

static void _player_stop_internal(cos_audio_player_t *p);

static bool _player_fill_one_buffer(cos_audio_player_t *p)
{
    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk == NULL || spk->ops == NULL)
        return false;

    void *buf = NULL;
    uint32_t cap = 0;
    if (spk->ops->borrow == NULL || spk->ops->borrow(&buf, &cap) != 0)
        return false;

    if (p->muted)
    {
        memset(buf, 0, cap);
        spk->ops->enqueue(buf, cap);
        return true;
    }

    uint32_t bytes_read = 0;
    cos_result_t res = cos_audio_decoder_read(&p->dsc, buf, cap, &bytes_read);

    if (res == COS_OK && bytes_read == 0 && p->dsc.format.total_samples > 0
        && p->dsc.current_sample < p->dsc.format.total_samples)
    {
        p->dsc.current_sample = p->dsc.format.total_samples;
    }

    spk->ops->enqueue(buf, bytes_read);
    return (res == COS_OK && bytes_read > 0);
}

static void _feed_cb(void *user_data)
{
    cos_audio_player_t *p = (cos_audio_player_t *)user_data;
    if (p->state != COS_AUDIO_PLAYING)
        return;

    while (_player_fill_one_buffer(p))
    {
    }

    if (p->dsc.current_sample >= p->dsc.format.total_samples && p->dsc.format.total_samples > 0)
    {
        COS_LOG_I("Playback complete");
        _player_stop_internal(p);
        if (p->done_cb)
        {
            p->done_cb(p, p->done_user_data);
        }
    }
}

static void _player_stop_internal(cos_audio_player_t *p)
{
    if (p->feed)
    {
        cos_audio_feed_delete(p->feed);
        p->feed = NULL;
    }

    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk && spk->ops && spk->ops->stop)
    {
        spk->ops->stop();
    }

    if (p->decoder_open)
    {
        cos_audio_decoder_close(&p->dsc);
        p->decoder_open = false;
    }

    p->state = COS_AUDIO_IDLE;
}

static void _player_start_feed(cos_audio_player_t *p)
{
    if (p->feed)
    {
        cos_audio_feed_delete(p->feed);
    }
    p->feed = cos_audio_feed_create(FEED_TIMER_PERIOD_MS, _feed_cb, p);
}

void cos_audio_player_init(cos_audio_player_t *p)
{
    if (p == NULL)
        return;
    memset(p, 0, sizeof(*p));
    p->state = COS_AUDIO_IDLE;
    p->volume = 50;
    p->muted = false;
    COS_LOG_I("Audio player initialized");
}

cos_result_t cos_audio_player_play(cos_audio_player_t *p, const void *src, cos_audio_src_type_t src_type)
{
    if (p == NULL || src == NULL)
        return COS_ERR_INVALID_ARG;

    if (src_type == COS_AUDIO_SRC_NONE)
    {
        src_type = cos_audio_src_get_type(src);
        if (src_type == COS_AUDIO_SRC_NONE)
            return COS_ERR_INVALID_ARG;
    }

    if (p->state != COS_AUDIO_IDLE)
    {
        _player_stop_internal(p);
    }

    if (p->cached_src && p->cached_src_type == COS_AUDIO_SRC_FILE)
    {
        cos_free(p->cached_src);
    }
    p->cached_src = NULL;
    if (src_type == COS_AUDIO_SRC_FILE && src)
    {
        p->cached_src = cos_strdup((const char *)src);
        p->cached_src_type = COS_AUDIO_SRC_FILE;
    }
    else
    {
        p->cached_src = NULL;
        p->cached_src_type = COS_AUDIO_SRC_NONE;
    }

    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk == NULL || spk->ops == NULL)
    {
        return COS_ERR_DEV_NOT_FOUND;
    }

    cos_result_t res = cos_audio_decoder_open(&p->dsc, src, src_type);
    if (res != COS_OK)
    {
        COS_LOG_E("Failed to open decoder");
        return res;
    }
    p->decoder_open = true;

    int ret = spk->ops->open(p->dsc.format.sample_rate, p->dsc.format.channels, p->dsc.format.bits_per_sample);
    if (ret != 0)
    {
        COS_LOG_E("Failed to open speaker");
        cos_audio_decoder_close(&p->dsc);
        p->decoder_open = false;
        return COS_ERR_DEV_ERROR;
    }

    if (spk->ops->set_volume)
    {
        spk->ops->set_volume(p->volume);
    }

    int filled = 0;
    for (int i = 0; i < PRE_FILL_BUFFERS; i++)
    {
        if (_player_fill_one_buffer(p))
            filled++;
        else
            break;
    }

    if (filled == 0 && p->dsc.format.total_samples == 0)
    {
        COS_LOG_E("Cannot play: source has no samples and unknown duration");
        if (spk->ops->stop)
            spk->ops->stop();
        cos_audio_decoder_close(&p->dsc);
        p->decoder_open = false;
        p->state = COS_AUDIO_IDLE;
        return COS_ERR_DEV_ERROR;
    }

    p->state = COS_AUDIO_PLAYING;
    _player_start_feed(p);

    COS_LOG_I("Playing: %s", (const char *)src);

    return COS_OK;
}

cos_result_t cos_audio_player_stop(cos_audio_player_t *p)
{
    if (p == NULL)
        return COS_ERR_INVALID_ARG;
    _player_stop_internal(p);
    if (p->cached_src_type == COS_AUDIO_SRC_FILE && p->cached_src)
    {
        cos_free(p->cached_src);
    }
    p->cached_src = NULL;
    p->cached_src_type = COS_AUDIO_SRC_NONE;
    return COS_OK;
}

cos_result_t cos_audio_player_pause(cos_audio_player_t *p)
{
    if (p == NULL)
        return COS_ERR_INVALID_ARG;
    if (p->state != COS_AUDIO_PLAYING)
        return COS_ERR_INVALID_STATE;

    cos_audio_feed_pause(p->feed);

    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk && spk->ops && spk->ops->pause)
    {
        spk->ops->pause();
    }
    p->state = COS_AUDIO_PAUSED;
    return COS_OK;
}

cos_result_t cos_audio_player_resume(cos_audio_player_t *p)
{
    if (p == NULL)
        return COS_ERR_INVALID_ARG;
    if (p->state != COS_AUDIO_PAUSED)
        return COS_ERR_INVALID_STATE;

    for (int i = 0; i < PRE_FILL_BUFFERS; i++)
    {
        if (!_player_fill_one_buffer(p))
            break;
    }

    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk && spk->ops && spk->ops->resume)
    {
        spk->ops->resume();
    }
    p->state = COS_AUDIO_PLAYING;
    cos_audio_feed_resume(p->feed);
    return COS_OK;
}

cos_result_t cos_audio_player_seek(cos_audio_player_t *p, uint32_t sample)
{
    if (p == NULL)
        return COS_ERR_INVALID_ARG;
    if (!p->decoder_open)
        return COS_ERR_INVALID_STATE;

    uint32_t total = p->dsc.format.total_samples;
    if (total == 0)
        return COS_ERR_INVALID_STATE;
    if (sample >= total)
        sample = total - 1;

    if (p->dsc.decoder && p->dsc.decoder->seek_cb)
    {
        cos_result_t res = cos_audio_decoder_seek(&p->dsc, sample);
        if (res == COS_OK)
        {
            COS_LOG_I("Seeked to sample %u / %u (native)", sample, total);
            return COS_OK;
        }
        /* Fall through: decoder has seek_cb but it failed — use reopen fallback */
    }

    cos_audio_player_state_t prev_state = p->state;

    if (p->feed)
    {
        cos_audio_feed_delete(p->feed);
        p->feed = NULL;
    }

    if (p->decoder_open)
    {
        cos_audio_decoder_close(&p->dsc);
        p->decoder_open = false;
    }

    cos_result_t res = cos_audio_decoder_open(&p->dsc, p->cached_src, p->cached_src_type);
    if (res != COS_OK)
    {
        COS_LOG_E("Seek: failed to re-open decoder");
        p->state = COS_AUDIO_IDLE;
        return res;
    }
    p->decoder_open = true;

    if (sample > 0)
    {
        uint32_t bytes_per_frame = (p->dsc.format.bits_per_sample / 8) * p->dsc.format.channels;
        uint32_t aligned_chunk = bytes_per_frame * 512;
        if (aligned_chunk < 1024)
            aligned_chunk = 1024;
        uint32_t to_skip = sample * bytes_per_frame;
        uint32_t skipped = 0;
        uint8_t *discard = cos_malloc(aligned_chunk);

        if (discard)
        {
            while (skipped < to_skip)
            {
                uint32_t chunk = (to_skip - skipped) > aligned_chunk ? aligned_chunk : (to_skip - skipped);
                uint32_t bytes_read = 0;
                res = cos_audio_decoder_read(&p->dsc, discard, chunk, &bytes_read);
                if (res != COS_OK || bytes_read == 0)
                    break;
                skipped += bytes_read;
            }
            cos_free(discard);
        }
    }
    p->dsc.current_sample = sample;

    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk && spk->ops)
    {
        int ret = spk->ops->open(p->dsc.format.sample_rate, p->dsc.format.channels, p->dsc.format.bits_per_sample);
        if (ret != 0)
        {
            cos_audio_decoder_close(&p->dsc);
            p->decoder_open = false;
            p->state = COS_AUDIO_IDLE;
            return COS_ERR_DEV_ERROR;
        }

        if (spk->ops->set_volume)
        {
            spk->ops->set_volume(p->volume);
        }
    }

    if (prev_state != COS_AUDIO_IDLE)
    {
        for (int i = 0; i < PRE_FILL_BUFFERS; i++)
        {
            if (!_player_fill_one_buffer(p))
                break;
        }
    }

    p->state = prev_state;
    if (prev_state != COS_AUDIO_IDLE)
    {
        _player_start_feed(p);
    }

    COS_LOG_I("Seeked to sample %u / %u", sample, total);
    return COS_OK;
}

cos_audio_player_state_t cos_audio_player_get_state(cos_audio_player_t *p)
{
    if (p == NULL)
        return COS_AUDIO_IDLE;
    return p->state;
}

uint32_t cos_audio_player_get_position(cos_audio_player_t *p)
{
    if (p == NULL || !p->decoder_open)
        return 0;
    return p->dsc.current_sample;
}

uint32_t cos_audio_player_get_duration(cos_audio_player_t *p)
{
    if (p == NULL || !p->decoder_open)
        return 0;
    return p->dsc.format.total_samples;
}

uint32_t cos_audio_player_get_sample_rate(cos_audio_player_t *p)
{
    if (p == NULL || !p->decoder_open)
        return 0;
    return p->dsc.format.sample_rate;
}

cos_result_t cos_audio_player_set_volume(cos_audio_player_t *p, uint8_t vol)
{
    if (p == NULL)
        return COS_ERR_INVALID_ARG;
    if (vol > 100)
        vol = 100;
    p->volume = vol;
    return COS_OK;
}

void cos_audio_player_apply_volume(cos_audio_player_t *p)
{
    if (p == NULL)
        return;
    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk && spk->ops && spk->ops->set_volume)
    {
        spk->ops->set_volume(p->volume);
    }
}

void cos_audio_player_set_mute(cos_audio_player_t *p, bool mute)
{
    if (p == NULL)
        return;
    p->muted = mute;
}

bool cos_audio_player_is_muted(cos_audio_player_t *p)
{
    if (p == NULL)
        return false;
    return p->muted;
}

void cos_audio_player_set_done_callback(cos_audio_player_t *p, cos_audio_player_done_cb cb, void *user_data)
{
    if (p == NULL)
        return;
    p->done_cb = cb;
    p->done_user_data = user_data;
}

void cos_audio_player_save_state(cos_audio_player_t *p,
                                 void **saved_src,
                                 cos_audio_src_type_t *saved_type,
                                 uint32_t *saved_pos)
{
    if (p == NULL || saved_src == NULL || saved_type == NULL || saved_pos == NULL)
        return;
    *saved_src = p->cached_src;
    *saved_type = p->cached_src_type;
    *saved_pos = p->dsc.current_sample;
    p->cached_src = NULL;
    p->cached_src_type = COS_AUDIO_SRC_NONE;
}

cos_result_t cos_audio_player_restore_state(cos_audio_player_t *p,
                                            void *src,
                                            cos_audio_src_type_t src_type,
                                            uint32_t position)
{
    if (p == NULL || src == NULL)
        return COS_ERR_INVALID_ARG;
    if (p->state != COS_AUDIO_IDLE)
        return COS_ERR_INVALID_STATE;

    cos_result_t r = cos_audio_player_play(p, src, src_type);
    if (r != COS_OK)
        return r;

    if (position > 0)
    {
        r = cos_audio_player_seek(p, position);
    }
    return r;
}
