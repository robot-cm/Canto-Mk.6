/**
 * @file cos_spotify_audio.c
 * @brief Spotify 音频管线实现:WAV 解析 + MP3 解码 + PCM 环形缓冲 + UAC 推送。
 *
 * 交付批次:Batch 3。
 *
 * ── 线程模型 ────────────────────────────────────────────────
 *   pump() / flush_to_uac() 都在 LVGL ui_task 的 lv_timer 回调内执行。
 *   不新建任何任务。UAC 端的等时发送由 TinyUSB Host 任务完成,
 *   本层只负责把 PCM 填进环形缓冲 / 从缓冲取出交给 UAC。
 *
 * ── 环形缓冲并发 ────────────────────────────────────────────
 *   生产者 = 本层 pump()(ui_task)
 *   消费者 = 本层 flush_to_uac()(同样是 ui_task,在 tick 内紧随 pump)
 *   两者同任务,天然串行,无需锁。
 *   而 UAC 等时端点发送本身在 TinyUSB 任务里异步进行,故 write() 返回后
 *   缓冲即视为已消费(等时语义允许丢帧)。
 */
#include "cos_spotify_audio.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"

#define COS_LOG_TAG "SpotifyAudio"
#include "cos_log.h"

#include "cos_mem.h"
#include "cos_service_storage.h"
#include "cos_service_config.h"
#include "cos_spotify_uac.h"
#include "cos_spotify_mp3.h"

/* ── PCM 环形缓冲(内部 RAM + DMA 安全) ───────────────────── */

static uint8_t *s_ring = NULL;
static uint32_t s_ring_head = 0;   /* 写入位置 */
static uint32_t s_ring_tail = 0;   /* 读取位置 */

static uint32_t _ring_used(void)
{
    if (s_ring_head >= s_ring_tail)
    {
        return s_ring_head - s_ring_tail;
    }
    return COS_SPOTIFY_PCM_RING_BYTES - s_ring_tail + s_ring_head;
}

static uint32_t _ring_free(void)
{
    /* 留 1 字节区分空/满 */
    return COS_SPOTIFY_PCM_RING_BYTES - 1 - _ring_used();
}

static void _ring_reset(void)
{
    s_ring_head = 0;
    s_ring_tail = 0;
}

/** 向环形缓冲写入 n 字节(不做容量检查,调用方先查 _ring_free)。 */
static void _ring_write(const uint8_t *src, uint32_t n)
{
    uint32_t first = COS_SPOTIFY_PCM_RING_BYTES - s_ring_head;
    if (n <= first)
    {
        memcpy(s_ring + s_ring_head, src, n);
        s_ring_head = (s_ring_head + n) % COS_SPOTIFY_PCM_RING_BYTES;
    }
    else
    {
        memcpy(s_ring + s_ring_head, src, first);
        memcpy(s_ring, src + first, n - first);
        s_ring_head = n - first;
    }
}

/** 从环形缓冲取出 n 字节并推进 tail。 */
static void _ring_consume(uint32_t n)
{
    s_ring_tail = (s_ring_tail + n) % COS_SPOTIFY_PCM_RING_BYTES;
}

/* ── 状态 ─────────────────────────────────────────────────── */

static cos_spotify_fmt_t s_fmt = COS_SPOTIFY_FMT_UNKNOWN;
static bool s_open = false;
static cos_file_t s_fp_handle = COS_FILE_INVALID;   /* 当前打开的文件句柄 */

/* WAV 解析结果 */
static uint32_t s_wav_data_off = 0;    /* data chunk 起始偏移 */
static uint32_t s_wav_data_len = 0;    /* data chunk 长度     */
static uint32_t s_wav_read_off = 0;    /* 当前读到 data 内偏移 */

/* 通用音频参数 */
static uint32_t s_sample_rate = 44100;
static uint8_t s_channels = 2;
static uint8_t s_bits = 16;
static uint32_t s_duration_ms = 0;
static uint32_t s_position_ms = 0;

