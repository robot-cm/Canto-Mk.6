/**
 * @file eos_spotify_lrc.c
 * @brief LRC 歌词解析与检索实现。
 *
 * 解析流程:
 *   1. 一次性读入整个 .lrc(歌词文件通常 < 20KB,放 PSRAM);
 *   2. 逐行扫描:
 *      · 以 '[' 开头且内容是 "数字:数字" 的 → 时间标签,可一行多个;
 *      · 其余 [...] 元数据标签([ti:]/[ar:]/...) → 记录 offset 后丢弃;
 *      · 标签之后的剩余文本即歌词内容;
 *   3. 展开为 (时间, 文本) 数组,按时间升序排序(多标签行会展开成多条);
 *   4. 释放原始文件缓冲。
 *
 * 时间复杂度:解析 O(n·k);检索用二分(O(log n))。
 */
#include "eos_spotify_lrc.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#define EOS_LOG_TAG "SpotifyLrc"
#include "eos_log.h"

#include "eos_mem.h"
#include "eos_service_storage.h"

typedef struct
{
    uint32_t time_ms;
    char    *text;      /* eos_malloc 的 UTF-8 文本           */
} lrc_line_t;

struct eos_spotify_lrc_s
{
    lrc_line_t *lines;
    int         count;
    int         cap;
    int32_t     offset_ms;   /* [offset:±ms] 修正值 */
};

/* ── 动态数组 ─────────────────────────────────────────────── */

static bool _push(eos_spotify_lrc_t *lrc, uint32_t t, const char *text, size_t len)
{
    if (lrc->count >= lrc->cap)
    {
        int ncap = lrc->cap ? lrc->cap * 2 : 64;
        lrc_line_t *nl = (lrc_line_t *)heap_caps_realloc(lrc->lines,
                                                         (size_t)ncap * sizeof(lrc_line_t),
                                                         MALLOC_CAP_SPIRAM);
        if (nl == NULL)
        {
            nl = (lrc_line_t *)eos_realloc(lrc->lines, (size_t)ncap * sizeof(lrc_line_t));
        }
        if (nl == NULL)
        {
            return false;
        }
        lrc->lines = nl;
        lrc->cap = ncap;
    }

    /* 去掉首尾空白 */
    while (len > 0 && (text[0] == ' ' || text[0] == '\t'))
    {
        text++;
        len--;
    }
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' ||
                       text[len - 1] == '\r' || text[len - 1] == '\n'))
    {
        len--;
    }
    if (len >= EOS_SPOTIFY_LRC_LINE_MAX)
    {
        len = EOS_SPOTIFY_LRC_LINE_MAX - 1;
    }

    char *copy = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (copy == NULL)
    {
        copy = (char *)eos_malloc(len + 1);
    }
    if (copy == NULL)
    {
        return false;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';

    lrc->lines[lrc->count].time_ms = t;
    lrc->lines[lrc->count].text = copy;
    lrc->count++;
    return true;
}

static int _cmp_line(const void *a, const void *b)
{
    const lrc_line_t *la = (const lrc_line_t *)a;
    const lrc_line_t *lb = (const lrc_line_t *)b;
    if (la->time_ms < lb->time_ms)
        return -1;
    if (la->time_ms > lb->time_ms)
        return 1;
    return 0;
}

/* ── 时间标签解析 ─────────────────────────────────────────── */

/**
 * @brief 尝试解析 "[mm:ss.xx]" / "[mm:ss]" 形式的时间标签。
 * @param p     指向 '['
 * @param out_t 输出毫秒
 * @param out_next 输出标签结束后的位置(即 ']' 之后)
 * @return true 是合法时间标签。
 */
static bool _parse_time_tag(const char *p, uint32_t *out_t, const char **out_next)
{
    if (p[0] != '[')
    {
        return false;
    }
    const char *q = p + 1;

    /* 分钟:1-3 位数字 */
    int mm = 0, digits = 0;
    while (*q >= '0' && *q <= '9' && digits < 3)
    {
        mm = mm * 10 + (*q - '0');
        q++;
        digits++;
    }
    if (digits == 0 || *q != ':')
    {
        return false;
    }
    q++;

    /* 秒:1-2 位 */
    int ss = 0;
    digits = 0;
    while (*q >= '0' && *q <= '9' && digits < 2)
    {
        ss = ss * 10 + (*q - '0');
        q++;
        digits++;
    }
    if (digits == 0)
    {
        return false;
    }

    /* 小数部分:.xx 或 .xxx */
    int frac = 0, fdigits = 0;
    if (*q == '.' || *q == ':')
    {
        q++;
        while (*q >= '0' && *q <= '9' && fdigits < 3)
        {
            frac = frac * 10 + (*q - '0');
            q++;
            fdigits++;
        }
    }
    if (*q != ']')
    {
        return false;
    }
    q++;

    /* 补齐到毫秒 */
    while (fdigits < 3)
    {
        frac *= 10;
        fdigits++;
    }

    *out_t = (uint32_t)(mm * 60000 + ss * 1000 + frac);
    *out_next = q;
    return true;
}

/* ── 加载 ─────────────────────────────────────────────────── */

