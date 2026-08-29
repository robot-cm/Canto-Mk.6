/**
 * @file eos_dictionary.c
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

#include "eos_config.h"
#include "eos_dictionary.h"
#include "eos_activity.h"
#include "eos_theme.h"
#include "eos_font.h"
#include "eos_mem.h"
#include "eos_round_clip.h"
#include "eos_round_keyboard.h"
#include "eos_service_storage.h"
#include "eos_app_header.h"
#include "eos_log.h"

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
static eos_file_t s_fp;
static bool s_fp_open;
static uint8_t *s_idx_buf;       /* bucket index cache (PSRAM)   */
static uint8_t *s_bucket_buf;    /* current bucket data (PSRAM)  */
static uint32_t s_bucket_count;
static uint32_t s_checkpoint_every;
static uint32_t s_cur_bucket_size;
static bool s_loaded;

static bool _read_full(uint8_t *buf, size_t size)
{
    size_t got = 0;
    while (got < size) {
        ssize_t r = eos_storage_file_read(s_fp, buf + got, size - got);
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
        eos_storage_file_close(s_fp);
        s_fp = NULL;
        s_fp_open = false;
    }
    if (s_idx_buf) {
        eos_free(s_idx_buf);
        s_idx_buf = NULL;
    }
    if (s_bucket_buf) {
        eos_free(s_bucket_buf);
        s_bucket_buf = NULL;
    }
    s_loaded = false;
    s_cur_bucket_size = 0;
}

static bool _dict_open(void)
{
    if (s_loaded) return true;

    if (!s_fp_open) {
        s_fp = eos_storage_file_open_read(DICT_PATH);
        if (!s_fp) {
            EOS_LOG_E("dict: cannot open %s (SD mounted?)", DICT_PATH);
            return false;
        }
        s_fp_open = true;
    }

    uint8_t hdr[DICT_HEADER_SIZE];
    if (eos_storage_file_seek(s_fp, 0) != EOS_OK) return false;
    if (!_read_full(hdr, DICT_HEADER_SIZE)) return false;

    if (memcmp(hdr, DICT_MAGIC, DICT_MAGIC_LEN) != 0) {
        EOS_LOG_E("dict: bad magic (not a step4_1_export .dat?)");
        return false;
    }
    if (_rd_u32(hdr + 8) != DICT_VERSION) {
        EOS_LOG_E("dict: unsupported version %u", (unsigned)_rd_u32(hdr + 8));
        return false;
    }
    /* 76-byte header produced by step4_1_export.py, FMT "<8sIIIQIIIIQQQQ":
     * magic(8) version@8 header_size@12 entry_count@16 bucket_count(Q)@20
     * bucket_target@28 checkpoint_every@32 entry_header_size@36 reserved@40
     * bucket_index_off(Q)@44 bucket_data_off@52 total_data_size@60 sha256@68 */
    if (_rd_u32(hdr + 36) != DICT_ENTRY_HEADER_SIZE) {
        EOS_LOG_E("dict: bad entry header size %u", (unsigned)_rd_u32(hdr + 36));
        return false;
    }

    s_bucket_count = (uint32_t)_rd_u64(hdr + 20);
    s_checkpoint_every = _rd_u32(hdr + 32);
    if (s_bucket_count == 0 || s_bucket_count > DICT_MAX_BUCKETS) {
        EOS_LOG_E("dict: bad bucket count %u", (unsigned)s_bucket_count);
        return false;
    }

    if (!s_idx_buf) s_idx_buf = (uint8_t *)eos_malloc((size_t)s_bucket_count * DICT_BUCKET_INDEX_ITEM);
    if (!s_idx_buf) {
        EOS_LOG_E("dict: no mem for bucket index");
        return false;
    }
    if (eos_storage_file_seek(s_fp, (uint32_t)_rd_u64(hdr + 44)) != EOS_OK) return false;
    if (!_read_full(s_idx_buf, (size_t)s_bucket_count * DICT_BUCKET_INDEX_ITEM)) return false;

    if (!s_bucket_buf) s_bucket_buf = (uint8_t *)eos_malloc(DICT_BUCKET_BUF_CAP);
    if (!s_bucket_buf) {
        EOS_LOG_E("dict: no mem for bucket buffer");
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
    if (eos_storage_file_seek(s_fp, (uint32_t)abs_off) != EOS_OK) {
        EOS_LOG_E("dict: cp seek %llu failed", (unsigned long long)abs_off);
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
    EOS_LOG_I("dict: load bucket=%u data_off=%llu dsize=%u",
              (unsigned)bi, (unsigned long long)data_off, (unsigned)dsize);
    if (dsize == 0 || dsize > DICT_BUCKET_BUF_CAP) {
        EOS_LOG_E("dict: bad dsize=%u cap=%u", (unsigned)dsize, (unsigned)DICT_BUCKET_BUF_CAP);
        return false;
    }
    if (eos_storage_file_seek(s_fp, (uint32_t)data_off) != EOS_OK) {
        EOS_LOG_E("dict: seek %llu failed", (unsigned long long)data_off);
        return false;
    }
    if (!_read_full(s_bucket_buf, dsize)) {
        EOS_LOG_E("dict: read bucket failed");
        return false;
    }
    s_cur_bucket_size = dsize;
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
        EOS_LOG_E("dict: bucket magic %.4s", (const char *)b);
        return false;
    }
    uint32_t b_entry_count = _rd_u32(b + 8);
    uint32_t b_cp_count = _rd_u32(b + 12);
    uint32_t cp_start = _rd_u32(b + 16);
    uint32_t entry_start = _rd_u32(b + 20);
    uint32_t dsize = s_cur_bucket_size;
    EOS_LOG_I("dict: bkt entries=%u cps=%u cp_start=%u ent_start=%u dsize=%u",
              (unsigned)b_entry_count, (unsigned)b_cp_count,
              (unsigned)cp_start, (unsigned)entry_start, (unsigned)dsize);

    if (cp_start + DICT_CHECKPOINT_HEADER > entry_start || entry_start > dsize) {
        EOS_LOG_E("dict: bad bucket layout");
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
        EOS_LOG_E("dict: no checkpoint selected (scan=%u off=%u ent_start=%u)",
                  (unsigned)scanned, (unsigned)off, (unsigned)entry_start);
        return false;
    }

    uint32_t start_entry_idx = _rd_u32(b + sel_off);
    uint32_t start_entry_off = _rd_u32(b + sel_off + 4);

    uint32_t end_entry_idx = start_entry_idx + s_checkpoint_every;
    if (end_entry_idx > b_entry_count) end_entry_idx = b_entry_count;
    EOS_LOG_I("dict: sel_cp idx=%u off=%u scan=[%u,%u)",
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
            EOS_LOG_I("dict: hit '%s'", nw);
            return _fill_result(b + eoff, out, cap);
        }
        if (cmp > 0) {
            EOS_LOG_I("dict: miss, '%s' > '%s' after %u entries",
                      nw, norm, (unsigned)(eidx - start_entry_idx));
            break;   /* sorted, rest are larger */
        }

        eoff += total;
        eidx++;
    }
    EOS_LOG_I("dict: miss, scanned %u entries eoff=%u dsize=%u",
              (unsigned)(eidx - start_entry_idx), (unsigned)eoff, (unsigned)dsize);
    return false;
}