/* 解码器状态 */
static cos_spotify_mp3_t *s_mp3 = NULL;
static uint8_t *s_read_buf = NULL;      /* 文件读取缓冲(SD 到解码器)   */
static int16_t *s_pcm_buf = NULL;       /* 解码输出 PCM(交错立体声)    */

#define AUDIO_READ_CHUNK   (8 * 1024)
#define AUDIO_PCM_FRAMES   1152           /* MP3 一帧最大样点数           */

/* 变速实现:以 1/16 为步进维护小数累加器 */
static float s_speed = 1.0f;
static uint32_t s_speed_acc = 0;          /* 0..15 累加器 */

/* 音量(0–100):软件增益,在写入环形缓冲前施加。
 * UAC 等时端点无硬件音量,必须在解码侧处理。 */
static int s_volume = -1;                 /* -1 = 尚未从配置加载 */

/* ── 音量 ─────────────────────────────────────────────────── */

void cos_spotify_audio_set_volume(int vol)
{
    if (vol < 0)
    {
        vol = 0;
    }
    if (vol > 100)
    {
        vol = 100;
    }
    s_volume = vol;
    /* 复用系统音量配置键,保证与系统静音/音量策略一致 */
    cos_config_set_number(COS_CONFIG_KEY_SPEAKER_VOLUME_NUMBER, vol);
}

int cos_spotify_audio_get_volume(void)
{
    if (s_volume < 0)
    {
        int v = (int)cos_config_get_number(COS_CONFIG_KEY_SPEAKER_VOLUME_NUMBER, 50);
        if (v < 0)
        {
            v = 0;
        }
        if (v > 100)
        {
            v = 100;
        }
        s_volume = v;
    }
    return s_volume;
}

bool cos_spotify_audio_is_muted(void)
{
    return cos_config_get_bool(COS_CONFIG_KEY_MUTE_BOOL, false);
}

/**
 * @brief 对一段交错 16-bit PCM 施加软件增益(原地)。
 * @param pcm    PCM 缓冲
 * @param bytes  字节数
 * @note 音量 100 且未静音时直接返回,避免无谓运算。
 *       使用 16.16 定点乘法,无浮点开销。
 */
static void _apply_volume(int16_t *pcm, uint32_t bytes)
{
    int vol = cos_spotify_audio_get_volume();
    if (cos_spotify_audio_is_muted())
    {
        vol = 0;
    }
    if (vol >= 100)
    {
        return;
    }
    if (vol <= 0 || pcm == NULL || bytes < 2)
    {
        if (pcm)
        {
            memset(pcm, 0, bytes & ~1u);
        }
        return;
    }

    /* gain = vol/100,以 16.16 定点表示 */
    uint32_t gain = ((uint32_t)vol << 16) / 100u;
    uint32_t count = bytes / 2u;
    for (uint32_t i = 0; i < count; i++)
    {
        int32_t s = (int32_t)pcm[i] * (int32_t)gain;
        s >>= 16;
        if (s > 32767)
        {
            s = 32767;
        }
        if (s < -32768)
        {
            s = -32768;
        }
        pcm[i] = (int16_t)s;
    }
}

/* ── WAV 解析 ─────────────────────────────────────────────── */

/* 在 RIFF 容器内定位 fmt / data chunk。
 * 返回 true 并填好 s_sample_rate / s_channels / s_bits / s_wav_data_*。 */
