/**
 * @file cos_audio_decoder_wav.c
 * @brief WAV decoder: RIFF/WAVE PCM audio file decoder
 */

#include "cos_audio_decoder_wav.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_mem.h"
#define COS_LOG_TAG "AudioDecWAV"
#include "cos_log.h"
#include "cos_audio_decoder.h"
#include "cos_fs_port.h"

/* Macros and Definitions -------------------------------------*/

#pragma pack(push, 1)
typedef struct
{
    uint8_t riff[4];
    uint32_t file_size;
    uint8_t wave[4];
} wav_riff_header_t;

typedef struct
{
    uint8_t id[4];
    uint32_t size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_chunk_t;

typedef struct
{
    uint8_t id[4];
    uint32_t size;
} wav_data_chunk_t;
#pragma pack(pop)

/* Variables --------------------------------------------------*/

typedef struct
{
    cos_file_t file;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t read_pos;
    uint32_t bytes_per_sample;
} wav_dec_data_t;

/* Function Implementations -----------------------------------*/

static cos_result_t _wav_probe(const void *src, cos_audio_src_type_t src_type, cos_audio_format_t *format)
{
    if (src_type != COS_AUDIO_SRC_FILE)
        return COS_FAILED;
    if (src == NULL)
        return COS_FAILED;

    COS_LOG_I("WAV probe: checking %s", (const char *)src);

    const char *path = (const char *)src;
    cos_file_t f = cos_fs_open_read(path);
    if (f == COS_FILE_INVALID)
    {
        COS_LOG_W("WAV probe: cannot open file: %s", path);
        return COS_FAILED;
    }

    wav_riff_header_t riff;
    if (cos_fs_read(f, &riff, sizeof(riff)) != sizeof(riff))
    {
        COS_LOG_W("WAV probe: read RIFF header failed");
        cos_fs_close(f);
        return COS_FAILED;
    }

    if (memcmp(riff.riff, "RIFF", 4) != 0 || memcmp(riff.wave, "WAVE", 4) != 0)
    {
        COS_LOG_W("WAV probe: not a RIFF/WAVE file");
        cos_fs_close(f);
        return COS_FAILED;
    }

    uint8_t chunk_id[4];
    uint32_t chunk_size;
    uint32_t offset = sizeof(wav_riff_header_t);
    bool found_fmt = false;

    while (!found_fmt && offset < sizeof(wav_riff_header_t) + 4096)
    {
        if (cos_fs_seek(f, offset) != COS_OK)
            break;
        if (cos_fs_read(f, chunk_id, 4) != 4)
            break;
        if (cos_fs_read(f, &chunk_size, 4) != 4)
            break;
        offset += 8;

        if (memcmp(chunk_id, "fmt ", 4) == 0)
        {
            wav_fmt_chunk_t fmt;
            if (cos_fs_read(f, &fmt, sizeof(fmt)) != sizeof(fmt))
                break;

            if (fmt.audio_format != 1)
            {
                cos_fs_close(f);
                return COS_FAILED;
            }

            format->sample_rate = fmt.sample_rate;
            format->channels = (uint8_t)fmt.num_channels;
            format->bits_per_sample = (uint8_t)fmt.bits_per_sample;
            found_fmt = true;
        }
        offset += chunk_size;
    }

    cos_fs_close(f);

    if (!found_fmt)
    {
        COS_LOG_W("WAV probe: no fmt chunk found");
        return COS_FAILED;
    }

    COS_LOG_I("WAV probe: success (%dHz %dch %dbits)", format->sample_rate, format->channels, format->bits_per_sample);
    format->total_samples = 0;
    format->duration_ms = 0;
    return COS_OK;
}

static cos_result_t _wav_open(cos_audio_decoder_dsc_t *dsc)
{
    const char *path = (const char *)dsc->src;

    wav_dec_data_t *wd = cos_malloc_zeroed(sizeof(wav_dec_data_t));
    if (!wd)
        return COS_ERR_MEM;

    wd->file = cos_fs_open_read(path);
    if (wd->file == COS_FILE_INVALID)
    {
        cos_free(wd);
        return COS_ERR_IO;
    }

    uint8_t chunk_id[4];
    uint32_t chunk_size;
    uint32_t offset = sizeof(wav_riff_header_t);

    while (offset < 0x10000000)
    {
        if (cos_fs_seek(wd->file, offset) != COS_OK)
            break;
        if (cos_fs_read(wd->file, chunk_id, 4) != 4)
            break;
        if (cos_fs_read(wd->file, &chunk_size, 4) != 4)
            break;

        if (memcmp(chunk_id, "fmt ", 4) != 0 && memcmp(chunk_id, "data", 4) != 0)
        {
            offset += 8 + chunk_size;
            continue;
        }

        if (memcmp(chunk_id, "data", 4) == 0)
        {
            wd->data_offset = offset + 8;
            wd->data_size = chunk_size;
            break;
        }

        offset += 8 + chunk_size;
    }

    if (wd->data_offset == 0)
    {
        cos_fs_close(wd->file);
        cos_free(wd);
        return COS_ERR_FILE_ERROR;
    }

    wd->bytes_per_sample = (dsc->format.bits_per_sample / 8) * dsc->format.channels;
    wd->read_pos = 0;

    if (wd->data_size > 0 && wd->bytes_per_sample > 0)
    {
        dsc->format.total_samples = wd->data_size / wd->bytes_per_sample;
        dsc->format.duration_ms = (uint32_t)((uint64_t)dsc->format.total_samples * 1000 / dsc->format.sample_rate);
    }

    dsc->user_data = wd;

    COS_LOG_I("WAV decoder opened: %dHz %dch %dbits, %d samples",
              dsc->format.sample_rate,
              dsc->format.channels,
              dsc->format.bits_per_sample,
              dsc->format.total_samples);
    return COS_OK;
}

static cos_result_t _wav_read(cos_audio_decoder_dsc_t *dsc, void *buf, uint32_t buf_size, uint32_t *bytes_read)
{
    wav_dec_data_t *wd = (wav_dec_data_t *)dsc->user_data;
    if (!wd)
        return COS_ERR_INVALID_STATE;

    if (wd->read_pos >= wd->data_size)
    {
        *bytes_read = 0;
        return COS_OK;
    }

    uint32_t remaining = wd->data_size - wd->read_pos;
    uint32_t to_read = (buf_size < remaining) ? buf_size : remaining;

    int ret = cos_fs_read(wd->file, buf, to_read);
    if (ret < 0)
        return COS_ERR_FILE_ERROR;

    wd->read_pos += (uint32_t)ret;
    *bytes_read = (uint32_t)ret;

    dsc->current_sample += (uint32_t)ret / wd->bytes_per_sample;
    return COS_OK;
}

static void _wav_close(cos_audio_decoder_dsc_t *dsc)
{
    wav_dec_data_t *wd = (wav_dec_data_t *)dsc->user_data;
    if (!wd)
        return;

    if (wd->file != COS_FILE_INVALID)
    {
        cos_fs_close(wd->file);
    }
    cos_free(wd);
    dsc->user_data = NULL;
}

static cos_result_t _wav_seek(cos_audio_decoder_dsc_t *dsc, uint32_t sample)
{
    wav_dec_data_t *wd = (wav_dec_data_t *)dsc->user_data;
    if (!wd)
        return COS_ERR_INVALID_STATE;

    uint32_t byte_offset = sample * wd->bytes_per_sample;
    if (byte_offset >= wd->data_size)
        return COS_ERR_INVALID_ARG;

    int ret = cos_fs_seek(wd->file, wd->data_offset + byte_offset);
    if (ret != 0)
        return COS_ERR_FILE_ERROR;

    wd->read_pos = byte_offset;
    dsc->current_sample = sample;
    return COS_OK;
}

void cos_audio_decoder_wav_init(void)
{
    cos_audio_decoder_t *dec = cos_audio_decoder_create();
    cos_audio_decoder_set_probe_cb(dec, _wav_probe);
    cos_audio_decoder_set_open_cb(dec, _wav_open);
    cos_audio_decoder_set_read_cb(dec, _wav_read);
    cos_audio_decoder_set_close_cb(dec, _wav_close);
    cos_audio_decoder_set_seek_cb(dec, _wav_seek);
    dec->name = "WAV";
    COS_LOG_I("Registered WAV decoder");
}