eos_spotify_lrc_t *eos_spotify_lrc_load(const char *path)
{
    if (path == NULL)
    {
        return NULL;
    }
    if (!eos_storage_is_file(path))
    {
        return NULL;
    }

    char *data = eos_storage_read_file(path);
    if (data == NULL)
    {
        EOS_LOG_W("lrc: read fail %s", path);
        return NULL;
    }

    eos_spotify_lrc_t *lrc = (eos_spotify_lrc_t *)heap_caps_malloc(
        sizeof(eos_spotify_lrc_t), MALLOC_CAP_SPIRAM);
    if (lrc == NULL)
    {
        lrc = (eos_spotify_lrc_t *)eos_malloc(sizeof(eos_spotify_lrc_t));
    }
    if (lrc == NULL)
    {
        eos_free(data);
        return NULL;
    }
    memset(lrc, 0, sizeof(*lrc));

    /* 逐行处理 */
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save))
    {
        /*
         * 一行可能形如:
         *   [00:12.00]歌词
         *   [00:12.00][00:45.30]副歌
         *   [ti:歌名]                ← 元数据
         *   [offset:-500]
         *   [00:12.00]               ← 空歌词(过场)
         */
        const char *p = line;
        uint32_t times[8];
        int ntimes = 0;

        /* 先收集该行所有前置时间标签 */
        while (ntimes < 8)
        {
            /* 跳过标签之间的空白 */
            while (*p == ' ' || *p == '\t')
            {
                p++;
            }
            if (*p != '[')
            {
                break;
            }
            uint32_t t = 0;
            const char *next = NULL;
            if (_parse_time_tag(p, &t, &next))
            {
                times[ntimes++] = t;
                p = next;
                continue;
            }
            /* 不是时间标签 → 可能是元数据 [xx:...] 或 ID tag */
            const char *close = strchr(p, ']');
            if (close == NULL)
            {
                break;
            }
            /* 识别 [offset:±ms] */
            if (strncmp(p, "[offset:", 8) == 0)
            {
                lrc->offset_ms = (int32_t)strtol(p + 8, NULL, 10);
            }
            p = close + 1;
        }

        if (ntimes == 0)
        {
            continue;   /* 纯元数据行或无标签行,跳过 */
        }

        /* 剩余部分即歌词文本(可能为空,表示间奏) */
        for (int i = 0; i < ntimes; i++)
        {
            _push(lrc, times[i], p, strlen(p));
        }
    }

    eos_free(data);

    if (lrc->count == 0)
    {
        EOS_LOG_I("lrc: no timed lines in %s", path);
        eos_free(lrc);
        return NULL;
    }

    qsort(lrc->lines, (size_t)lrc->count, sizeof(lrc_line_t), _cmp_line);

    /* 应用 offset 修正 */
    if (lrc->offset_ms != 0)
    {
        for (int i = 0; i < lrc->count; i++)
        {
            int64_t t = (int64_t)lrc->lines[i].time_ms + lrc->offset_ms;
            lrc->lines[i].time_ms = (t < 0) ? 0u : (uint32_t)t;
        }
    }

    EOS_LOG_I("lrc: loaded %d lines from %s (offset=%ldms)",
              lrc->count, path, (long)lrc->offset_ms);
    return lrc;
}

void eos_spotify_lrc_free(eos_spotify_lrc_t *lrc)
{
    if (lrc == NULL)
    {
        return;
    }
    for (int i = 0; i < lrc->count; i++)
    {
        if (lrc->lines[i].text)
        {
            eos_free(lrc->lines[i].text);
        }
    }
    if (lrc->lines)
    {
        eos_free(lrc->lines);
    }
    eos_free(lrc);
}

/* ── 检索 ─────────────────────────────────────────────────── */

int eos_spotify_lrc_count(const eos_spotify_lrc_t *lrc)
{
    return lrc ? lrc->count : 0;
}

int eos_spotify_lrc_index_at(const eos_spotify_lrc_t *lrc, uint32_t ms)
{
    if (lrc == NULL || lrc->count == 0 || ms < lrc->lines[0].time_ms)
    {
        return -1;
    }

    /* 二分找最后一个 time <= ms 的行 */
    int lo = 0, hi = lrc->count - 1, best = 0;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (lrc->lines[mid].time_ms <= ms)
        {
            best = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return best;
}

const char *eos_spotify_lrc_line(const eos_spotify_lrc_t *lrc, int idx)
{
    if (lrc == NULL || idx < 0 || idx >= lrc->count)
    {
        return NULL;
    }
    return lrc->lines[idx].text;
}

uint32_t eos_spotify_lrc_time(const eos_spotify_lrc_t *lrc, int idx)
{
    if (lrc == NULL || idx < 0 || idx >= lrc->count)
    {
        return 0;
    }
    return lrc->lines[idx].time_ms;
}

uint32_t eos_spotify_lrc_duration(const eos_spotify_lrc_t *lrc, int idx)
{
    if (lrc == NULL || idx < 0 || idx >= lrc->count)
    {
        return EOS_SPOTIFY_LRC_TAIL_MS;
    }
    uint32_t dur;
    if (idx + 1 < lrc->count)
    {
        uint32_t next = lrc->lines[idx + 1].time_ms;
        uint32_t cur = lrc->lines[idx].time_ms;
        dur = (next > cur) ? (next - cur) : 300u;
    }
    else
    {
        dur = EOS_SPOTIFY_LRC_TAIL_MS;
    }
    if (dur < 300u)
    {
        dur = 300u;   /* 下限:避免滚动速度过快 */
    }
    return dur;
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
