/**
 * @file cos_service_audio.c
 * @brief Audio service - high-level API for speaker and microphone
 */
#include "cos_service_audio.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_mem.h"
#define COS_LOG_TAG "AudioService"
#include "cos_log.h"
#include "cos_dev_speaker.h"
#include "cos_dev_microphone.h"
#include "cos_audio_player.h"
#include "cos_audio_decoder.h"
#include "cos_audio_decoder_wav.h"
#include "cos_service_config.h"
#include "cos_service_state.h"
#include "cos_service_storage.h"
#include "lvgl.h"

/* Macros and Definitions -------------------------------------*/

#define MIC_RING_BUFFER_SIZE 32768
#define MIC_POLL_PERIOD_MS 50
#define MIC_DEFAULT_SAMPLE_RATE 16000
#define MIC_DEFAULT_CHANNELS 1
#define MIC_DEFAULT_BITS 16

/* Variables --------------------------------------------------*/
static bool _initialized = false;

/*
 * Player instances managed by the audio service.
 * Currently one (MEDIA), expandable to multiple channels.
 */
static cos_audio_player_t _player_media;

/* Recording state */
static struct
{
    bool active;
    cos_file_t file;
    uint8_t *ring_buf;
    uint32_t ring_buf_size;
    uint32_t read_offset;
    lv_timer_t *timer;
    char file_path[128];
} _rec;

/* Function Implementations -----------------------------------*/

void cos_service_audio_init(void)
{
    if (_initialized)
    {
        return;
    }
    _initialized = true;
    cos_audio_decoder_init();
    cos_audio_decoder_wav_init();
    cos_audio_player_init(&_player_media);
    COS_LOG_I("Audio service initialized");
}

cos_result_t cos_service_audio_set_volume(uint8_t volume)
{
    if (volume > COS_SPEAKER_VOLUME_MAX)
    {
        volume = COS_SPEAKER_VOLUME_MAX;
    }

    cos_audio_player_set_volume(&_player_media, volume);
    cos_audio_player_apply_volume(&_player_media);

    cos_config_set_number(COS_CONFIG_KEY_SPEAKER_VOLUME_NUMBER, volume);
    return COS_OK;
}

uint8_t cos_service_audio_get_volume(void)
{
    return (uint8_t)cos_config_get_number(COS_CONFIG_KEY_SPEAKER_VOLUME_NUMBER, 50);
}

void cos_service_audio_set_mute(bool mute)
{
    cos_config_set_bool(COS_CONFIG_KEY_MUTE_BOOL, mute);

    cos_audio_player_set_mute(&_player_media, mute);
}

bool cos_service_audio_is_muted(void)
{
    return cos_config_get_bool(COS_CONFIG_KEY_MUTE_BOOL, false);
}

cos_result_t cos_service_audio_play(const char *file_path)
{
    if (file_path == NULL)
    {
        return COS_ERR_INVALID_ARG;
    }

    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk == NULL || spk->ops == NULL || spk->ops->is_available == NULL)
    {
        return COS_ERR_DEV_NOT_FOUND;
    }

    if (!spk->ops->is_available())
    {
        return COS_ERR_DEV_NOT_FOUND;
    }

    return cos_audio_player_play(&_player_media, file_path, COS_AUDIO_SRC_FILE);
}

cos_result_t cos_service_audio_play_tone(uint16_t freq, uint32_t duration_ms)
{
    (void)freq;
    (void)duration_ms;
    return COS_ERR_DEV_OPS_NOT_SUPPORTED;
}

cos_result_t cos_service_audio_stop(void)
{
    return cos_audio_player_stop(&_player_media);
}

cos_result_t cos_service_audio_pause(void)
{
    return cos_audio_player_pause(&_player_media);
}

cos_result_t cos_service_audio_resume(void)
{
    return cos_audio_player_resume(&_player_media);
}

/* ---- Recording (ring-buffer producer-consumer) ------------- */

#define WAV_HEADER_SIZE 44

/**
 * Write a standard 44-byte RIFF/WAV header for 16-bit mono PCM.
 * Size fields are set to zero initially and patched on stop_recording.
 */