static bool _wav_parse(cos_file_t fp, uint32_t size)
{
    uint8_t hdr[12];
    if (cos_storage_file_seek(fp, 0) != COS_OK)
    {
        return false;
    }
    if (cos_storage_file_read(fp, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr))
    {
        return false;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
        COS_LOG_W("WAV: bad RIFF/WAVE magic");
        return false;
    }

    uint32_t off = 12;
    bool got_fmt = false;
    bool got_data = false;
    uint16_t audio_format = 0;

    while (off + 8 <= size)
    {
        uint8_t chdr[8];
        if (cos_storage_file_seek(fp, off) != COS_OK)
        {
            return false;
        }
        if (cos_storage_file_read(fp, chdr, 8) != 8)
        {
            break;
        }
        uint32_t clen = (uint32_t)chdr[4] | ((uint32_t)chdr[5] << 8) |
                        ((uint32_t)chdr[6] << 16) | ((uint32_t)chdr[7] << 24);
        uint32_t body = off + 8;

        if (memcmp(chdr, "fmt ", 4) == 0 && clen >= 16)
        {
            uint8_t fmt[16];
            if (cos_storage_file_read(fp, fmt, 16) != 16)
            {
                return false;
            }
            audio_format = (uint16_t)(fmt[0] | (fmt[1] << 8));
            s_channels = (uint8_t)(fmt[2] | (fmt[3] << 8));
            s_sample_rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                            ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            s_bits = (uint8_t)(fmt[14] | (fmt[15] << 8));
            got_fmt = true;
        }
        else if (memcmp(chdr, "data", 4) == 0)
        {
            s_wav_data_off = body;
            s_wav_data_len = clen;
            if (body + clen > size)
            {
                s_wav_data_len = size - body;   /* 截断的 data chunk */
            }
            got_data = true;
        }

        /* chunk 按偶数字节对齐 */
        off = body + clen + (clen & 1u);
    }

    if (!got_fmt || !got_data)
    {
        COS_LOG_W("WAV: missing fmt(%d)/data(%d)", got_fmt, got_data);
        return false;
    }
    /* 只支持 PCM(1) 与 IEEE float 之外的 PCM 16bit。
     * 0xFFFE = WAVE_FORMAT_EXTENSIBLE,按 PCM 处理(常见于 48k 立体声)。 */
    if (audio_format != 1 && audio_format != 0xFFFE)
    {
        COS_LOG_W("WAV: unsupported format tag 0x%04X", audio_format);
        return false;
    }
    if (s_bits != 16)
    {
        COS_LOG_W("WAV: unsupported bits=%u (need 16)", s_bits);
        return false;
    }
    if (s_channels != 1 && s_channels != 2)
    {
        COS_LOG_W("WAV: unsupported channels=%u", s_channels);
        return false;
    }
    if (s_sample_rate < 8000 || s_sample_rate > 96000)
    {
        COS_LOG_W("WAV: unsupported rate=%lu", (unsigned long)s_sample_rate);
        return false;
    }

    s_wav_read_off = 0;
    uint32_t bytes_per_sec = s_sample_rate * s_channels * 2u;
    s_duration_ms = bytes_per_sec ? (uint32_t)(((uint64_t)s_wav_data_len * 1000u) / bytes_per_sec) : 0;
    COS_LOG_I("WAV: %lu Hz %u ch %u bit, %lu ms",
              (unsigned long)s_sample_rate, s_channels, s_bits,
              (unsigned long)s_duration_ms);
    return true;
}

/* ── WAV 解码分片 ─────────────────────────────────────────── */

static int32_t _pump_wav(cos_file_t fp)
{
    if (s_wav_read_off >= s_wav_data_len)
    {
        return -1;   /* 文件尾 */
    }

    uint32_t free_bytes = _ring_free();
    if (free_bytes == 0)
    {
        return 0;
    }

    uint32_t want = AUDIO_READ_CHUNK;
    if (want > free_bytes)
    {
        want = free_bytes;
    }
    /* 按完整 PCM 帧对齐,避免半个采样写入导致爆音 */
    uint32_t frame = s_channels * 2u;
    want = (want / frame) * frame;
    if (want == 0)
    {
        return 0;
    }

    uint32_t remain = s_wav_data_len - s_wav_read_off;
    if (want > remain)
    {
        want = (remain / frame) * frame;
    }
    if (want == 0)
    {
        return -1;
    }

    if (cos_storage_file_seek(fp, s_wav_data_off + s_wav_read_off) != COS_OK)
    {
        return -2;
    }
    ssize_t rd = cos_storage_file_read(fp, s_read_buf, want);
    if (rd <= 0)
    {
        return -2;
    }
    uint32_t n = ((uint32_t)rd / frame) * frame;
    if (n == 0)
    {
        return 0;
    }

    _apply_volume((int16_t *)s_read_buf, n);
    _ring_write(s_read_buf, n);
    s_wav_read_off += n;

    uint32_t bytes_per_ms = (s_sample_rate * frame) / 1000u;
    if (bytes_per_ms)
    {
        s_position_ms = s_wav_read_off / bytes_per_ms;
    }
    return (int32_t)n;
}

