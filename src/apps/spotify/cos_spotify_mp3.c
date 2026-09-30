/**
 * @file eos_spotify_mp3.c
 * @brief minimp3 封装:把 SD 上的 MP3 流喂给 minimp3 得到交错 16-bit PCM。
 *
 * ── 为什么用 minimp3 ─────────────────────────────────────────
 *   仓库内无任何 MP3 解码器。minimp3 是单头文件、CC0(公共领域)、
 *   被广泛验证的 MPEG-1/2/2.5 Layer III 解码器,适合嵌入。
 *   源码位于本 App 自己的 third_party/(不污染全局 third_party/)。
 *
 * ── 集成方式 ────────────────────────────────────────────────
 *   minimp3 是"推流式"解码:调用方把一段 MP3 字节交给
 *   mp3dec_decode_frame(),它返回可解出的样点数。
 *   本封装负责:
 *     · 维护一个 4KB 的读缓冲,从 eos_storage_file_* 拉数据;
 *     · 处理"一帧跨缓冲区"的情况(保留未消费尾部);
 *     · 输出交错 int16 PCM。
 *
 * ── 内存 ────────────────────────────────────────────────────
 *   mp3dec_t 约 5.5KB(PSRAM 优先);读缓冲 4KB(DMA 内部 RAM)。
 */
#include "eos_spotify_mp3.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#define EOS_LOG_TAG "SpotifyMP3"
#include "eos_log.h"

#include "eos_mem.h"

/* minimp3 实现编译在本文件内(单头文件 + 实现宏) */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3          /* 只保留 MP3,省 Flash(~30KB) */
#define MINIMP3_NO_STDIO          /* 不用 stdio,避免与 VFS 交互    */
#include "minimp3.h"

/* ── 读缓冲 ───────────────────────────────────────────────── */

#define MP3_READ_CHUNK   (4 * 1024)

struct eos_spotify_mp3_s
{
    mp3dec_t        dec;
    eos_file_t      fp;
    uint32_t        file_size;

    uint8_t        *buf;         /* 读缓冲 */
    uint32_t        buf_size;    /* 容量 */
    uint32_t        buf_len;     /* 有效字节数 */
    uint32_t        file_off;    /* 缓冲在文件中的起始偏移 */
    bool            eof;         /* 文件已读完 */

    uint32_t        sample_rate;
    uint8_t         channels;
    uint32_t        duration_ms;
    bool            eos;

    uint32_t        frames_decoded;
};

/* 从 SD 把缓冲填满(尽量)。返回新增字节数。 */
static uint32_t _fill(eos_spotify_mp3_t *m)
{
    if (m->eof || m->buf_len >= m->buf_size)
    {
        return 0;
    }
    uint32_t space = m->buf_size - m->buf_len;
    ssize_t rd = eos_storage_file_read(m->fp, m->buf + m->buf_len, space);
    if (rd <= 0)
    {
        m->eof = true;
        return 0;
    }
    m->buf_len += (uint32_t)rd;
    return (uint32_t)rd;
}

/* 丢弃前 n 字节,把剩余数据搬到缓冲开头并尽量补满。 */
static void _consume(eos_spotify_mp3_t *m, uint32_t n)
{
    if (n > m->buf_len)
    {
        n = m->buf_len;
    }
    if (n > 0 && n < m->buf_len)
    {
        memmove(m->buf, m->buf + n, m->buf_len - n);
    }
    m->buf_len -= n;
    m->file_off += n;
    _fill(m);
}

/* ── 对外 API ─────────────────────────────────────────────── */