/** 精确查询: 返回 true=命中并填充 out; false=未命中或失败 */
static bool dict_lookup(const char *query, char *out, size_t cap)
{
    char norm[DICT_NORM_CAP];
    if (_norm(query, norm, sizeof(norm)) == 0) return false;
    EOS_LOG_I("dict: lookup norm='%s'", norm);

    int32_t bi = _find_bucket(norm);
    EOS_LOG_I("dict: bucket=%d", (int)bi);
    if (bi < 0) return false;
    if (!_load_bucket((uint32_t)bi)) {
        EOS_LOG_E("dict: load bucket %d failed", (int)bi);
        return false;
    }
    return _lookup_in_bucket(norm, out, cap);
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

extern const lv_image_dsc_t eos_icon_search;

static eos_activity_t *s_act;
static lv_obj_t *s_root;
static lv_obj_t *s_pill;     /* 底部悬浮胶囊(搜索) */
static lv_obj_t *s_ta;       /* 输入框(圆 -> 胶囊) */
static lv_obj_t *s_kb;       /* 圆形键盘 */
static lv_obj_t *s_scroll;   /* 结果滚动区 */
static lv_obj_t *s_dim;      /* 搜索时全屏黑色底(遮住结果, 在其上展开输入框) */
static char s_result[DICT_RESULT_CAP];
static char s_query[DICT_QUERY_CAP];

static void _show_idle(void);
static void _show_msg(const char *msg);
static void _show_result_ui(void);
static void _render_result(const char *word, const char *phon, const char *rest);
static void _save_history(const char *word);
static void _run_lookup(void);

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

    if (!dict_lookup(s_query, s_result, sizeof(s_result))) {
        _show_msg("Word not found");
        return;
    }

    /* 解析结果: line1=word line2=phonetic rest=translation/[pos]def/ex */
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

/* 在滚动区追加一行, 返回其 y 增量 */
static void _add_row(const char *text, eos_font_size_t fs, uint32_t color, int *y)
{
    if (!text || !text[0]) return;
    lv_obj_t *lb = lv_label_create(s_scroll);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(lb, DICT_ROW_W);
    lv_obj_set_pos(lb, DICT_ROW_X, *y);
    lv_obj_set_style_text_color(lb, lv_color_hex(color), 0);
    eos_label_set_font_size(lb, fs);
    lv_obj_set_style_text_line_space(lb, 2, 0);
    lv_label_set_text(lb, text);
    lv_obj_update_layout(s_scroll);
    *y += lv_obj_get_height(lb) + 6;
}

static void _render_result(const char *word, const char *phon, const char *rest)
{
    lv_obj_clean(s_scroll);
    int y = 6;

    /* word 标题 */
    lv_obj_t *title = lv_label_create(s_scroll);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(title, DICT_ROW_W);
    lv_obj_set_pos(title, DICT_ROW_X, y);
    lv_obj_set_style_text_color(title, lv_color_hex(_UI_ACCENT), 0);
    eos_label_set_font_size(title, EOS_FONT_SIZE_TALL);
    lv_label_set_text(title, word);
    lv_obj_update_layout(s_scroll);
    y += lv_obj_get_height(title) + 6;

    if (phon && phon[0]) {
        _add_row(phon, EOS_FONT_SIZE_MICRO, _UI_DIM, &y);
    }

    /* rest 为 \n 分隔: translation / [pos] definition / ex: exchange */
    char *line = rest;
    while (line && line[0]) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, "ex: ", 4) == 0) {
            _add_row(line, EOS_FONT_SIZE_MICRO, _UI_DIM, &y);
        } else {
            _add_row(line, EOS_FONT_SIZE_MICRO, _UI_TEXT, &y);
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

static void _show_msg(const char *msg)
{
    lv_obj_clean(s_scroll);
    lv_obj_t *lb = lv_label_create(s_scroll);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(lb, 200);
    lv_obj_set_style_text_color(lb, lv_color_hex(_UI_DIM), 0);
    eos_label_set_font_size(lb, EOS_FONT_SIZE_EXTRA_SMALL);
    lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lb);
    lv_label_set_text(lb, msg);
    _show_result_ui();
}