/* ── 对外 API ─────────────────────────────────────────────── */

cos_spotify_fmt_t cos_spotify_audio_probe(const char *path)
{
    if (!path)
    {
        return COS_SPOTIFY_FMT_UNKNOWN;
    }
    const char *dot = strrchr(path, '.');
    if (!dot)
    {
        return COS_SPOTIFY_FMT_UNKNOWN;
    }
    if (strcasecmp(dot, ".mp3") == 0)
    {
        return COS_SPOTIFY_FMT_MP3;
    }
    if (strcasecmp(dot, ".wav") == 0)
    {
        return COS_SPOTIFY_FMT_WAV;
    }
    return COS_SPOTIFY_FMT_UNKNOWN;
}

bool cos_spotify_audio_open(const char *path, cos_spotify_ade_err_t *out_err)
{
    if (out_err)
    {
        *out_err = COS_SPOTIFY_ADE_OK;
    }
    if (!path)
    {
        if (out_err)
            *out_err = COS_SPOTIFY_ADE_ERR_OPEN;
        return false;
    }

    cos_spotify_audio_close();

    /* 懒惰分配内部缓冲(首次打开时) */
    if (s_ring == NULL)
    {
        s_ring = (uint8_t *)heap_caps_malloc(COS_SPOTIFY_PCM_RING_BYTES,
                                             MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_ring == NULL)
        {
            COS_LOG_E("audio: ring alloc failed (%u B)", COS_SPOTIFY_PCM_RING_BYTES);
            if (out_err)
                *out_err = COS_SPOTIFY_ADE_ERR_MEM;
            return false;
        }
    }
    if (s_read_buf == NULL)
    {
        s_read_buf = (uint8_t *)heap_caps_malloc(AUDIO_READ_CHUNK,
                                                 MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_read_buf == NULL)
        {
            COS_LOG_E("audio: read buf alloc failed");
            if (out_err)
                *out_err = COS_SPOTIFY_ADE_ERR_MEM;
            return false;
        }
    }
    if (s_pcm_buf == NULL)
    {
        /* PSRAM:解码输出非 DMA,且体积较大(1152 帧 × 2ch × 2B = 4.6KB) */
        s_pcm_buf = (int16_t *)heap_caps_malloc(AUDIO_PCM_FRAMES * 2 * sizeof(int16_t),
                                                MALLOC_CAP_SPIRAM);
        if (s_pcm_buf == NULL)
        {
            s_pcm_buf = (int16_t *)cos_malloc(AUDIO_PCM_FRAMES * 2 * sizeof(int16_t));
        }
        if (s_pcm_buf == NULL)
        {
            COS_LOG_E("audio: pcm buf alloc failed");
            if (out_err)
                *out_err = COS_SPOTIFY_ADE_ERR_MEM;
            return false;
        }
    }

    _ring_reset();
    s_position_ms = 0;
    s_duration_ms = 0;
    s_wav_data_off = 0;
    s_wav_data_len = 0;
    s_wav_read_off = 0;
    s_speed_acc = 0;

    cos_file_t fp = cos_storage_file_open_read(path);
    if (fp == COS_FILE_INVALID)
    {
        COS_LOG_W("audio: open fail %s", path);
        if (out_err)
            *out_err = COS_SPOTIFY_ADE_ERR_OPEN;
        return false;
    }

    uint32_t size = 0;
    if (cos_storage_file_size(fp, &size) != COS_OK || size == 0)
    {
        cos_storage_file_close(fp);
        if (out_err)
            *out_err = COS_SPOTIFY_ADE_ERR_OPEN;
        return false;
    }
    if (size > COS_SPOTIFY_MAX_FILE_BYTES)
    {
        COS_LOG_W("audio: too big %lu B", (unsigned long)size);
        cos_storage_file_close(fp);
        if (out_err)
            *out_err = COS_SPOTIFY_ADE_ERR_FORMAT;
        return false;
    }

    cos_spotify_fmt_t fmt = cos_spotify_audio_probe(path);
    if (fmt == COS_SPOTIFY_FMT_UNKNOWN)
    {
        cos_storage_file_close(fp);
        if (out_err)
            *out_err = COS_SPOTIFY_ADE_ERR_FORMAT;
        return false;
    }

    if (fmt == COS_SPOTIFY_FMT_WAV)
    {
        if (!_wav_parse(fp, size))
        {
            cos_storage_file_close(fp);
            if (out_err)
                *out_err = COS_SPOTIFY_ADE_ERR_FORMAT;
            return false;
        }
        s_fmt = COS_SPOTIFY_FMT_WAV;
    }
    else   /* MP3 */
    {
        s_mp3 = cos_spotify_mp3_create();
        if (s_mp3 == NULL)
        {
            cos_storage_file_close(fp);
            if (out_err)
                *out_err = COS_SPOTIFY_ADE_ERR_MEM;
            return false;
        }
        if (!cos_spotify_mp3_open(s_mp3, fp, size))
        {
            COS_LOG_W("audio: mp3 open/parse failed %s", path);
            s_mp3 = NULL;
            cos_storage_file_close(fp);
            if (out_err)
                *out_err = COS_SPOTIFY_ADE_ERR_DECODE;
            return false;
        }
        s_sample_rate = cos_spotify_mp3_sample_rate(s_mp3);
        s_channels = cos_spotify_mp3_channels(s_mp3);
        s_bits = 16;
        s_duration_ms = cos_spotify_mp3_duration_ms(s_mp3);
        s_fmt = COS_SPOTIFY_FMT_MP3;
    }

    s_fp_handle = fp;   /* 保存句柄,供 pump/seek 使用 */
    s_open = true;
    COS_LOG_I("audio: opened %s (%s) %lu Hz %u ch %lu ms",
              path,
              s_fmt == COS_SPOTIFY_FMT_MP3 ? "MP3" : "WAV",
              (unsigned long)s_sample_rate, s_channels,
              (unsigned long)s_duration_ms);
    return true;
}