eos_spotify_mp3_t *eos_spotify_mp3_create(void)
{
    eos_spotify_mp3_t *m = (eos_spotify_mp3_t *)heap_caps_malloc(
        sizeof(eos_spotify_mp3_t), MALLOC_CAP_SPIRAM);
    if (m == NULL)
    {
        m = (eos_spotify_mp3_t *)eos_malloc(sizeof(eos_spotify_mp3_t));
    }
    if (m == NULL)
    {
        EOS_LOG_E("mp3: decoder alloc failed (%u B)", (unsigned)sizeof(eos_spotify_mp3_t));
        return NULL;
    }
    memset(m, 0, sizeof(*m));

    m->buf_size = MP3_READ_CHUNK;
    m->buf = (uint8_t *)heap_caps_malloc(m->buf_size,
                                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (m->buf == NULL)
    {
        m->buf = (uint8_t *)eos_malloc(m->buf_size);
    }
    if (m->buf == NULL)
    {
        EOS_LOG_E("mp3: read buf alloc failed");
        eos_free(m);
        return NULL;
    }

    m->fp = EOS_FILE_INVALID;
    mp3dec_init(&m->dec);
    return m;
}

void eos_spotify_mp3_destroy(eos_spotify_mp3_t *m)
{
    if (m == NULL)
    {
        return;
    }
    if (m->buf)
    {
        eos_free(m->buf);
    }
    eos_free(m);
}

bool eos_spotify_mp3_open(eos_spotify_mp3_t *m, eos_file_t fp, uint32_t file_size)
{
    if (m == NULL || fp == EOS_FILE_INVALID)
    {
        return false;
    }
    m->fp = fp;
    m->file_size = file_size;
    m->buf_len = 0;
    m->file_off = 0;
    m->eof = false;
    m->eos = false;
    m->frames_decoded = 0;
    m->sample_rate = 0;
    m->channels = 0;
    m->duration_ms = 0;

    mp3dec_init(&m->dec);

    if (eos_storage_file_seek(fp, 0) != EOS_OK)
    {
        return false;
    }
    _fill(m);
    if (m->buf_len < 4)
    {
        EOS_LOG_W("mp3: file too small");
        return false;
    }

    /* 先解一帧以取得采样率/声道,再把缓冲重置到文件头重新开始。
     * minimp3 的 decode_frame 自带同步字扫描,能跳过 ID3v2 标签。 */
    {
        static int16_t probe[MINIMP3_MAX_SAMPLES_PER_FRAME];
        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        int samples = mp3dec_decode_frame(&m->dec, m->buf, (int)m->buf_len,
                                          probe, &info);
        if (info.hz == 0 || info.channels == 0)
        {
            EOS_LOG_W("mp3: not a valid MP3 stream");
            return false;
        }
        m->sample_rate = (uint32_t)info.hz;
        m->channels = (uint8_t)info.channels;

        /* 时长估算:基于首帧位率(CBR 准确,VBR 为近似) */
        if (info.bitrate_kbps > 0)
        {
            uint64_t br = (uint64_t)info.bitrate_kbps * 1000u;
            m->duration_ms = (uint32_t)(((uint64_t)file_size * 8u * 1000u) / br);
        }
        EOS_LOG_I("mp3: %d Hz %d ch %d kbps layer=%d, ~%lu ms%s",
                  info.hz, info.channels, info.bitrate_kbps, info.layer,
                  (unsigned long)m->duration_ms,
                  samples > 0 ? "" : " (resync needed)");
    }

    /* 重置到文件开头,正式解码从第 0 字节开始 */
    if (eos_storage_file_seek(fp, 0) != EOS_OK)
    {
        return false;
    }
    m->buf_len = 0;
    m->file_off = 0;
    m->eof = false;
    mp3dec_init(&m->dec);
    _fill(m);
    return true;
}

int eos_spotify_mp3_read_frame(eos_spotify_mp3_t *m, int16_t *pcm, int max_frames,
                               int *out_frames, uint32_t *out_rate, uint8_t *out_ch)
{
    if (m == NULL || pcm == NULL || m->eos)
    {
        return -1;
    }

    /* 保证缓冲里有足够数据(minimp3 一帧最大 ~1441 字节) */
    uint32_t guard = 0;
    while (m->buf_len < 1600 && !m->eof && guard++ < 8)
    {
        if (_fill(m) == 0)
        {
            break;
        }
    }
    if (m->buf_len < 4)
    {
        m->eos = true;
        return -1;
    }

    mp3dec_frame_info_t info;
    memset(&info, 0, sizeof(info));

    int samples = mp3dec_decode_frame(&m->dec, m->buf, (int)m->buf_len,
                                      (mp3d_sample_t *)pcm, &info);
    if (info.frame_bytes <= 0)
    {
        /* 无法同步:丢弃 1 字节重试,否则判定文件尾 */
        if (m->eof && m->buf_len < 1600)
        {
            m->eos = true;
            return -1;
        }
        _consume(m, 1);
        if (out_frames)
        {
            *out_frames = 0;
        }
        return 0;
    }

    /* 消费本帧 */
    _consume(m, (uint32_t)info.frame_bytes);

    if (info.hz > 0)
    {
        m->sample_rate = (uint32_t)info.hz;
    }
    if (info.channels > 0)
    {
        m->channels = (uint8_t)info.channels;
    }

    /* 输出交错立体声(mono 复制成双声道,便于 UAC 端点固定 2ch 发送) */
    if (samples > 0)
    {
        int nch = info.channels ? info.channels : 1;
        if (nch == 1)
        {
            /* 原地把 mono 展开为 stereo(从后往前,避免覆盖) */
            for (int i = samples - 1; i >= 0; i--)
            {
                int16_t s = pcm[i];
                pcm[(size_t)i * 2] = s;
                pcm[(size_t)i * 2 + 1] = s;
            }
            if (out_ch)
            {
                *out_ch = 2;
            }
        }
        else if (out_ch)
        {
            *out_ch = (uint8_t)nch;
        }
        m->channels = 2;
    }

    if (out_frames)
    {
        *out_frames = samples;
    }
    if (out_rate)
    {
        *out_rate = m->sample_rate;
    }
    m->frames_decoded++;

    if (m->eof && m->buf_len == 0)
    {
        m->eos = true;
    }
    return samples > 0 ? 0 : 0;   /* 0 样本不算错误,继续下一帧 */
}

uint32_t eos_spotify_mp3_sample_rate(const eos_spotify_mp3_t *m)
{
    return m ? m->sample_rate : 0;
}

uint8_t eos_spotify_mp3_channels(const eos_spotify_mp3_t *m)
{
    return m ? m->channels : 0;
}

uint32_t eos_spotify_mp3_duration_ms(const eos_spotify_mp3_t *m)
{
    return m ? m->duration_ms : 0;
}

bool eos_spotify_mp3_eos(const eos_spotify_mp3_t *m)
{
    return m ? m->eos : true;
}

void eos_spotify_mp3_seek_ms(eos_spotify_mp3_t *m, uint32_t ms)
{
    if (m == NULL || m->fp == EOS_FILE_INVALID || m->sample_rate == 0)
    {
        return;
    }
    /* 用平均位率估算目标字节偏移。minimp3 的 decode_frame 会自动扫描
     * 同步字,因此从估算位置附近开始即可重新对齐。 */
    uint32_t target = ms;
    if (m->duration_ms > 0)
    {
        target = (uint32_t)(((uint64_t)ms * (uint64_t)m->file_size) / m->duration_ms);
    }
    if (target >= m->file_size)
    {
        target = m->file_size ? m->file_size - 1 : 0;
    }

    if (eos_storage_file_seek(m->fp, target) != EOS_OK)
    {
        return;
    }
    m->buf_len = 0;
    m->file_off = target;
    m->eof = false;
    m->eos = false;
    mp3dec_init(&m->dec);
    _fill(m);
    EOS_LOG_I("mp3: seek -> %lu ms (off=%lu)", (unsigned long)ms, (unsigned long)target);
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