/* 空页面: 仅底部悬浮胶囊 */
static void _show_idle(void)
{
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
    eos_storage_mkdir_recursive(DICT_HIST_DIR);

    char *old = eos_storage_read_file(DICT_HIST_FILE);
    if (old) {
        /* 第一行已是该词则无需写 */
        char *nl = strchr(old, '\n');
        size_t fl = nl ? (size_t)(nl - old) : strlen(old);
        if (fl == strlen(word) && strncmp(old, word, fl) == 0) {
            eos_free(old);
            return;
        }
    }

    char buf[EOS_FS_PATH_MAX * 2];
    int n = snprintf(buf, sizeof(buf), "%s\n", word);
    if (old && n > 0 && (size_t)n + strlen(old) < sizeof(buf) - 1) {
        memcpy(buf + n, old, strlen(old) + 1);   /* 旧历史保留在其后 */
    }
    if (old) eos_free(old);
    eos_storage_write_file(DICT_HIST_FILE, buf, strlen(buf));
}

static bool _load_history(char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!eos_storage_is_file(DICT_HIST_FILE)) return false;

    char *content = eos_storage_read_file(DICT_HIST_FILE);
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
        eos_free(content);
        return true;
    }
    eos_free(content);
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
static void _build_ui(eos_activity_t *act)
{
    s_act = act;
    s_root = eos_activity_get_view(act);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(_UI_BG), 0);
    eos_round_clip(s_root);

    /* 结果滚动区 (word 标题 + 字段) */
    s_scroll = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_scroll);
    lv_obj_set_size(s_scroll, DICT_SCROLL_W, DICT_SCROLL_H);
    lv_obj_set_pos(s_scroll, DICT_SCROLL_X, DICT_SCROLL_Y);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_0, 0);
    lv_obj_set_style_radius(s_scroll, 10, 0);
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_AUTO);
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
    s_kb = eos_round_keyboard_create(s_root);
    eos_round_keyboard_set_mode(s_kb, EOS_RKB_MODE_EN);
    eos_round_keyboard_set_textarea(s_kb, s_ta);
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
    lv_image_set_src(ic, &eos_icon_search);
    lv_obj_center(ic);
}

/* ---------------------------------------------------------------- */
/* Lifecycle                                                         */
/* ---------------------------------------------------------------- */
static void _on_enter(eos_activity_t *act)
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

static void _on_destroy(eos_activity_t *act)
{
    (void)act;
    _dict_close();
    s_act = NULL;
    s_root = NULL;
}

static bool _on_swipe_back(eos_activity_t *act, lv_dir_t dir)
{
    (void)act;
    (void)dir;
    eos_activity_back();
    return true;
}

/* ---------------------------------------------------------------- */
/* App entry                                                         */
/* ---------------------------------------------------------------- */
void eos_dictionary_enter(void)
{
    static const eos_activity_lifecycle_t lifecycle = {
        .on_enter = _on_enter,
        .on_destroy = _on_destroy,
        .on_swipe_back = _on_swipe_back,
    };
    eos_activity_t *act = eos_activity_create(&lifecycle);
    if (!act) return;
    eos_activity_set_type(act, EOS_ACTIVITY_TYPE_APP);
    eos_activity_enter(act);
}