void cos_spotify_audio_close(void)
{
    if (s_fp_handle != COS_FILE_INVALID)
    {
        cos_storage_file_close(s_fp_handle);
        s_fp_handle = COS_FILE_INVALID;
    }
    if (s_mp3)
    {
        cos_spotify_mp3_destroy(s_mp3);
        s_mp3 = NULL;
    }
    _ring_reset();
    s_open = false;
    s_fmt = COS_SPOTIFY_FMT_UNKNOWN;
    s_position_ms = 0;
    s_duration_ms = 0;
    s_wav_read_off = 0;
}

/**
 * @brief 应用播放速度:从 PCM 帧流中按比例重复/丢弃帧。
 * @param src 交错 PCM
 * @param frames 输入帧数
 * @param out 输出缓冲
 * @param out_cap_frames 输出缓冲容量(帧)
 * @return 输出帧数
 *
 * 实现:每输出一帧,累加 speed(以 1/16 为步进)。
 *   speed > 1 → 跳过部分输入帧(加快)
 *   speed < 1 → 重复当前帧(放慢)
 * 这是"解码侧变速",UAC 侧始终固定采样率输出。
 */
static uint32_t _apply_speed(const int16_t *src, uint32_t frames,
                             int16_t *out, uint32_t out_cap_frames,
                             uint8_t ch)
{
    uint32_t out_n = 0;
    uint32_t step = (uint32_t)(s_speed * 16.0f + 0.5f);   /* 1/16 单位 */
    if (step < 8)   step = 8;      /* 0.5x 下限 */
    if (step > 64)  step = 64;     /* 4.0x 上限 */

    uint32_t in_i = 0;
    while (out_n < out_cap_frames && in_i < frames)
    {
        memcpy(out + (size_t)out_n * ch, src + (size_t)in_i * ch,
               (size_t)ch * sizeof(int16_t));
        out_n++;

        s_speed_acc += step;
        uint32_t advance = s_speed_acc / 16u;
        s_speed_acc %= 16u;
        if (advance == 0)
        {
            advance = 1;   /* 至少前进 1 帧,避免死循环 */
        }
        in_i += advance;
    }
    return out_n;
}

