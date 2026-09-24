/**
 * @file cos_dictionary.c
 * @brief Dictionary app — ECDICT .dat random-access reader on 240x240 round UI.
 *
 * Reads /sdcard/ecdict/ecdict.dat on demand (never loads the whole file,
 * it is ~200 MB). Binary layout follows scripts/ecdict/step4_1_export.py
 * (little-endian):
 *
 *   Global header (76 B): magic "ECDICT01", version=1, entry_count,
 *       bucket_count=1670, checkpoint_every=64, entry_header_size=12, ...
 *   Bucket index (40 B/bucket): first_checkpoint_offset, first_entry_index,
 *       data_size, entry_count, checkpoint_count, data_offset.
 *   Bucket: 28 B header ("BKT1") + checkpoints + entries.
 *   Checkpoint (12 B + utf8): entry_index, entry_offset, normalized_len.
 *   Entry (12 B + utf8): word_len, phonetic_len, translation_len,
 *       definition_len, pos_len, exchange_len + payloads.
 *
 * Lookup: normalize(query) -> binary search bucket (via first checkpoint) ->
 *         load whole bucket (~128 KB into PSRAM) -> binary search checkpoint ->
 *         scan <= checkpoint_every entries.
 *
 * UI (album-style): empty page -> floating search pill at the bottom; tap it
 * and the input box stretches out from a circle into a rounded capsule while
 * the round keyboard pops up; on OK the box collapses back, the result is
 * shown in a scrollable list (word title + phonetic + translation +
 * definition + pos + exchange), and the pill reappears for another search.
 * The last searched word is persisted to /sdcard/history/ecdict/history.txt
 * (line 1) and restored on app enter.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "cos_config.h"
#include "cos_dictionary.h"
#include "cos_activity.h"
#include "cos_theme.h"
#include "cos_font.h"
#include "cos_mem.h"
#include "cos_round_clip.h"
#include "cos_round_keyboard.h"
#include "cos_service_storage.h"
#include "cos_app_header.h"
#include "cos_icon.h"
#include "cos_log.h"

/* ---------------------------------------------------------------- */
/* .dat format constants (see scripts/ecdict/step4_1_export.py)       */
/* ---------------------------------------------------------------- */
#define DICT_PATH "/sdcard/ecdict/ecdict.dat"

#define DICT_MAGIC          "ECDICT01"
#define DICT_MAGIC_LEN      8
#define DICT_VERSION        1
#define DICT_HEADER_SIZE    76   /* GLOBAL_HEADER_FMT "<8sIIIQIIIIQQQQ" 实际 76 B */
#define DICT_ENTRY_HEADER_SIZE 12
#define DICT_BUCKET_INDEX_ITEM 40
#define DICT_BUCKET_HEADER_SIZE 28
#define DICT_CHECKPOINT_HEADER 12

#define DICT_MAX_BUCKETS    2048
#define DICT_BUCKET_BUF_CAP (128 * 1024 + 4096)   /* bucket_data_size <= 128 KB */

#define DICT_NORM_CAP       256
#define DICT_RESULT_CAP     2048
#define DICT_QUERY_CAP      64

#define DICT_FUZZY_PAGE     10   /* 模糊结果每批渲染条数(懒加载页大小) */
#define DICT_WORD_CAP       80   /* 模糊命中 word 缓冲 */
#define DICT_SUMMARY_CAP    64   /* 模糊命中翻译摘要缓冲 */

/* ---------------------------------------------------------------- */
/* Small endian helpers                                              */
/* ---------------------------------------------------------------- */
static uint16_t _rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t _rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t _rd_u64(const uint8_t *p)
{
    return (uint64_t)_rd_u32(p) | ((uint64_t)_rd_u32(p + 4) << 32);
}

/* ---------------------------------------------------------------- */
/* Engine state                                                      */
/* ---------------------------------------------------------------- */
static cos_file_t s_fp;
static bool s_fp_open;
static uint8_t *s_idx_buf;       /* bucket index cache (PSRAM)   */
static uint8_t *s_bucket_buf;    /* current bucket data (PSRAM)  */
static uint32_t s_bucket_count;
static uint32_t s_checkpoint_every;
static uint32_t s_cur_bucket_size;
static uint32_t s_cur_bucket_idx;   /* 当前已加载 bucket 索引(避免重复加载) */
static bool s_loaded;