static void _write_wav_header(cos_file_t fp)
{
    uint8_t hdr[WAV_HEADER_SIZE];
    uint16_t channels = MIC_DEFAULT_CHANNELS;
    uint32_t sample_rate = MIC_DEFAULT_SAMPLE_RATE;
    uint16_t bits = MIC_DEFAULT_BITS;
    uint32_t byte_rate = sample_rate * channels * bits / 8;
    uint16_t block_align = channels * bits / 8;

    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "RIFF", 4);
    /* hdr[4..7] = riff size (placeholder, 0) */
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    hdr[16] = 16; /* fmt chunk size = 16 (PCM) */
    hdr[20] = 1; /* audio format = 1 (PCM) */
    hdr[22] = channels & 0xFF;
    hdr[24] = sample_rate & 0xFF;
    hdr[25] = (sample_rate >> 8) & 0xFF;
    hdr[26] = (sample_rate >> 16) & 0xFF;
    hdr[27] = (sample_rate >> 24) & 0xFF;
    hdr[28] = byte_rate & 0xFF;
    hdr[29] = (byte_rate >> 8) & 0xFF;
    hdr[30] = (byte_rate >> 16) & 0xFF;
    hdr[31] = (byte_rate >> 24) & 0xFF;
    hdr[32] = block_align;
    hdr[34] = bits;
    memcpy(hdr + 36, "data", 4);
    /* hdr[40..43] = data chunk size (placeholder, 0) */

    cos_storage_file_write(fp, hdr, WAV_HEADER_SIZE);
}

/**
 * Patch the RIFF total-size and data-chunk-size fields in the WAV header
 * after recording completes.
 */
static void _update_wav_header(cos_file_t fp)
{
    uint32_t file_size;
    if (cos_storage_file_size(fp, &file_size) != COS_OK)
        return;

    uint32_t riff_size = file_size - 8;
    uint32_t data_size = file_size - WAV_HEADER_SIZE;

    uint8_t buf[4];
    buf[0] = riff_size & 0xFF;
    buf[1] = (riff_size >> 8) & 0xFF;
    buf[2] = (riff_size >> 16) & 0xFF;
    buf[3] = (riff_size >> 24) & 0xFF;
    cos_storage_file_seek(fp, 4);
    cos_storage_file_write(fp, buf, 4);

    buf[0] = data_size & 0xFF;
    buf[1] = (data_size >> 8) & 0xFF;
    buf[2] = (data_size >> 16) & 0xFF;
    buf[3] = (data_size >> 24) & 0xFF;
    cos_storage_file_seek(fp, 40);
    cos_storage_file_write(fp, buf, 4);
}

static void _ensure_parent_dir(const char *file_path)
{
    if (!file_path)
        return;
    char tmp[128];
    strncpy(tmp, file_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *slash = strrchr(tmp, '/');
    if (slash && slash != tmp)
    {
        *slash = '\0';
        cos_storage_mkdir_recursive(tmp);
    }
}

/**
 * Read available data from the shared ring buffer and write to file.
 * Handles wrap-around: data may span buffer end → buffer start.
 */
static void _recording_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    cos_dev_microphone_t *mic = cos_dev_microphone_get_instance();
    if (!mic || !mic->ops || _rec.file == COS_FILE_INVALID)
        return;

    uint32_t write_off = mic->ops->get_write_offset();
    uint32_t avail = (write_off - _rec.read_offset + _rec.ring_buf_size) % _rec.ring_buf_size;
    if (avail == 0)
        return;

    uint32_t read_mod = _rec.read_offset % _rec.ring_buf_size;

    if (read_mod + avail <= _rec.ring_buf_size)
    {
        /* Contiguous: single write */
        cos_storage_file_write(_rec.file, _rec.ring_buf + read_mod, avail);
    }
    else
    {
        /* Wraps around: two writes */
        uint32_t first_part = _rec.ring_buf_size - read_mod;
        uint32_t second_part = avail - first_part;
        cos_storage_file_write(_rec.file, _rec.ring_buf + read_mod, first_part);
        cos_storage_file_write(_rec.file, _rec.ring_buf, second_part);
    }

    _rec.read_offset = write_off;
}