int32_t cos_spotify_audio_pump(void)
{
    if (!s_open || s_fp_handle == COS_FILE_INVALID)
    {
        return -1;
    }
    if (_ring_free() < AUDIO_READ_CHUNK / 2)
    {
        return 0;   /* 缓冲接近满,本片不产出 */
    }

    if (s_fmt == COS_SPOTIFY_FMT_WAV)
    {
        /* WAV 若速度不是 1.0x,也需变速处理 */
        if (s_speed >= 0.999f && s_speed <= 1.001f)
        {
            return _pump_wav(s_fp_handle);
        }
        /* 变速路径:读一块再变速写入 */
        uint32_t frame = s_channels * 2u;
        uint32_t free_bytes = _ring_free();
        uint32_t want = AUDIO_READ_CHUNK;
        if (want > s_wav_data_len - s_wav_read_off)
        {
            want = s_wav_data_len - s_wav_read_off;
        }
        want = (want / frame) * frame;
        if (want == 0)
        {
            return -1;
        }
        if (cos_storage_file_seek(s_fp_handle, s_wav_data_off + s_wav_read_off) != COS_OK)
        {
            return -2;
        }
        ssize_t rd = cos_storage_file_read(s_fp_handle, s_read_buf, want);
        if (rd <= 0)
        {
            return -2;
        }
        uint32_t in_frames = ((uint32_t)rd / frame);
        uint32_t out_cap = free_bytes / frame;
        uint32_t out_frames = _apply_speed((const int16_t *)s_read_buf, in_frames,
                                           s_pcm_buf, out_cap, s_channels);
        uint32_t out_bytes = out_frames * frame;
        if (out_bytes)
        {
            _apply_volume(s_pcm_buf, out_bytes);
            _ring_write((const uint8_t *)s_pcm_buf, out_bytes);
        }
        s_wav_read_off += (uint32_t)rd;
        uint32_t bytes_per_ms = (s_sample_rate * frame) / 1000u;
        if (bytes_per_ms)
        {
            s_position_ms = s_wav_read_off / bytes_per_ms;
        }
        return (int32_t)out_bytes;
    }

    /* ── MP3 ── */
    int frames = 0;
    uint32_t sr = 0;
    uint8_t ch = 0;
    int r = cos_spotify_mp3_read_frame(s_mp3, s_pcm_buf, AUDIO_PCM_FRAMES,
                                      &frames, &sr, &ch);
    if (r < 0)
    {
        return -1;   /* 文件尾或解码错误 */
    }
    if (frames <= 0)
    {
        return 0;
    }
    if (sr)
    {
        s_sample_rate = sr;
    }
    if (ch)
    {
        s_channels = ch;
    }

    uint32_t frame_bytes = (uint32_t)ch * 2u;
    uint32_t free_bytes = _ring_free();

    if (s_speed >= 0.999f && s_speed <= 1.001f)
    {
        uint32_t bytes = (uint32_t)frames * frame_bytes;
        if (bytes > free_bytes)
        {
            bytes = (free_bytes / frame_bytes) * frame_bytes;
        }
        if (bytes)
        {
            _apply_volume(s_pcm_buf, bytes);
            _ring_write((const uint8_t *)s_pcm_buf, bytes);
        }
        /* 位置:按已解码帧推进(未考虑被截断部分,足够近似) */
        uint32_t s = s_sample_rate ? s_sample_rate : 44100;
        s_position_ms += (uint32_t)(((uint64_t)frames * 1000u) / s);
        return (int32_t)bytes;
    }

    /* 变速路径 */
    uint32_t out_cap = free_bytes / frame_bytes;
    uint32_t out_frames = _apply_speed(s_pcm_buf, (uint32_t)frames,
                                       s_pcm_buf, out_cap, ch);
    uint32_t out_bytes = out_frames * frame_bytes;
    if (out_bytes)
    {
        _apply_volume(s_pcm_buf, out_bytes);
        _ring_write((const uint8_t *)s_pcm_buf, out_bytes);
    }
    uint32_t s = s_sample_rate ? s_sample_rate : 44100;
    s_position_ms += (uint32_t)(((uint64_t)frames * 1000u) / s);
    return (int32_t)out_bytes;
}