static bool _read_full(uint8_t *buf, size_t size)
{
    size_t got = 0;
    while (got < size) {
        ssize_t r = cos_storage_file_read(s_fp, buf + got, size - got);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

/**
 * @brief 归一化: NFKC-lite (fullwidth ASCII -> halfwidth) + trim + lowercase.
 *        Matches the normalizer used when the .dat was built, for ASCII words.
 */
static size_t _norm(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    const char *p = in;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    while (*p && n + 1 < cap) {
        const unsigned char c = (unsigned char)*p;
        if (c == 0xEF && (unsigned char)p[1] == 0xBC &&
            (unsigned char)p[2] >= 0x81 && (unsigned char)p[2] <= 0xBE) {
            /* fullwidth !..~ (U+FF01..U+FF5E) -> halfwidth */
            out[n++] = (char)((unsigned char)p[2] - 0x81 + 0x21);
            p += 3;
        } else if (c == 0xE3 && (unsigned char)p[1] == 0x80 &&
                   (unsigned char)p[2] == 0x80) {
            /* ideographic space U+3000 -> ' ' */
            out[n++] = ' ';
            p += 3;
        } else if (c == '\t' || c == '\r' || c == '\n') {
            out[n++] = ' ';
            p++;
        } else if (c == ' ') {
            out[n++] = ' ';
            p++;
        } else {
            out[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
            p++;
        }
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return n;
}

/* ---------------------------------------------------------------- */
/* Open / close engine                                               */
/* ---------------------------------------------------------------- */
static void _dict_close(void)
{
    if (s_fp_open) {
        cos_storage_file_close(s_fp);
        s_fp = NULL;
        s_fp_open = false;
    }
    if (s_idx_buf) {
        cos_free(s_idx_buf);
        s_idx_buf = NULL;
    }
    if (s_bucket_buf) {
        cos_free(s_bucket_buf);
        s_bucket_buf = NULL;
    }
    s_loaded = false;
    s_cur_bucket_size = 0;
    s_cur_bucket_idx = UINT32_MAX;
}

static bool _dict_open(void)
{
    if (s_loaded) return true;

    if (!s_fp_open) {
        s_fp = cos_storage_file_open_read(DICT_PATH);
        if (!s_fp) {
            COS_LOG_E("dict: cannot open %s (SD mounted?)", DICT_PATH);
            return false;
        }
        s_fp_open = true;
    }

    uint8_t hdr[DICT_HEADER_SIZE];
    if (cos_storage_file_seek(s_fp, 0) != COS_OK) return false;
    if (!_read_full(hdr, DICT_HEADER_SIZE)) return false;

    if (memcmp(hdr, DICT_MAGIC, DICT_MAGIC_LEN) != 0) {
        COS_LOG_E("dict: bad magic (not a step4_1_export .dat?)");
        return false;
    }
    if (_rd_u32(hdr + 8) != DICT_VERSION) {
        COS_LOG_E("dict: unsupported version %u", (unsigned)_rd_u32(hdr + 8));
        return false;
    }
    /* 76-byte header produced by step4_1_export.py, FMT "<8sIIIQIIIIQQQQ":
     * magic(8) version@8 header_size@12 entry_count@16 bucket_count(Q)@20
     * bucket_target@28 checkpoint_every@32 entry_header_size@36 reserved@40
     * bucket_index_off(Q)@44 bucket_data_off@52 total_data_size@60 sha256@68 */
    if (_rd_u32(hdr + 36) != DICT_ENTRY_HEADER_SIZE) {
        COS_LOG_E("dict: bad entry header size %u", (unsigned)_rd_u32(hdr + 36));
        return false;
    }

    s_bucket_count = (uint32_t)_rd_u64(hdr + 20);
    s_checkpoint_every = _rd_u32(hdr + 32);
    if (s_bucket_count == 0 || s_bucket_count > DICT_MAX_BUCKETS) {
        COS_LOG_E("dict: bad bucket count %u", (unsigned)s_bucket_count);
        return false;
    }

    if (!s_idx_buf) s_idx_buf = (uint8_t *)cos_malloc((size_t)s_bucket_count * DICT_BUCKET_INDEX_ITEM);
    if (!s_idx_buf) {
        COS_LOG_E("dict: no mem for bucket index");
        return false;
    }
    if (cos_storage_file_seek(s_fp, (uint32_t)_rd_u64(hdr + 44)) != COS_OK) return false;
    if (!_read_full(s_idx_buf, (size_t)s_bucket_count * DICT_BUCKET_INDEX_ITEM)) return false;

    if (!s_bucket_buf) s_bucket_buf = (uint8_t *)cos_malloc(DICT_BUCKET_BUF_CAP);
    if (!s_bucket_buf) {
        COS_LOG_E("dict: no mem for bucket buffer");
        return false;
    }

    s_loaded = true;
    return true;
}

/* ---------------------------------------------------------------- */
/* Lookup                                                            */
/* ---------------------------------------------------------------- */
static bool _read_cp_from_file(uint64_t abs_off, char *norm, size_t cap)
{
    uint8_t hdr[DICT_CHECKPOINT_HEADER];
    if (cos_storage_file_seek(s_fp, (uint32_t)abs_off) != COS_OK) {
        COS_LOG_E("dict: cp seek %llu failed", (unsigned long long)abs_off);
        return false;
    }
    if (!_read_full(hdr, DICT_CHECKPOINT_HEADER)) return false;

    uint16_t nlen = _rd_u16(hdr + 8);
    if (nlen >= cap) nlen = (uint16_t)(cap - 1);
    if (!_read_full((uint8_t *)norm, nlen)) return false;
    norm[nlen] = '\0';
    return true;
}

/** 返回最后一个 first_checkpoint.norm <= query 的 bucket, 无则 -1 */
static int32_t _find_bucket(const char *norm)
{
    int32_t lo = 0;
    int32_t hi = (int32_t)s_bucket_count - 1;
    int32_t ans = -1;
    char cpn[DICT_NORM_CAP];

    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        uint64_t cp_off = _rd_u64(s_idx_buf + (size_t)mid * DICT_BUCKET_INDEX_ITEM);
        if (!_read_cp_from_file(cp_off, cpn, sizeof(cpn))) break;
        int cmp = strcmp(cpn, norm);
        if (cmp <= 0) {
            ans = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return ans;
}

static bool _load_bucket(uint32_t bi)
{
    const uint8_t *idx = s_idx_buf + (size_t)bi * DICT_BUCKET_INDEX_ITEM;
    uint64_t data_off = _rd_u64(idx + 32);
    uint32_t dsize = _rd_u32(idx + 16);
    COS_LOG_I("dict: load bucket=%u data_off=%llu dsize=%u",
              (unsigned)bi, (unsigned long long)data_off, (unsigned)dsize);
    if (dsize == 0 || dsize > DICT_BUCKET_BUF_CAP) {
        COS_LOG_E("dict: bad dsize=%u cap=%u", (unsigned)dsize, (unsigned)DICT_BUCKET_BUF_CAP);
        return false;
    }
    if (cos_storage_file_seek(s_fp, (uint32_t)data_off) != COS_OK) {
        COS_LOG_E("dict: seek %llu failed", (unsigned long long)data_off);
        return false;
    }
    if (!_read_full(s_bucket_buf, dsize)) {
        COS_LOG_E("dict: read bucket failed");
        return false;
    }
    s_cur_bucket_size = dsize;
    s_cur_bucket_idx = bi;
    return true;
}

static bool _sn_cat(char *buf, size_t cap, size_t *n, const char *fmt, ...)
{
    if (*n >= cap - 1) return false;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (r < 0) return false;
    size_t room = cap - 1 - *n;
    *n += (r < (int)room) ? (size_t)r : room;
    return true;
}

/** 命中 entry 后拼接展示文本: word / phonetic / translation / [pos] def / ex */
static bool _fill_result(const uint8_t *e, char *out, size_t cap)
{
    uint16_t wl = _rd_u16(e + 0);
    uint16_t pl = _rd_u16(e + 2);
    uint16_t tl = _rd_u16(e + 4);
    uint16_t dl = _rd_u16(e + 6);
    uint16_t posl = _rd_u16(e + 8);
    uint16_t el = _rd_u16(e + 10);

    const char *w = (const char *)(e + 12);
    const char *p = w + wl;
    const char *t = p + pl;
    const char *d = t + tl;
    const char *pos = d + dl;
    const char *ex = pos + posl;

    size_t n = 0;
    _sn_cat(out, cap, &n, "%.*s\n", (int)wl, w);                  /* word    */
    if (pl > 0) _sn_cat(out, cap, &n, "/%.*s/\n", (int)pl, p);    /* phonetic */
    if (tl > 0) _sn_cat(out, cap, &n, "%.*s\n", (int)tl, t);      /* translation */
    if (posl > 0) _sn_cat(out, cap, &n, "[%.*s] ", (int)posl, pos);
    if (dl > 0) _sn_cat(out, cap, &n, "%.*s\n", (int)dl, d);      /* definition */
    if (el > 0) _sn_cat(out, cap, &n, "ex: %.*s\n", (int)el, ex); /* exchange */
    out[n] = '\0';
    return n > 0;
}

static bool _lookup_in_bucket(const char *norm, char *out, size_t cap)
{
    const uint8_t *b = s_bucket_buf;

    /* bucket header */
    if (memcmp(b, "BKT1", 4) != 0) {
        COS_LOG_E("dict: bucket magic %.4s", (const char *)b);
        return false;
    }
    uint32_t b_entry_count = _rd_u32(b + 8);
    uint32_t b_cp_count = _rd_u32(b + 12);
    uint32_t cp_start = _rd_u32(b + 16);
    uint32_t entry_start = _rd_u32(b + 20);
    uint32_t dsize = s_cur_bucket_size;
    COS_LOG_I("dict: bkt entries=%u cps=%u cp_start=%u ent_start=%u dsize=%u",
              (unsigned)b_entry_count, (unsigned)b_cp_count,
              (unsigned)cp_start, (unsigned)entry_start, (unsigned)dsize);

    if (cp_start + DICT_CHECKPOINT_HEADER > entry_start || entry_start > dsize) {
        COS_LOG_E("dict: bad bucket layout");
        return false;
    }

    /* linear scan checkpoints (in RAM): last checkpoint with norm <= query */
    uint32_t off = cp_start;
    uint32_t sel_off = 0;
    bool have_sel = false;
    uint32_t scanned = 0;
    while (off + DICT_CHECKPOINT_HEADER <= entry_start && scanned < b_cp_count) {
        uint16_t nlen = _rd_u16(b + off + 8);
        if (off + DICT_CHECKPOINT_HEADER + nlen > entry_start) break;
        /* checkpoint norm 是长度前缀字段、无 NUL 终止，必须按 nlen 截取再比较，
         * 否则会串读到下一个 checkpoint 头导致比较错乱 */
        char cns[DICT_NORM_CAP];
        uint32_t clen = nlen;
        if (clen >= sizeof(cns)) clen = (uint32_t)sizeof(cns) - 1;
        memcpy(cns, b + off + DICT_CHECKPOINT_HEADER, clen);
        cns[clen] = '\0';
        int cmp = strcmp(cns, norm);
        if (cmp == 0) {
            sel_off = off;
            have_sel = true;
            break;
        }
        if (cmp > 0) break;
        sel_off = off;
        have_sel = true;
        off += DICT_CHECKPOINT_HEADER + nlen;
        scanned++;
    }
    if (!have_sel) {
        COS_LOG_E("dict: no checkpoint selected (scan=%u off=%u ent_start=%u)",
                  (unsigned)scanned, (unsigned)off, (unsigned)entry_start);
        return false;
    }

    uint32_t start_entry_idx = _rd_u32(b + sel_off);
    uint32_t start_entry_off = _rd_u32(b + sel_off + 4);

    uint32_t end_entry_idx = start_entry_idx + s_checkpoint_every;
    if (end_entry_idx > b_entry_count) end_entry_idx = b_entry_count;
    COS_LOG_I("dict: sel_cp idx=%u off=%u scan=[%u,%u)",
              (unsigned)start_entry_idx, (unsigned)start_entry_off,
              (unsigned)start_entry_idx, (unsigned)end_entry_idx);

    /* scan entries (sorted by normalized word) */
    uint32_t eoff = entry_start + start_entry_off;
    uint32_t eidx = start_entry_idx;
    while (eidx < end_entry_idx && eoff + DICT_ENTRY_HEADER_SIZE <= dsize) {
        uint16_t wl = _rd_u16(b + eoff + 0);
        uint16_t pl = _rd_u16(b + eoff + 2);
        uint16_t tl = _rd_u16(b + eoff + 4);
        uint16_t dl = _rd_u16(b + eoff + 6);
        uint16_t posl = _rd_u16(b + eoff + 8);
        uint16_t el = _rd_u16(b + eoff + 10);
        uint32_t total = DICT_ENTRY_HEADER_SIZE + (uint32_t)wl + pl + tl + dl + posl + el;
        if (eoff + total > dsize) break;

        const char *word = (const char *)(b + eoff + DICT_ENTRY_HEADER_SIZE);
        /* word 字段是长度前缀存储、无 NUL 终止，必须按 wl 截取后再归一化，
         * 否则会串读到 phonetic/translation 字段（如 "add" -> "addædvt. ..."） */
        char nw[DICT_NORM_CAP];
        uint32_t wlen = wl;
        if (wlen >= sizeof(nw)) wlen = (uint32_t)sizeof(nw) - 1;
        memcpy(nw, word, wlen);
        nw[wlen] = '\0';
        _norm(nw, nw, sizeof(nw));

        int cmp = strcmp(nw, norm);
        if (cmp == 0) {
            COS_LOG_I("dict: hit '%s'", nw);
            return _fill_result(b + eoff, out, cap);
        }
        if (cmp > 0) {
            COS_LOG_I("dict: miss, '%s' > '%s' after %u entries",
                      nw, norm, (unsigned)(eidx - start_entry_idx));
            break;   /* sorted, rest are larger */
        }

        eoff += total;
        eidx++;
    }
    COS_LOG_I("dict: miss, scanned %u entries eoff=%u dsize=%u",
              (unsigned)(eidx - start_entry_idx), (unsigned)eoff, (unsigned)dsize);
    return false;
}

/** 精确查询: 返回 true=命中并填充 out; false=未命中或失败 */
static bool dict_lookup(const char *query, char *out, size_t cap)
{
    char norm[DICT_NORM_CAP];
    if (_norm(query, norm, sizeof(norm)) == 0) return false;
    COS_LOG_I("dict: lookup norm='%s'", norm);

    int32_t bi = _find_bucket(norm);
    COS_LOG_I("dict: bucket=%d", (int)bi);
    if (bi < 0) return false;
    if (!_load_bucket((uint32_t)bi)) {
        COS_LOG_E("dict: load bucket %d failed", (int)bi);
        return false;
    }
    return _lookup_in_bucket(norm, out, cap);
}

/* ---------------------------------------------------------------- */
/* Fuzzy search: 前缀匹配, 流式游标遍历                              */
/* .dat 按 normalized 排序, 前缀匹配结果是连续区间, 因此可            */
/* 从 _find_bucket 定位起点, 顺序扫描直到第一个不匹配即结束,          */
/* 无需全库扫描, 内存恒定(只保留当前 bucket), 天然支持懒加载。       */
/* ---------------------------------------------------------------- */

typedef struct {
    uint32_t bi;            /* 当前 bucket 索引 */
    uint32_t ei;            /* bucket 内当前 entry 序号 */
    uint32_t eo;            /* bucket 内当前 entry 偏移(相对 entry 区起点) */
    char     prefix[DICT_NORM_CAP]; /* 归一化前缀 */
    bool     done;          /* 已扫描到末尾(无更多匹配) */
} dict_fuzzy_t;

/* 返回下一条前缀匹配的 word(原始拼写, 非归一化) + 翻译字段摘要。
 * 语义类似迭代器: 首次调用前必须 _fuzzy_open 初始化。
 * 内存: 只缓存当前 bucket, 跨 bucket 自动续读。 */
static bool _fuzzy_next(dict_fuzzy_t *fz, char *word, size_t word_cap,
                        char *trans, size_t trans_cap)
{
    if (fz->done) return false;
    const size_t plen = strlen(fz->prefix);
    if (plen == 0) { fz->done = true; return false; }

    while (fz->bi < s_bucket_count) {
        /* 当前 bucket 未加载或已切换 → 重新加载 */
        if (s_cur_bucket_idx != fz->bi) {
            if (!_load_bucket(fz->bi)) { fz->done = true; return false; }
        }
        const uint8_t *b = s_bucket_buf;
        if (memcmp(b, "BKT1", 4) != 0) { fz->done = true; return false; }
        uint32_t b_entry_count = _rd_u32(b + 8);
        uint32_t entry_start = _rd_u32(b + 20);
        uint32_t dsize = s_cur_bucket_size;
        if (entry_start > dsize) { fz->done = true; return false; }

        uint32_t eoff = entry_start + fz->eo;
        while (fz->ei < b_entry_count && eoff + DICT_ENTRY_HEADER_SIZE <= dsize) {
            uint16_t wl = _rd_u16(b + eoff + 0);
            uint16_t pl = _rd_u16(b + eoff + 2);
            uint16_t tl = _rd_u16(b + eoff + 4);
            uint16_t dl = _rd_u16(b + eoff + 6);
            uint16_t posl = _rd_u16(b + eoff + 8);
            uint16_t el = _rd_u16(b + eoff + 10);
            uint32_t total = DICT_ENTRY_HEADER_SIZE + (uint32_t)wl + pl + tl + dl + posl + el;
            if (eoff + total > dsize) break;

            const char *w = (const char *)(b + eoff + DICT_ENTRY_HEADER_SIZE);
            size_t wl_cap = wl < DICT_NORM_CAP - 1 ? wl : DICT_NORM_CAP - 1;

            /* 归一化并比较 */
            char nw[DICT_NORM_CAP];
            memcpy(nw, w, wl_cap);
            nw[wl_cap] = '\0';
            _norm(nw, nw, sizeof(nw));

            int cmp = strncmp(nw, fz->prefix, plen);
            if (cmp == 0) {
                /* 前缀命中 → 输出原始 word + translation 摘要, 游标前进 */
                size_t wn = wl;
                if (wn >= word_cap) wn = word_cap - 1;
                memcpy(word, w, wn);
                word[wn] = '\0';

                if (trans && trans_cap > 0) {
                    const char *t = w + wl + pl;   /* word 后是 phonetic, 再后是 translation */
                    size_t tn = tl;
                    if (tn >= trans_cap) tn = trans_cap - 1;
                    memcpy(trans, t, tn);
                    trans[tn] = '\0';
                }

                fz->eo = (eoff - entry_start) + total;
                fz->ei++;
                return true;
            }
            if (cmp > 0) {
                /* 已越过前缀区间(排序保证后续均不匹配) */
                fz->done = true;
                return false;
            }
            /* 仍在区间前, 跳过 */
            eoff += total;
            fz->ei++;
            fz->eo = (eoff - entry_start);
        }
        /* 当前 bucket 扫完 → 下一 bucket */
        fz->bi++;
        fz->ei = 0;
        fz->eo = 0;
    }
    fz->done = true;
    return false;
}

/* 初始化模糊游标: 定位到首个 >= prefix 的位置, 并预取第一个匹配 */
static bool _fuzzy_open(dict_fuzzy_t *fz, const char *query, char *first_word,
                        size_t word_cap, char *first_trans, size_t trans_cap)
{
    memset(fz, 0, sizeof(*fz));
    if (_norm(query, fz->prefix, sizeof(fz->prefix)) == 0) return false;
    COS_LOG_I("dict: fuzzy open prefix='%s'", fz->prefix);

    int32_t bi = _find_bucket(fz->prefix);
    if (bi < 0) {
        /* prefix 小于所有 bucket 首个 checkpoint: 从 bucket 0 起扫 */
        fz->bi = 0;
    } else {
        fz->bi = (uint32_t)bi;
    }
    /* 预取第一条, 无匹配则返回 false */
    return _fuzzy_next(fz, first_word, word_cap, first_trans, trans_cap);
}

/* ---------------------------------------------------------------- */
/* UI (album style)                                                  */
/* ---------------------------------------------------------------- */
#define _UI_BG        0x12121A
#define _UI_BTN_BG    0x1E1E2E
#define _UI_CAPSULE   0x34343F   /* bottom action capsule (light grey) */
#define _UI_PANEL     0x1A1A24
#define _UI_TEXT      0xFFFFFF
#define _UI_DIM       0x8A8AA0
#define _UI_ACCENT    0x6FA8FF

#define DICT_HIST_DIR  "/sdcard/history/ecdict"
#define DICT_HIST_FILE "/sdcard/history/ecdict/history.txt"

#define DICT_PILL_W    112
#define DICT_PILL_H    56
#define DICT_TA_W      190   /* 输入框展开宽度 */
#define DICT_TA_H      40
#define DICT_TA_W0     40    /* 输入框初始(圆形)宽度 */
#define DICT_TA_CX     120   /* 输入框中心 x(屏幕中心) */
#define DICT_TA_Y      52    /* 屏幕中央偏上 */

#define DICT_SCROLL_X  8
#define DICT_SCROLL_Y  30    /* 下移, 避开圆屏顶部圆弧, 标题不被裁切 */
#define DICT_SCROLL_W  224
#define DICT_SCROLL_H  136   /* 底部给悬浮胶囊留出空间 */
#define DICT_ROW_W     208
#define DICT_ROW_X     8

extern const lv_image_dsc_t cos_icon_search;

static cos_activity_t *s_act;
static lv_obj_t *s_root;
static lv_obj_t *s_pill;     /* 底部悬浮胶囊(搜索) */
static lv_obj_t *s_ta;       /* 输入框(圆 -> 胶囊) */
static lv_obj_t *s_kb;       /* 圆形键盘 */
static lv_obj_t *s_scroll;   /* 结果滚动区 */
static lv_obj_t *s_dim;      /* 搜索时全屏黑色底(遮住结果, 在其上展开输入框) */
static char s_result[DICT_RESULT_CAP];
static char s_query[DICT_QUERY_CAP];

/* 模糊搜索(懒加载)状态 */
static dict_fuzzy_t s_fuzzy;     /* 当前模糊游标 */
static bool s_fuzzy_mode;        /* 当前处于模糊结果列表模式 */
static bool s_fuzzy_more;        /* 还有更多匹配可加载 */
static int  s_fuzzy_y;           /* 列表当前渲染 y */
static char s_exact_word[DICT_WORD_CAP];    /* 精确命中词(非空 = 列表顶部置顶项) */
static char s_exact_trans[DICT_SUMMARY_CAP]; /* 精确命中词翻译摘要 */
static bool s_detail_mode;                  /* true = 详情/消息页(左滑回列表, 不退出) */

static void _show_idle(void);
static void _show_msg(const char *msg);
static void _show_result_ui(void);
static void _render_result(const char *word, const char *phon, const char *rest);
static void _save_history(const char *word);
static void _run_lookup(void);
static void _render_search_list(void);
static void _fuzzy_load_more(void);

/* 输入框动画: v: 0..100, 0=圆形小点 100=展开胶囊; 宽度向两侧对称拉伸 */
static void _ta_anim_exec(void *var, int32_t v)
{
    lv_obj_t *ta = (lv_obj_t *)var;
    int w = DICT_TA_W0 + (DICT_TA_W - DICT_TA_W0) * v / 100;
    lv_obj_set_width(ta, w);
    lv_obj_set_x(ta, DICT_TA_CX - w / 2);
}

static void _anim_ta(int to, uint32_t time, lv_anim_path_cb_t path,
                     lv_anim_ready_cb_t ready)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ta);
    lv_anim_set_values(&a, 0, to);
    lv_anim_set_time(&a, time);
    lv_anim_set_path_cb(&a, path);
    lv_anim_set_exec_cb(&a, _ta_anim_exec);
    if (ready) lv_anim_set_ready_cb(&a, ready);
    lv_anim_start(&a);
}

/* 收起动画完成后: 隐藏输入框/键盘, 执行查询 */
static void _collapse_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    _run_lookup();
}

/* 打开输入: 胶囊隐藏, 输入框由圆形向两边拉伸, 键盘弹出 */
static void _open_input(void)
{
    lv_obj_add_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_dim, LV_OBJ_FLAG_HIDDEN);   /* 黑色底先出现 */
    lv_obj_remove_flag(s_ta, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(s_ta, "");
    lv_obj_set_width(s_ta, DICT_TA_W0);
    lv_obj_set_x(s_ta, DICT_TA_CX - DICT_TA_W0 / 2);
    lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    _anim_ta(100, 220, lv_anim_path_ease_out, NULL);
}

/* 提交搜索: 输入框收起动画完成后查询 */
static void _do_search(const char *text)
{
    if (!text || !text[0]) {
        _show_idle();
        return;
    }
    strncpy(s_query, text, sizeof(s_query) - 1);
    s_query[sizeof(s_query) - 1] = '\0';

    if (lv_obj_has_flag(s_ta, LV_OBJ_FLAG_HIDDEN)) {
        /* 恢复上次搜索等场景: 输入框未展开, 直接查询 */
        _run_lookup();
        return;
    }
    _anim_ta(0, 200, lv_anim_path_ease_in, _collapse_done);
}

static void _run_lookup(void)
{
    if (_norm(s_query, s_query, sizeof(s_query)) == 0) {
        _show_idle();
        return;
    }

    if (!_dict_open()) {
        _show_msg("Dictionary unavailable\nPut ecdict.dat in\n/sdcard/ecdict/");
        return;
    }

    /* 重置精确命中状态(置顶项) */
    s_exact_word[0] = '\0';
    s_exact_trans[0] = '\0';

    if (!dict_lookup(s_query, s_result, sizeof(s_result))) {
        /* 精确未命中 → 前缀匹配列表(懒加载) */
        _render_search_list();
        return;
    }

    /* 精确命中: 保存置顶项(word + 翻译), 也进列表, 点箭头展开详情 */
    /* 解析结果: line1=word line2=phonetic rest=translation/[pos]def/ex */
    char *line1 = s_result;
    char *nl = strchr(line1, '\n');
    if (nl) *nl = '\0';
    char *line2 = nl ? nl + 1 : "";
    char *nl2 = strchr(line2, '\n');
    if (nl2) *nl2 = '\0';
    char *rest = nl2 ? nl2 + 1 : "";

    strncpy(s_exact_word, line1, sizeof(s_exact_word) - 1);
    s_exact_word[sizeof(s_exact_word) - 1] = '\0';

    /* rest 首行 = translation(中文释义) */
    char *tnl = strchr(rest, '\n');
    if (tnl) *tnl = '\0';
    strncpy(s_exact_trans, rest, sizeof(s_exact_trans) - 1);
    s_exact_trans[sizeof(s_exact_trans) - 1] = '\0';

    _save_history(line1);
    _render_search_list();
}

/* 在滚动区追加一行, 返回其 y 增量 */
static void _add_row(const char *text, cos_font_size_t fs, uint32_t color, int *y)
{
    if (!text || !text[0]) return;
    lv_obj_t *lb = lv_label_create(s_scroll);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(lb, DICT_ROW_W);
    lv_obj_set_pos(lb, DICT_ROW_X, *y);
    lv_obj_set_style_text_color(lb, lv_color_hex(color), 0);
    cos_label_set_font_size(lb, fs);
    lv_obj_set_style_text_line_space(lb, 2, 0);
    lv_label_set_text(lb, text);
    lv_obj_update_layout(s_scroll);
    *y += lv_obj_get_height(lb) + 6;
}

static void _render_result(const char *word, const char *phon, const char *rest)
{
    s_fuzzy_mode = false;
    s_fuzzy_more = false;
    s_detail_mode = true;
    lv_obj_clean(s_scroll);
    int y = 6;

    /* word 标题 */
    lv_obj_t *title = lv_label_create(s_scroll);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(title, DICT_ROW_W);
    lv_obj_set_pos(title, DICT_ROW_X, y);
    lv_obj_set_style_text_color(title, lv_color_hex(_UI_ACCENT), 0);
    cos_label_set_font_size(title, COS_FONT_SIZE_TALL);
    lv_label_set_text(title, word);
    lv_obj_update_layout(s_scroll);
    y += lv_obj_get_height(title) + 6;

    if (phon && phon[0]) {
        _add_row(phon, COS_FONT_SIZE_MICRO, _UI_DIM, &y);
    }

    /* rest 为 \n 分隔: translation / [pos] definition / ex: exchange */
    char *line = rest;
    while (line && line[0]) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, "ex: ", 4) == 0) {
            _add_row(line, COS_FONT_SIZE_MICRO, _UI_DIM, &y);
        } else {
            _add_row(line, COS_FONT_SIZE_MICRO, _UI_TEXT, &y);
        }
        if (!nl) break;
        line = nl + 1;
    }

    _show_result_ui();
}

