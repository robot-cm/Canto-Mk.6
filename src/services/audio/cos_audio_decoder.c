/**
 * @file cos_audio_decoder.c
 * @brief Audio decoder registry - linked list of decoder plugins
 */

#include "cos_audio_decoder.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "AudioDecoder"
#include "cos_log.h"
#include "lvgl.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/
static lv_ll_t _decoder_ll;
static bool _decoder_initialized = false;

/* Function Implementations -----------------------------------*/

void cos_audio_decoder_init(void)
{
    if (_decoder_initialized)
        return;
    lv_ll_init(&_decoder_ll, sizeof(cos_audio_decoder_t));
    _decoder_initialized = true;
    COS_LOG_I("Audio decoder subsystem initialized");
}

cos_audio_src_type_t cos_audio_src_get_type(const void *src)
{
    if (src == NULL)
        return COS_AUDIO_SRC_NONE;
    return COS_AUDIO_SRC_FILE;
}

cos_audio_decoder_t *cos_audio_decoder_create(void)
{
    if (!_decoder_initialized)
        return NULL;

    cos_audio_decoder_t *decoder = lv_ll_ins_head(&_decoder_ll);
    if (decoder)
    {
        lv_memzero(decoder, sizeof(cos_audio_decoder_t));
    }
    return decoder;
}

void cos_audio_decoder_delete(cos_audio_decoder_t *decoder)
{
    if (!_decoder_initialized || decoder == NULL)
        return;
    lv_ll_remove(&_decoder_ll, decoder);
}

cos_audio_decoder_t *cos_audio_decoder_get_next(cos_audio_decoder_t *decoder)
{
    if (!_decoder_initialized)
        return NULL;
    if (decoder == NULL)
        return lv_ll_get_head(&_decoder_ll);
    return lv_ll_get_next(&_decoder_ll, decoder);
}

void cos_audio_decoder_set_probe_cb(cos_audio_decoder_t *d, cos_audio_decoder_probe_f_t cb)
{
    if (d)
        d->probe_cb = cb;
}

void cos_audio_decoder_set_open_cb(cos_audio_decoder_t *d, cos_audio_decoder_open_f_t cb)
{
    if (d)
        d->open_cb = cb;
}

void cos_audio_decoder_set_read_cb(cos_audio_decoder_t *d, cos_audio_decoder_read_f_t cb)
{
    if (d)
        d->read_cb = cb;
}

void cos_audio_decoder_set_close_cb(cos_audio_decoder_t *d, cos_audio_decoder_close_f_t cb)
{
    if (d)
        d->close_cb = cb;
}

void cos_audio_decoder_set_seek_cb(cos_audio_decoder_t *d, cos_audio_decoder_seek_f_t cb)
{
    if (d)
        d->seek_cb = cb;
}

static cos_audio_decoder_t *_decoder_find_match(const void *src,
                                                cos_audio_src_type_t src_type,
                                                cos_audio_format_t *format)
{
    cos_audio_decoder_t *dec;
    LV_LL_READ(&_decoder_ll, dec)
    {
        if (dec->probe_cb == NULL)
            continue;
        if (dec->probe_cb(src, src_type, format) == COS_OK)
        {
            return dec;
        }
    }
    return NULL;
}

cos_result_t cos_audio_decoder_open(cos_audio_decoder_dsc_t *dsc, const void *src, cos_audio_src_type_t src_type)
{
    if (!_decoder_initialized)
        return COS_ERR_NOT_INITIALIZED;
    if (dsc == NULL || src == NULL)
        return COS_ERR_INVALID_ARG;

    if (src_type == COS_AUDIO_SRC_NONE)
    {
        src_type = cos_audio_src_get_type(src);
    }

    lv_memzero(dsc, sizeof(cos_audio_decoder_dsc_t));
    dsc->src = src;
    dsc->src_type = src_type;

    cos_audio_decoder_t *dec = _decoder_find_match(src, src_type, &dsc->format);
    if (dec == NULL)
    {
        COS_LOG_E("No decoder found for source");
        return COS_ERR_NOT_FOUND;
    }

    dsc->decoder = dec;
    if (dec->open_cb == NULL)
        return COS_ERR_NOT_FOUND;

    return dec->open_cb(dsc);
}

cos_result_t cos_audio_decoder_read(cos_audio_decoder_dsc_t *dsc, void *buf, uint32_t buf_size, uint32_t *bytes_read)
{
    if (!_decoder_initialized)
        return COS_ERR_NOT_INITIALIZED;
    if (dsc == NULL || dsc->decoder == NULL || dsc->decoder->read_cb == NULL)
        return COS_ERR_INVALID_ARG;
    return dsc->decoder->read_cb(dsc, buf, buf_size, bytes_read);
}

cos_result_t cos_audio_decoder_seek(cos_audio_decoder_dsc_t *dsc, uint32_t sample)
{
    if (!_decoder_initialized)
        return COS_ERR_NOT_INITIALIZED;
    if (dsc == NULL || dsc->decoder == NULL)
        return COS_ERR_INVALID_ARG;
    if (dsc->decoder->seek_cb == NULL)
        return COS_ERR_DEV_OPS_NOT_SUPPORTED;
    return dsc->decoder->seek_cb(dsc, sample);
}

void cos_audio_decoder_close(cos_audio_decoder_dsc_t *dsc)
{
    if (!_decoder_initialized)
        return;
    if (dsc == NULL || dsc->decoder == NULL)
        return;
    if (dsc->decoder->close_cb)
    {
        dsc->decoder->close_cb(dsc);
    }
    lv_memzero(dsc, sizeof(cos_audio_decoder_dsc_t));
}