int32_t cos_spotify_audio_flush_to_uac(void)
{
    if (!s_ring)
    {
        return 0;
    }
    uint32_t used = _ring_used();
    if (used == 0)
    {
        return 0;
    }

    /* 等时端点要求每次发送连续内存;环形缓冲跨界时分两段发送 */
    uint32_t first = COS_SPOTIFY_PCM_RING_BYTES - s_ring_tail;
    if (first > used)
    {
        first = used;
    }

    int32_t sent = cos_spotify_uac_write(s_ring + s_ring_tail, first);
    if (sent > 0)
    {
        _ring_consume((uint32_t)sent);
    }
    return sent > 0 ? sent : 0;
}

void cos_spotify_audio_seek_ms(uint32_t ms)
{
    if (!s_open || s_fp_handle == COS_FILE_INVALID)
    {
        return;
    }
    _ring_reset();

    if (s_fmt == COS_SPOTIFY_FMT_WAV)
    {
        uint32_t frame = s_channels * 2u;
        uint32_t bytes_per_ms = (s_sample_rate * frame) / 1000u;
        uint32_t off = ms * bytes_per_ms;
        off = (off / frame) * frame;
        if (off > s_wav_data_len)
        {
            off = s_wav_data_len;
        }
        s_wav_read_off = off;
        s_position_ms = ms;
        COS_LOG_I("audio: seek WAV -> %lu ms (off=%lu)", (unsigned long)ms, (unsigned long)off);
        return;
    }

    /* MP3:重建解码器并跳到目标帧(按位率估算)。 */
    if (s_mp3 && s_sample_rate)
    {
        /* MP3 seek 依赖帧索引;这里采用"重建 + 线性跳过"的近似实现,
         * 由 mp3 层负责内部帧跳过。 */
        cos_spotify_mp3_seek_ms(s_mp3, ms);
        s_position_ms = ms;
        COS_LOG_I("audio: seek MP3 -> %lu ms", (unsigned long)ms);
    }
}

void cos_spotify_audio_seek_relative_ms(int32_t delta_ms)
{
    int64_t target = (int64_t)s_position_ms + delta_ms;
    if (target < 0)
    {
        target = 0;
    }
    if (s_duration_ms && target > (int64_t)s_duration_ms)
    {
        target = (int64_t)s_duration_ms;
    }
    cos_spotify_audio_seek_ms((uint32_t)target);
}

void cos_spotify_audio_set_speed(float speed)
{
    if (speed < 0.5f)
    {
        speed = 0.5f;
    }
    if (speed > 4.0f)
    {
        speed = 4.0f;
    }
    s_speed = speed;
    s_speed_acc = 0;
}

float cos_spotify_audio_get_speed(void)
{
    return s_speed;
}

uint32_t cos_spotify_audio_position_ms(void)
{
    return s_position_ms;
}

uint32_t cos_spotify_audio_duration_ms(void)
{
    return s_duration_ms;
}

bool cos_spotify_audio_cos(void)
{
    if (!s_open)
    {
        return true;
    }
    bool file_end = (s_fmt == COS_SPOTIFY_FMT_WAV)
                        ? (s_wav_read_off >= s_wav_data_len)
                        : cos_spotify_mp3_cos(s_mp3);
    return file_end && (_ring_used() == 0);
}

uint8_t cos_spotify_audio_channels(void)
{
    return s_channels;
}

uint32_t cos_spotify_audio_sample_rate(void)
{
    return s_sample_rate;
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