static void _show_result_ui(void)
{
    lv_obj_remove_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(s_scroll, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------- */
/* Fuzzy list (懒加载匹配列表)                                       */
/* ---------------------------------------------------------------- */

#define _FUZZY_ROW_H  46   /* 两行: Word 标题 + 释义摘要 + 右侧箭头 */
#define _FUZZY_PAD_Y  6
#define _FUZZY_SUMMARY_CHARS 10   /* 中文释义摘要最大字符数(超出补 ...) */

/* 中文释义摘要: 取前 max_chars 个 UTF-8 字符(首个换行处截断), 有剩余内容补 "..." */
static void _summary(const char *in, char *out, size_t out_cap, size_t max_chars)
{
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t i = 0, chars = 0;
    while (in[i]) {
        if (in[i] == '\n') break;                  /* 取主要释义行 */
        if (chars >= max_chars) break;             /* 已满 max_chars 字 */
        unsigned char c = (unsigned char)in[i];
        size_t len = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        if (i + len >= out_cap - 1) break;         /* 缓冲余量不足 */
        memcpy(out + i, in + i, len);
        i += len;
        chars++;
    }
    if (in[i] != '\0' && i + 3 < out_cap) {        /* 还有内容 → 省略号 */
        memcpy(out + i, "...", 3);
        i += 3;
    }
    out[i] = '\0';
}

/* 点击行右箭头 → 精确查看该词详情 */
static void _fuzzy_row_cb(lv_event_t *e)
{
    lv_obj_t *arrow = lv_event_get_target(e);
    const char *word = lv_obj_get_user_data(arrow);
    if (!word || !word[0]) return;

    strncpy(s_query, word, sizeof(s_query) - 1);
    s_query[sizeof(s_query) - 1] = '\0';
    s_fuzzy_mode = false;   /* 退出列表模式, 显示详情 */
    s_exact_word[0] = '\0'; /* 从列表点入详情: 回列表时以该词重新生成列表 */
    s_exact_trans[0] = '\0';

    if (!dict_lookup(s_query, s_result, sizeof(s_result))) {
        _show_msg("Word not found");
        return;
    }
    char *line1 = s_result;
    char *nl = strchr(line1, '\n');
    if (nl) *nl = '\0';
    char *line2 = nl ? nl + 1 : "";
    char *nl2 = strchr(line2, '\n');
    if (nl2) *nl2 = '\0';
    char *rest = nl2 ? nl2 + 1 : "";
    _save_history(line1);
    _render_result(line1, line2, rest);
}

/* 箭头删除时释放 user_data(word 副本) */
static void _fuzzy_row_del_cb(lv_event_t *e)
{
    lv_obj_t *arrow = lv_event_get_target(e);
    char *w = lv_obj_get_user_data(arrow);
    if (w) {
        cos_free(w);
        lv_obj_set_user_data(arrow, NULL);
    }
}

/* 追加一个匹配行: Word 标题 + 中文释义摘要 + 右侧箭头(仅箭头可展开详情) */
static void _fuzzy_add_row(const char *word, const char *trans, int *y)
{
    lv_obj_t *row = lv_obj_create(s_scroll);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, DICT_ROW_W, _FUZZY_ROW_H);
    lv_obj_set_pos(row, DICT_ROW_X, *y);
    lv_obj_set_style_bg_color(row, lv_color_hex(_UI_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_50, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    /* 行本身不拦截点击 → 列表滑动顺畅; 只有右箭头可展开详情 */

    /* Word 标题 */
    lv_obj_t *title = lv_label_create(row);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(title, DICT_ROW_W - 40);
    lv_obj_set_pos(title, 8, 5);
    lv_obj_set_style_text_color(title, lv_color_hex(_UI_TEXT), 0);
    cos_label_set_font_size(title, COS_FONT_SIZE_EXTRA_SMALL);
    lv_label_set_text(title, word);

    /* 中文释义摘要 (前 10 字 + ...) */
    if (trans && trans[0]) {
        char sum[DICT_SUMMARY_CAP];
        _summary(trans, sum, sizeof(sum), _FUZZY_SUMMARY_CHARS);
        lv_obj_t *sumlb = lv_label_create(row);
        lv_label_set_long_mode(sumlb, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(sumlb, DICT_ROW_W - 40);
        lv_obj_set_pos(sumlb, 8, 25);
        lv_obj_set_style_text_color(sumlb, lv_color_hex(_UI_DIM), 0);
        cos_label_set_font_size(sumlb, COS_FONT_SIZE_MICRO);
        lv_label_set_text(sumlb, sum);
    }

    /* 右侧箭头: 唯一可展开入口 */
    lv_obj_t *arrbtn = lv_button_create(row);
    lv_obj_remove_style_all(arrbtn);
    lv_obj_set_size(arrbtn, 24, _FUZZY_ROW_H - 4);
    lv_obj_align(arrbtn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(arrbtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(arrbtn, _fuzzy_row_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(arrbtn, _fuzzy_row_del_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_user_data(arrbtn, cos_strdup(word));
    lv_obj_t *arr = lv_label_create(arrbtn);
    lv_obj_set_style_text_font(arr, &COS_FONT_ICON, 0);
    lv_obj_set_style_text_color(arr, lv_color_hex(_UI_DIM), 0);
    lv_label_set_text(arr, RI_ARROW_RIGHT_S_LINE);
    lv_obj_center(arr);

    *y += _FUZZY_ROW_H + _FUZZY_PAD_Y;
}

/* 从游标加载下一批匹配(懒加载页), 返回本批条数 */
static void _fuzzy_load_more(void)
{
    if (!s_fuzzy_mode || !s_fuzzy_more) return;
    char word[DICT_WORD_CAP];
    char trans[DICT_SUMMARY_CAP];
    int n = 0;
    while (n < DICT_FUZZY_PAGE && _fuzzy_next(&s_fuzzy, word, sizeof(word), trans, sizeof(trans))) {
        if (s_exact_word[0] && strcmp(word, s_exact_word) == 0) continue; /* 已置顶, 跳过 */
        _fuzzy_add_row(word, trans, &s_fuzzy_y);
        n++;
    }
    if (n < DICT_FUZZY_PAGE) {
        s_fuzzy_more = false;   /* 已耗尽 */
        if (n == 0) {
            lv_obj_t *end = lv_label_create(s_scroll);
            lv_label_set_long_mode(end, LV_LABEL_LONG_MODE_CLIP);
            lv_obj_set_width(end, DICT_ROW_W);
            lv_obj_set_pos(end, DICT_ROW_X, s_fuzzy_y);
            lv_obj_set_style_text_color(end, lv_color_hex(_UI_DIM), 0);
            cos_label_set_font_size(end, COS_FONT_SIZE_MICRO);
            lv_label_set_text(end, "-- end of matches --");
            s_fuzzy_y += 18;
        }
    }
    lv_obj_update_layout(s_scroll);
}

/* 滚动接近底部 → 加载下一批 */
static void _fuzzy_scroll_cb(lv_event_t *e)
{
    (void)e;
    if (!s_fuzzy_mode || !s_fuzzy_more) return;
    int32_t sy = lv_obj_get_scroll_y(s_scroll);
    int32_t bottom = lv_obj_get_scroll_bottom(s_scroll);
    if (bottom <= 0) return;                    /* 未溢出(可能已滚动到底) */
    if (sy >= bottom - 8) {
        _fuzzy_load_more();
    }
}

/* 搜索 → 渲染匹配列表: 精确命中词置顶, 其后为前缀匹配(按词典序),
 * 每行 = Word + 中文释义前 10 字摘要 + 右侧箭头, 点箭头展开详情。
 * 列表可滚动, 匹配项按需懒加载。 */
static void _render_search_list(void)
{
    lv_obj_clean(s_scroll);
    s_fuzzy_mode = true;
    s_fuzzy_more = true;
    s_detail_mode = false;
    s_fuzzy_y = 6;

    /* 标题: 显示查询词(精确命中 → results, 否则 fuzzy match) */
    lv_obj_t *hint = lv_label_create(s_scroll);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(hint, DICT_ROW_W);
    lv_obj_set_pos(hint, DICT_ROW_X, s_fuzzy_y);
    lv_obj_set_style_text_color(hint, lv_color_hex(_UI_DIM), 0);
    cos_label_set_font_size(hint, COS_FONT_SIZE_MICRO);
    char hbuf[DICT_QUERY_CAP + 24];
    if (s_exact_word[0]) {
        snprintf(hbuf, sizeof(hbuf), "results: '%s'", s_query);
    } else {
        snprintf(hbuf, sizeof(hbuf), "fuzzy match: '%s'", s_query);
    }
    lv_label_set_text(hint, hbuf);
    s_fuzzy_y += 20;

    /* 精确命中词置顶(最符合) */
    if (s_exact_word[0]) {
        _fuzzy_add_row(s_exact_word, s_exact_trans, &s_fuzzy_y);
    }

    /* 前缀匹配游标: 跳过已置顶的精确词 */
    char first[DICT_WORD_CAP];
    char ftrans[DICT_SUMMARY_CAP];
    if (_fuzzy_open(&s_fuzzy, s_query, first, sizeof(first), ftrans, sizeof(ftrans))) {
        if (!(s_exact_word[0] && strcmp(first, s_exact_word) == 0)) {
            _fuzzy_add_row(first, ftrans, &s_fuzzy_y);
        }
    } else {
        s_fuzzy_more = false;
        if (!s_exact_word[0]) {
            _show_msg("Word not found");
            return;
        }
    }

    /* 首屏尽量填满可视区, 避免初次无滚动; 之后靠滚动懒加载 */
    lv_obj_update_layout(s_scroll);
    int32_t h = lv_obj_get_height(s_scroll);
    int guard = 0;
    while (s_fuzzy_more && s_fuzzy_y < (int)h && guard++ < 8) {
        _fuzzy_load_more();
        lv_obj_update_layout(s_scroll);
    }
    _show_result_ui();
}

static void _show_msg(const char *msg)
{
    s_fuzzy_mode = false;
    s_fuzzy_more = false;
    s_detail_mode = true;
    lv_obj_clean(s_scroll);
    lv_obj_t *lb = lv_label_create(s_scroll);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(lb, 200);
    lv_obj_set_style_text_color(lb, lv_color_hex(_UI_DIM), 0);
    cos_label_set_font_size(lb, COS_FONT_SIZE_EXTRA_SMALL);
    lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lb);
    lv_label_set_text(lb, msg);
    _show_result_ui();
}

/* 空页面: 仅底部悬浮胶囊 */
static void _show_idle(void)
{
    s_fuzzy_mode = false;
    s_fuzzy_more = false;
    s_detail_mode = false;
    lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(s_ta, "");
}

/* ---------------------------------------------------------------- */
/* History                                                           */
/* ---------------------------------------------------------------- */
static void _save_history(const char *word)
{
    if (!word || !word[0]) return;
    cos_storage_mkdir_recursive(DICT_HIST_DIR);

    char *old = cos_storage_read_file(DICT_HIST_FILE);
    if (old) {
        /* 第一行已是该词则无需写 */
        char *nl = strchr(old, '\n');
        size_t fl = nl ? (size_t)(nl - old) : strlen(old);
        if (fl == strlen(word) && strncmp(old, word, fl) == 0) {
            cos_free(old);
            return;
        }
    }

    char buf[COS_FS_PATH_MAX * 2];
    int n = snprintf(buf, sizeof(buf), "%s\n", word);
    if (old && n > 0 && (size_t)n + strlen(old) < sizeof(buf) - 1) {
        memcpy(buf + n, old, strlen(old) + 1);   /* 旧历史保留在其后 */
    }
    if (old) cos_free(old);
    cos_storage_write_file(DICT_HIST_FILE, buf, strlen(buf));
}

static bool _load_history(char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!cos_storage_is_file(DICT_HIST_FILE)) return false;

    char *content = cos_storage_read_file(DICT_HIST_FILE);
    if (!content) return false;

    /* line 1: 上次搜索的单词 */
    char *nl = strchr(content, '\n');
    if (nl) *nl = '\0';
    size_t len = strlen(content);
    while (len > 0 && (content[len - 1] == '\r' || content[len - 1] == ' ')) {
        content[--len] = '\0';
    }
    if (len > 0 && len < cap) {
        memcpy(out, content, len + 1);
        cos_free(content);
        return true;
    }
    cos_free(content);
    return false;
}

/* ---------------------------------------------------------------- */
/* Events                                                            */
/* ---------------------------------------------------------------- */
static void _pill_cb(lv_event_t *e)
{
    (void)e;
    _open_input();
}

static void _on_ta_ready(lv_event_t *e)
{
    (void)e;
    const char *text = lv_textarea_get_text(s_ta);
    _do_search(text);
}

/* ---------------------------------------------------------------- */
/* UI build                                                          */
/* ---------------------------------------------------------------- */
static void _build_ui(cos_activity_t *act)
{
    s_act = act;
    s_root = cos_activity_get_view(act);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(_UI_BG), 0);
    cos_round_clip(s_root);

    /* 结果滚动区 (word 标题 + 字段) */
    s_scroll = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_scroll);
    lv_obj_set_size(s_scroll, DICT_SCROLL_W, DICT_SCROLL_H);
    lv_obj_set_pos(s_scroll, DICT_SCROLL_X, DICT_SCROLL_Y);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_0, 0);
    lv_obj_set_style_radius(s_scroll, 10, 0);
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(s_scroll, _fuzzy_scroll_cb, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);

    /* 搜索遮罩: 全屏黑色底, 盖住结果; 输入框/键盘在其上层展开 */
    s_dim = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_dim);
    lv_obj_set_size(s_dim, 240, 240);
    lv_obj_set_pos(s_dim, 0, 0);
    lv_obj_set_style_bg_color(s_dim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);

    /* 输入框: 圆形 -> 圆角矩形(动画), 屏幕中央偏上 */
    s_ta = lv_textarea_create(s_root);
    lv_obj_remove_style_all(s_ta);
    lv_obj_set_size(s_ta, DICT_TA_W0, DICT_TA_H);
    lv_obj_set_pos(s_ta, DICT_TA_CX - DICT_TA_W0 / 2, DICT_TA_Y);
    lv_obj_set_style_bg_opa(s_ta, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(s_ta, lv_color_white(), 0);
    lv_obj_set_style_radius(s_ta, DICT_TA_H / 2, 0);
    lv_obj_set_style_text_color(s_ta, lv_color_white(), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(0x8A8F98), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_pad_all(s_ta, 0, 0);
    lv_obj_set_style_pad_left(s_ta, 18, 0);
    lv_obj_set_style_pad_top(s_ta, 11, 0);
    lv_textarea_set_placeholder_text(s_ta, "search in ecdict");
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_14, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 48);
    lv_textarea_set_cursor_click_pos(s_ta, true);
    lv_obj_add_event_cb(s_ta, _on_ta_ready, LV_EVENT_READY, NULL);
    lv_obj_add_flag(s_ta, LV_OBJ_FLAG_HIDDEN);

    /* 圆形键盘 (默认隐藏, 点击胶囊后弹出) */
    s_kb = cos_round_keyboard_create(s_root);
    cos_round_keyboard_set_mode(s_kb, COS_RKB_MODE_EN);
    cos_round_keyboard_set_textarea(s_kb, s_ta);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    /* 底部悬浮胶囊: 搜索按钮 */
    s_pill = lv_button_create(s_root);
    lv_obj_remove_style_all(s_pill);
    lv_obj_set_size(s_pill, DICT_PILL_W, DICT_PILL_H);
    lv_obj_align(s_pill, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(s_pill, lv_color_hex(_UI_CAPSULE), 0);
    lv_obj_set_style_bg_opa(s_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_pill, DICT_PILL_H / 2, 0);
    lv_obj_set_style_border_width(s_pill, 0, 0);
    lv_obj_add_event_cb(s_pill, _pill_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic = lv_image_create(s_pill);
    lv_image_set_src(ic, &cos_icon_search);
    lv_obj_center(ic);
}

/* ---------------------------------------------------------------- */
/* Lifecycle                                                         */
/* ---------------------------------------------------------------- */
static void _on_enter(cos_activity_t *act)
{
    _build_ui(act);

    /* 恢复上次搜索 (album 风格) */
    char hist[DICT_QUERY_CAP];
    if (_load_history(hist, sizeof(hist))) {
        _do_search(hist);
    } else {
        _show_idle();
    }
}

static void _on_destroy(cos_activity_t *act)
{
    (void)act;
    _dict_close();
    s_act = NULL;
    s_root = NULL;
}

static bool _on_swipe_back(cos_activity_t *act, lv_dir_t dir)
{
    (void)act;
    (void)dir;

    /* 详情/消息页 → 返回搜索列表(不退出 app) */
    if (s_detail_mode) {
        _render_search_list();
        return true;
    }

    /* 搜索列表 / 输入框(搜索框) / 空闲页 → 默认退出 app */
    return false;
}

/* ---------------------------------------------------------------- */
/* App entry                                                         */
/* ---------------------------------------------------------------- */
void cos_dictionary_enter(void)
{
    static const cos_activity_lifecycle_t lifecycle = {
        .on_enter = _on_enter,
        .on_destroy = _on_destroy,
        .on_swipe_back = _on_swipe_back,
    };
    cos_activity_t *act = cos_activity_create(&lifecycle);
    if (!act) return;
    cos_activity_set_type(act, COS_ACTIVITY_TYPE_APP);
    cos_activity_enter(act);
}