cos_result_t cos_service_audio_start_recording(const char *file_path)
{
    if (file_path == NULL)
    {
        return COS_ERR_INVALID_ARG;
    }

    if (_rec.active)
    {
        COS_LOG_W("Recording already in progress");
        return COS_ERR_ALREADY_EXISTS;
    }

    cos_dev_microphone_t *mic = cos_dev_microphone_get_instance();
    if (mic == NULL || mic->ops == NULL || mic->ops->is_available == NULL)
    {
        return COS_ERR_DEV_NOT_FOUND;
    }

    if (!mic->ops->is_available())
    {
        return COS_ERR_DEV_NOT_FOUND;
    }

    _rec.ring_buf = cos_malloc(MIC_RING_BUFFER_SIZE);
    if (!_rec.ring_buf)
    {
        return COS_ERR_MEM;
    }
    _rec.ring_buf_size = MIC_RING_BUFFER_SIZE;
    memset(_rec.ring_buf, 0, _rec.ring_buf_size);
    _rec.read_offset = 0;

    if (mic->ops->set_buffer(_rec.ring_buf, _rec.ring_buf_size) != 0)
    {
        COS_LOG_E("Failed to set mic ring buffer");
        cos_free(_rec.ring_buf);
        _rec.ring_buf = NULL;
        return COS_ERR_DEV_ERROR;
    }

    int ret = mic->ops->open(MIC_DEFAULT_SAMPLE_RATE, MIC_DEFAULT_CHANNELS, MIC_DEFAULT_BITS);
    if (ret != 0)
    {
        COS_LOG_E("Failed to open mic: %d", ret);
        cos_free(_rec.ring_buf);
        _rec.ring_buf = NULL;
        return COS_ERR_DEV_ERROR;
    }

    _ensure_parent_dir(file_path);

    _rec.file = cos_storage_file_open_write(file_path);
    if (_rec.file == COS_FILE_INVALID)
    {
        COS_LOG_E("Cannot open recording file: %s", file_path);
        mic->ops->close();
        cos_free(_rec.ring_buf);
        _rec.ring_buf = NULL;
        return COS_ERR_DEV_ERROR;
    }

    _write_wav_header(_rec.file);

    strncpy(_rec.file_path, file_path, sizeof(_rec.file_path) - 1);

    if (mic->ops->start() != 0)
    {
        COS_LOG_E("Failed to start mic DMA");
        cos_storage_file_close(_rec.file);
        _rec.file = COS_FILE_INVALID;
        mic->ops->close();
        cos_free(_rec.ring_buf);
        _rec.ring_buf = NULL;
        return COS_ERR_DEV_ERROR;
    }

    _rec.timer = lv_timer_create(_recording_timer_cb, MIC_POLL_PERIOD_MS, NULL);
    _rec.active = true;

    COS_LOG_I("Recording to: %s", file_path);
    return COS_OK;
}

cos_result_t cos_service_audio_stop_recording(void)
{
    if (!_rec.active)
    {
        return COS_OK;
    }

    cos_dev_microphone_t *mic = cos_dev_microphone_get_instance();

    if (_rec.timer)
    {
        lv_timer_delete(_rec.timer);
        _rec.timer = NULL;
    }

    if (mic && mic->ops)
    {
        /* Drain remaining data before stopping */
        _recording_timer_cb(NULL);

        mic->ops->stop();
        mic->ops->close();
    }

    if (_rec.file != COS_FILE_INVALID)
    {
        _update_wav_header(_rec.file);
        cos_storage_file_close(_rec.file);
        _rec.file = COS_FILE_INVALID;
    }

    cos_free(_rec.ring_buf);
    _rec.ring_buf = NULL;
    _rec.active = false;

    COS_LOG_I("Recording stopped");
    return COS_OK;
}

bool cos_service_audio_speaker_available(void)
{
    cos_dev_speaker_t *spk = cos_dev_speaker_get_instance();
    if (spk == NULL || spk->ops == NULL || spk->ops->is_available == NULL)
    {
        return false;
    }
    return spk->ops->is_available();
}

bool cos_service_audio_microphone_available(void)
{
    cos_dev_microphone_t *mic = cos_dev_microphone_get_instance();
    if (mic == NULL || mic->ops == NULL || mic->ops->is_available == NULL)
    {
        return false;
    }
    return mic->ops->is_available();
}

cos_audio_player_t *cos_service_audio_get_player(void)
{
    return &_player_media;
}
