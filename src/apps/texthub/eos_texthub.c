/**
 * @file eos_texthub.c
 * @brief Texthub - native C text reader (.txt/.md) for /sdcard/texthub/
 *
 * Replaces the old JS app (apps/texthub) which crashed with
 * JERRY_FATAL_OUT_OF_MEMORY because it synchronously enumerated every file
 * under /sdcard/texthub/ into the fixed 512KB JerryScript heap.
 *
 * C version design:
 *   - Lazy File Manager (FM) overlay: only the directory being expanded is
 *     read (eos_storage_dir_open); children are freed when collapsed or when
 *     the FM closes (memory stays small, no full-tree scan).
 *   - Chunked reading: files <= 128KB load in one shot; larger files are
 *     read segment-by-segment (16KB, newline-aligned) on scroll end.
 *   - History: /sdcard/history/texthub/latest.txt stores the last opened
 *     file path; it is auto-opened on entry (FM shown when absent).
 *   - UI (240x240 round): [FM] [scrolling filename] [> next], text card,
 *     bottom "idx/total" indicator. English only; CJK shown via jbm_13's
 *     han_sans_13 fallback.
 *
 * UI layout (all inside the round clipping region):
 *   System statusbar: y0..24 (owned by the framework)
 *   App bar: y30..56  FM btn(x8,34w) | filename clip(x48,152w) | next btn(x204,34w)
 *   Text card: x14 y60 w212 h166, rounded, drag-to-scroll
 *   Bottom: y230, "idx/total | seg x" (jbm_10, centered, inside safe arc)
 */

#include "eos_texthub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "eos_log.h"
#include "eos_config.h"
#include "eos_mem.h"
#include "eos_activity.h"
#include "eos_service_storage.h"
#include "eos_storage_paths.h"
#include "eos_font.h"
#include "eos_icon.h"
#include "ui/system/eos_round_clip.h"
#include "lvgl.h"

#define EOS_LOG_TAG "Texthub"

/* ------------------------------------------------------------------ */
/* Config                                                             */
/* ------------------------------------------------------------------ */

#define TH_DIR          "/sdcard/texthub"
#define TH_HIST_DIR     "/sdcard/history/texthub"
#define TH_HIST_FILE    "/sdcard/history/texthub/latest.txt"

#define TH_READ_ONCE_MAX   (128 * 1024)   /* files this size load at once  */
#define TH_SEG_BYTES       (16 * 1024)    /* chunk size for large files    */
#define TH_SEG_LINE_MAX    (4096)         /* guard against one giant line  */
#define TH_MAX_FILE_SIZE   (32 * 1024 * 1024) /* refuse anything bigger    */

#define TH_FM_DEPTH_MAX    6              /* FM tree max recursion depth   */
#define TH_FM_MAX_CHILD    300            /* entries per expanded dir      */
#define TH_FM_MAX_VISIBLE  300            /* visible rows cap              */
#define TH_FM_ROW_H        24             /* compact row spacing (TINY 10px) */

#define TH_NAME_W          152            /* filename clip width           */
#define TH_BAR_Y           30             /* app bar top                   */
#define TH_BAR_H           26
#define TH_BTN_W           34

#define _UI_BG        0x0E0E14
#define _UI_CARD      0xFFFFFF
#define _UI_CARD_OPA  8
#define _UI_TEXT      0xFFFFFF
#define _UI_DIM       0x8A8F98
#define _UI_ACCENT    0x6CB6FF            /* directories in the FM         */
#define _UI_PANEL     0x1E1E2E

/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct th_node_t th_node_t;
struct th_node_t {
    char    name[EOS_FS_NAME_MAX];
    char    path[EOS_FS_PATH_MAX];
    bool    is_dir;
    bool    expanded;
    bool    loaded;
    int     depth;                       /* depth in the FM tree         */
    int     child_count;
    th_node_t *children;                 /* array of children (lazy)      */
};

typedef struct {
    th_node_t *node;
    int        depth;
} th_item_t;

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

static eos_activity_t *s_act;
static lv_obj_t *s_root, *s_name, *s_name_clip, *s_card, *s_body, *s_bottom;
static lv_obj_t *s_fm_btn, *s_next_btn;

/* current file */
static char     *s_cur_path;             /* full path or NULL            */
static char     *s_cur_dir;              /* parent dir or NULL           */
static int       s_cur_idx;              /* index inside same-dir list   */
static int       s_cur_total;

/* text buffer */
static eos_file_t s_fp = EOS_FILE_INVALID;
static char      *s_text;                /* full text (small) or segment */
static uint32_t   s_size;
static bool       s_seg_mode;
static uint32_t   s_seg_start, s_seg_end, s_seg_idx;

/* file manager overlay */
static bool       s_fm_open;
static lv_obj_t  *s_fm_mask, *s_fm_panel, *s_fm_list, *s_fm_path;
static th_node_t *s_fm_root;
static th_item_t *s_fm_items;
static int        s_fm_item_count;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static const char *_base(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static bool _is_text_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot[1] == '\0') return false;
    const char *e = dot + 1;
    return (strcasecmp(e, "txt") == 0) || (strcasecmp(e, "md") == 0);
}

static void _dir_of(const char *path, char *buf, size_t sz)
{
    const char *s = strrchr(path, '/');
    size_t len = s ? (size_t)(s - path) : 0;
    if (len >= sz) len = sz - 1;
    memcpy(buf, path, len);
    buf[len] = '\0';
    if (len == 0) strcpy(buf, "/");
}

static int _cmp_name(const void *a, const void *b)
{
    const char *const *pa = (const char *const *)a;
    const char *const *pb = (const char *const *)b;
    return strcasecmp(*pa, *pb);
}

/* ------------------------------------------------------------------ */
/* Text loading                                                       */
/* ------------------------------------------------------------------ */

static void _free_text(void)
{
    if (s_fp != EOS_FILE_INVALID) {
        eos_storage_file_close(s_fp);
        s_fp = EOS_FILE_INVALID;
    }
    if (s_text) {
        eos_free(s_text);
        s_text = NULL;
    }
    s_seg_mode = false;
    s_seg_start = s_seg_end = s_seg_idx = 0;
}

/* Read one segment (newline-aligned) starting at s_seg_end (or 0 for seg 0)
 * into s_text, advancing s_seg_start / s_seg_end. */
static bool _seg_load(uint32_t idx)
{
    if (s_fp == EOS_FILE_INVALID) return false;

    uint32_t start = (idx == 0) ? 0 : s_seg_end;
    if (start >= s_size) return false;

    if (eos_storage_file_seek(s_fp, start) != EOS_OK) return false;

    char stack_buf[TH_SEG_BYTES];
    ssize_t rd = eos_storage_file_read(s_fp, stack_buf, TH_SEG_BYTES);
    if (rd <= 0) return false;
    uint32_t len = (uint32_t)rd;

    /* keep last whole line unless we reached EOF */
    if (start + len < s_size) {
        int32_t i = (int32_t)len - 1;
        while (i >= 0 && stack_buf[i] != '\n') i--;
        if (i >= 0) {
            len = (uint32_t)i + 1;               /* keep the '\n' */
        } else if (len > TH_SEG_LINE_MAX) {
            len = TH_SEG_LINE_MAX;               /* one giant line: clamp */
        }
        if (len == 0) return false;              /* nothing to show yet  */
    }

    char *txt = (char *)eos_malloc(len + 1);
    if (!txt) return false;
    memcpy(txt, stack_buf, len);
    txt[len] = '\0';

    if (s_text) eos_free(s_text);
    s_text = txt;
    s_seg_idx    = idx;
    s_seg_start  = start;
    s_seg_end    = start + len;
    return true;
}

static void _update_bar(void)
{
    /* filename (marquee handled by LVGL SCROLL_CIRCULAR) */
    if (s_cur_path) {
        lv_label_set_text(s_name, _base(s_cur_path));
    } else {
        lv_label_set_text(s_name, "Texthub");
    }

    /* bottom indicator */
    char buf[40];
    if (s_cur_total > 0) {
        if (s_seg_mode) {
            snprintf(buf, sizeof(buf), "%d/%d | seg %u",
                     s_cur_idx + 1, s_cur_total, (unsigned)(s_seg_idx + 1));
        } else {
            snprintf(buf, sizeof(buf), "%d/%d", s_cur_idx + 1, s_cur_total);
        }
    } else {
        strcpy(buf, "");
    }
    lv_label_set_text(s_bottom, buf);
}

static void _seg_next(void)
{
    if (s_seg_end >= s_size) return;
    uint32_t next = s_seg_idx + 1;
    if (_seg_load(next)) {
        lv_label_set_text(s_body, s_text);
        lv_obj_update_layout(s_card);
        lv_obj_scroll_to_y(s_card, 0, LV_ANIM_OFF);
        _update_bar();
    }
}

static void _seg_prev(void)
{
    if (s_seg_idx == 0) return;
    if (_seg_load(s_seg_idx - 1)) {
        lv_label_set_text(s_body, s_text);
        lv_obj_update_layout(s_card);
        lv_obj_scroll_to_y(s_card, LV_COORD_MAX, LV_ANIM_OFF);
        _update_bar();
    }
}

/* ------------------------------------------------------------------ */
/* Same-directory text file listing (for idx/total and [>] next)      */
/* ------------------------------------------------------------------ */

/* Returns an eos_malloc'd array of eos_strdup'd names (sorted), or NULL. */
static char **_scan_text_names(const char *dir, int *out_count, const char *cur_path, int *out_idx)
{
    *out_count = 0;
    if (out_idx) *out_idx = -1;

    eos_dir_t d = eos_storage_dir_open(dir);
    if (!d) return NULL;

    char **names = NULL;
    int cap = 0, cnt = 0;
    char name[EOS_FS_NAME_MAX];

    while (eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK) {
        if (name[0] == '\0' || name[0] == '.' || !_is_text_ext(name)) continue;
        if (cnt >= TH_FM_MAX_CHILD) break;
        if (cnt >= cap) {
            int ncap = cap ? cap * 2 : 16;
            char **nn = (char **)eos_realloc(names, (size_t)ncap * sizeof(char *));
            if (!nn) break;
            names = nn;
            cap = ncap;
        }
        names[cnt++] = eos_strdup(name);
    }
    eos_storage_dir_close(d);

    if (cnt == 0) {
        eos_free(names);
        return NULL;
    }
    qsort(names, cnt, sizeof(char *), _cmp_name);

    if (out_idx && cur_path) {
        const char *b = _base(cur_path);
        for (int i = 0; i < cnt; i++) {
            if (strcmp(names[i], b) == 0) { *out_idx = i; break; }
        }
    }
    *out_count = cnt;
    return names;
}

static void _free_names(char **names, int count)
{
    for (int i = 0; i < count; i++) eos_free(names[i]);
    eos_free(names);
}

/* ------------------------------------------------------------------ */
/* Open a file (full path)                                            */
/* ------------------------------------------------------------------ */

static void _show_centered_msg(const char *txt)
{
    if (!s_body) return;
    lv_label_set_text(s_body, txt);
    lv_obj_update_layout(s_card);
    lv_obj_scroll_to_y(s_card, 0, LV_ANIM_OFF);
}

static void _write_history(const char *path)
{
    eos_storage_mkdir_recursive(TH_HIST_DIR);
    eos_storage_write_file(TH_HIST_FILE, path, strlen(path));
}

static bool _open_path(const char *path)
{
    if (!path || !_is_text_ext(path)) return false;

    eos_file_t fp = eos_storage_file_open_read(path);
    if (fp == EOS_FILE_INVALID) {
        EOS_LOG_W("Texthub: open fail %s", path);
        _show_centered_msg("OPEN FAIL");
        return false;
    }

    uint32_t size = 0;
    if (eos_storage_file_size(fp, &size) != EOS_OK) {
        eos_storage_file_close(fp);
        _show_centered_msg("SIZE FAIL");
        return false;
    }
    if (size > TH_MAX_FILE_SIZE) {
        eos_storage_file_close(fp);
        _show_centered_msg("FILE TOO BIG");
        return false;
    }

    /* release previous file */
    _free_text();
    if (s_cur_path) { eos_free(s_cur_path); s_cur_path = NULL; }
    if (s_cur_dir)  { eos_free(s_cur_dir);  s_cur_dir  = NULL; }

    s_fp  = fp;
    s_size = size;

    if (size <= TH_READ_ONCE_MAX) {
        s_seg_mode = false;
        char *txt = (char *)eos_malloc(size + 1);
        if (!txt) {
            _free_text();
            _show_centered_msg("NO MEM");
            return false;
        }
        ssize_t rd = eos_storage_file_read(fp, txt, size);
        if (rd < 0) rd = 0;
        txt[rd] = '\0';
        s_text = txt;
    } else {
        s_seg_mode = true;
        if (!_seg_load(0)) {
            _free_text();
            _show_centered_msg("READ FAIL");
            return false;
        }
    }

    s_cur_path = eos_strdup(path);
    char dirbuf[EOS_FS_PATH_MAX];
    _dir_of(path, dirbuf, sizeof(dirbuf));
    s_cur_dir = eos_strdup(dirbuf);

    /* index / total within the same directory */
    int count = 0, idx = -1;
    char **names = _scan_text_names(s_cur_dir, &count, path, &idx);
    _free_names(names, count);
    s_cur_idx   = (idx >= 0) ? idx : 0;
    s_cur_total = count;

    /* render */
    lv_label_set_text(s_body, s_text);
    lv_obj_update_layout(s_card);
    lv_obj_scroll_to_y(s_card, 0, LV_ANIM_OFF);
    _update_bar();

    _write_history(path);
    EOS_LOG_I("Texthub: opened %s (%u bytes%s)", path, size,
              s_seg_mode ? ", seg-mode" : "");
    return true;
}

/* [>] next file in the same directory */
static void _next_file(void)
{
    if (!s_cur_path || !s_cur_dir) return;

    int count = 0, idx = -1;
    char **names = _scan_text_names(s_cur_dir, &count, s_cur_path, &idx);
    if (!names || idx < 0 || idx >= count - 1) {
        _free_names(names, count);
        return;
    }
    char next_path[EOS_FS_PATH_MAX];
    snprintf(next_path, sizeof(next_path), "%s/%s", s_cur_dir, names[idx + 1]);
    _free_names(names, count);

    _open_path(next_path);
}

/* ------------------------------------------------------------------ */
/* File Manager overlay (lazy tree)                                   */
/* ------------------------------------------------------------------ */

/* Free all descendants of n (recursively freeing each children array), but
 * NOT n itself. n->children is ONE malloc'd array of th_node_t structs; the
 * elements are not individually allocated, they are freed with the array. */
static void _fm_children_free(th_node_t *n)
{
    if (!n) return;
    if (n->children) {
        for (int i = 0; i < n->child_count; i++) {
            _fm_children_free(&n->children[i]);
        }
        eos_free(n->children);
        n->children = NULL;
    }
    n->child_count = 0;
    n->loaded = false;
    n->expanded = false;
}

static void _fm_node_unload(th_node_t *n)
{
    if (!n) return;
    _fm_children_free(n);   /* free the subtree, keep n itself */
}

/* Lazy load: read one directory into an array of children nodes. */
static bool _fm_node_load(th_node_t *n)
{
    if (n->loaded) return true;
    if (n->depth >= TH_FM_DEPTH_MAX) { n->loaded = true; return false; }

    /* pass 1: count entries */
    eos_dir_t d = eos_storage_dir_open(n->path);
    if (!d) {
        n->loaded = true;
        EOS_LOG_W("Texthub: opendir FAIL %s", n->path);
        return false;
    }
    int cnt = 0;
    char name[EOS_FS_NAME_MAX];
    while (eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK) {
        if (name[0] == '\0' || name[0] == '.') continue;
        if (++cnt > TH_FM_MAX_CHILD) { cnt = TH_FM_MAX_CHILD; break; }
    }
    eos_storage_dir_close(d);
    EOS_LOG_I("Texthub: dir %s raw=%d", n->path, cnt);

    if (cnt == 0) { n->loaded = true; return false; }

    th_node_t *arr = (th_node_t *)eos_malloc_zeroed((size_t)cnt * sizeof(th_node_t));
    if (!arr) { n->loaded = true; return false; }

    /* pass 2: fill */
    d = eos_storage_dir_open(n->path);
    if (!d) { eos_free(arr); n->loaded = true; return false; }
    int i = 0;
    while (i < cnt && eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK) {
        if (name[0] == '\0' || name[0] == '.') continue;
        char full[EOS_FS_PATH_MAX];
        size_t dl = strlen(n->path), nl = strlen(name);
        if (dl + 1 + nl + 1 > sizeof(full)) continue;
        memcpy(full, n->path, dl);
        full[dl] = '/';
        strcpy(full + dl + 1, name);

        bool is_dir = eos_storage_is_dir(full);
        if (!is_dir && !_is_text_ext(name)) continue; /* only text files */

        th_node_t *c = &arr[i];
        strncpy(c->name, name, sizeof(c->name) - 1);
        c->name[sizeof(c->name) - 1] = '\0';
        strncpy(c->path, full, sizeof(c->path) - 1);
        c->path[sizeof(c->path) - 1] = '\0';
        c->is_dir = is_dir;
        c->depth  = n->depth + 1;
        i++;
        if (i >= TH_FM_MAX_CHILD) break;
    }
    eos_storage_dir_close(d);

    n->children = arr;
    n->child_count = i;
    n->loaded = true;
    EOS_LOG_I("Texthub: dir %s children=%d", n->path, i);
    return i > 0;
}

static void _fm_build_items(th_node_t *n, int depth, int *cnt)
{
    if (*cnt >= TH_FM_MAX_VISIBLE) return;
    if (!n) return;
    s_fm_items[*cnt].node = n;
    s_fm_items[*cnt].depth = depth;
    (*cnt)++;

    /* only directories recurse (files are leaves: click opens them) */
    if (n->is_dir && n->expanded && n->children) {
        for (int i = 0; i < n->child_count && *cnt < TH_FM_MAX_VISIBLE; i++) {
            _fm_build_items(&n->children[i], depth + 1, cnt);
        }
    }
}

static void _fm_row_cb(lv_event_t *e);  /* fwd decl */

static void _fm_rebuild(void)
{
    if (!s_fm_open || !s_fm_list) {
        EOS_LOG_W("Texthub: rebuild skip open=%d list=%p", s_fm_open, (void *)s_fm_list);
        return;
    }

    /* clear old rows */
    lv_obj_clean(s_fm_list);

    if (!s_fm_root) {
        EOS_LOG_E("Texthub: rebuild root NULL");
        return;
    }
    if (!s_fm_root->loaded) _fm_node_load(s_fm_root);

    if (!s_fm_items) {
        s_fm_items = (th_item_t *)eos_malloc(TH_FM_MAX_VISIBLE * sizeof(th_item_t));
        if (!s_fm_items) {
            EOS_LOG_E("Texthub: items alloc FAIL size=%u",
                      (unsigned)(TH_FM_MAX_VISIBLE * sizeof(th_item_t)));
            return;
        }
        EOS_LOG_I("Texthub: items alloc OK");
    }
    int cnt = 0;
    _fm_build_items(s_fm_root, 0, &cnt);
    s_fm_item_count = cnt;
    EOS_LOG_I("Texthub: rebuild items=%d", cnt);

    for (int i = 0; i < cnt; i++) {
        th_node_t *node = s_fm_items[i].node;
        int depth = s_fm_items[i].depth;

        lv_obj_t *row = lv_button_create(s_fm_list);
        lv_obj_remove_style_all(row);
        /* NOTE: use constant width, NOT lv_obj_get_width(s_fm_list) -- right
         * after creation the list has not been laid out yet and get_width
         * returns 0, which makes the row width negative and it never draws. */
        lv_obj_set_size(row, 196, TH_FM_ROW_H);
        /* rows must not be scroll targets or they swallow the drag gesture
         * and the parent list can never scroll */
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(_UI_TEXT), 0);
        lv_obj_set_style_bg_opa(row, i % 2 ? 12 : 0, 0);   /* faint zebra */
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_pos(row, 2, i * TH_FM_ROW_H);

        char rowbuf[EOS_FS_NAME_MAX + 4];
        if (node->is_dir) {
            snprintf(rowbuf, sizeof(rowbuf), "%s/", node->name);
        } else {
            snprintf(rowbuf, sizeof(rowbuf), "%s", node->name);
        }

        lv_obj_t *lbl = lv_label_create(row);
        eos_label_set_font_size(lbl, EOS_FONT_SIZE_TINY);
        lv_obj_set_style_text_color(lbl, node->is_dir ? lv_color_hex(_UI_ACCENT)
                                                      : lv_color_hex(_UI_TEXT), 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);
        lv_obj_set_width(lbl, 148);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8 + depth * 10, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(lbl, rowbuf);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);

        if (node->is_dir) {
            lv_obj_t *arr = lv_label_create(row);
            lv_obj_set_style_text_font(arr, &EOS_FONT_ICON, 0);
            lv_obj_set_style_text_color(arr, lv_color_hex(_UI_DIM), 0);
            lv_label_set_text(arr, node->expanded ? RI_ARROW_DOWN_S_LINE
                                                  : RI_ARROW_RIGHT_S_LINE);
            lv_obj_align(arr, LV_ALIGN_RIGHT_MID, -8, 0);
            lv_obj_remove_flag(arr, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_add_event_cb(row, _fm_row_cb, LV_EVENT_CLICKED, node);
    }
    EOS_LOG_I("Texthub: rows rendered=%d", cnt);
}

static void _fm_close(void)
{
    if (!s_fm_open) return;
    s_fm_open = false;

    if (s_fm_items) { eos_free(s_fm_items); s_fm_items = NULL; }
    s_fm_item_count = 0;
    if (s_fm_root) {
        _fm_children_free(s_fm_root);
        eos_free(s_fm_root);   /* root itself is a single malloc'd node */
        s_fm_root = NULL;
    }

    if (s_fm_mask) { lv_obj_delete(s_fm_mask); s_fm_mask = NULL; }
    s_fm_panel = NULL;
    s_fm_list  = NULL;
    s_fm_path  = NULL;
}

static void _fm_row_cb(lv_event_t *e)
{
    th_node_t *node = (th_node_t *)lv_event_get_user_data(e);
    if (!node) return;
    EOS_LOG_I("Texthub: click %s is_dir=%d expanded=%d", node->name,
              node->is_dir, node->expanded);

    if (node->is_dir) {
        if (node->expanded) {
            node->expanded = false;
            _fm_node_unload(node);
        } else {
            if (!node->loaded) _fm_node_load(node);
            node->expanded = true;
        }
        _fm_rebuild();
    } else {
        char path[EOS_FS_PATH_MAX];
        strncpy(path, node->path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        _fm_close();
        _open_path(path);
    }
}

static void _fm_close_cb(lv_event_t *e)
{
    (void)e;
    _fm_close();
}

static void _fm_open(void)
{
    if (s_fm_open) return;
    s_fm_open = true;
    EOS_LOG_I("Texthub: fm open");

    /* mask: full-screen dim + tap-to-close */
    s_fm_mask = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_fm_mask);
    lv_obj_set_size(s_fm_mask, 240, 240);
    lv_obj_set_pos(s_fm_mask, 0, 0);
    lv_obj_set_style_bg_color(s_fm_mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_fm_mask, 160, 0);
    lv_obj_set_style_radius(s_fm_mask, 0, 0);
    lv_obj_clear_flag(s_fm_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_fm_mask, _fm_close_cb, LV_EVENT_CLICKED, NULL);

    /* panel */
    s_fm_panel = lv_obj_create(s_fm_mask);
    lv_obj_remove_style_all(s_fm_panel);
    lv_obj_set_size(s_fm_panel, 212, 198);
    lv_obj_set_pos(s_fm_panel, 14, 30);
    lv_obj_set_style_bg_color(s_fm_panel, lv_color_hex(_UI_PANEL), 0);
    lv_obj_set_style_bg_opa(s_fm_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_fm_panel, 14, 0);
    lv_obj_set_style_border_width(s_fm_panel, 0, 0);
    lv_obj_clear_flag(s_fm_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* panel sits above the mask; no event bubbling to mask by default */

    /* header: path + close */
    s_fm_path = lv_label_create(s_fm_panel);
    eos_label_set_font_size(s_fm_path, EOS_FONT_SIZE_MICRO);
    lv_obj_set_style_text_color(s_fm_path, lv_color_hex(_UI_DIM), 0);
    lv_obj_set_size(s_fm_path, 140, 18);
    lv_obj_set_pos(s_fm_path, 14, 10);
    lv_label_set_long_mode(s_fm_path, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(s_fm_path, TH_DIR);
    lv_obj_remove_flag(s_fm_path, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_fm_path, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *close = lv_button_create(s_fm_panel);
    lv_obj_remove_style_all(close);
    lv_obj_set_size(close, 26, 22);
    lv_obj_set_pos(close, 176, 8);
    lv_obj_set_style_bg_color(close, lv_color_hex(_UI_TEXT), 0);
    lv_obj_set_style_bg_opa(close, 18, 0);
    lv_obj_set_style_radius(close, 8, 0);
    lv_obj_add_event_cb(close, _fm_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close);
    lv_obj_set_style_text_font(cl, &EOS_FONT_ICON, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(_UI_TEXT), 0);
    lv_label_set_text(cl, RI_CLOSE_FILL);
    lv_obj_center(cl);
    lv_obj_remove_flag(cl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(cl, LV_OBJ_FLAG_SCROLLABLE);

    /* list */
    s_fm_list = lv_obj_create(s_fm_panel);
    lv_obj_remove_style_all(s_fm_list);
    lv_obj_set_size(s_fm_list, 200, 152);
    lv_obj_set_pos(s_fm_list, 6, 38);
    lv_obj_set_style_radius(s_fm_list, 10, 0);
    lv_obj_set_style_pad_all(s_fm_list, 0, 0);
    lv_obj_set_style_border_width(s_fm_list, 0, 0);
    lv_obj_add_flag(s_fm_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_fm_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_fm_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s_fm_list, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_bg_color(s_fm_list, lv_color_hex(_UI_DIM), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_fm_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_fm_list, 4, LV_PART_SCROLLBAR);

    /* root node */
    s_fm_root = (th_node_t *)eos_malloc_zeroed(sizeof(th_node_t));
    if (s_fm_root) {
        strncpy(s_fm_root->name, "texthub", sizeof(s_fm_root->name) - 1);
        strncpy(s_fm_root->path, TH_DIR, sizeof(s_fm_root->path) - 1);
        s_fm_root->is_dir = true;
        s_fm_root->expanded = true;    /* show root level immediately */
        _fm_node_load(s_fm_root);      /* load root lazily now */
    } else {
        EOS_LOG_E("Texthub: root alloc FAIL size=%u", (unsigned)sizeof(th_node_t));
    }

    _fm_rebuild();
}

/* ------------------------------------------------------------------ */
/* UI                                                                 */
/* ------------------------------------------------------------------ */

static lv_obj_t *_make_bar_btn(lv_obj_t *parent, int x, int w,
                               lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, TH_BAR_H);
    lv_obj_set_pos(btn, x, TH_BAR_Y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(_UI_TEXT), 0);
    lv_obj_set_style_bg_opa(btn, 22, 0);
    lv_obj_set_style_radius(btn, 13, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    eos_label_set_font_size(lbl, EOS_FONT_SIZE_MICRO);
    lv_obj_set_style_text_color(lbl, lv_color_hex(_UI_TEXT), 0);
    lv_obj_center(lbl);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
    return btn;
}

static void _card_scroll_cb(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_target(e);
    if (!s_seg_mode || s_fp == EOS_FILE_INVALID) return;

    int32_t sy = lv_obj_get_scroll_y(card);
    int32_t bottom = lv_obj_get_scroll_bottom(card);
    if (bottom <= 0) return;

    if (sy >= bottom - 6) {
        _seg_next();
    } else if (sy <= 0) {
        _seg_prev();
    }
}

static void _fm_btn_cb(lv_event_t *e)  { (void)e; _fm_open(); }
static void _next_btn_cb(lv_event_t *e) { (void)e; _next_file(); }

static void _build_ui(eos_activity_t *act)
{
    s_root = eos_activity_get_view(act);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(_UI_BG), 0);
    eos_round_clip(s_root);

    /* app bar: [FM] [filename clip] [>] */
    s_fm_btn = _make_bar_btn(s_root, 8, TH_BTN_W, _fm_btn_cb, NULL);
    lv_obj_t *fml = lv_obj_get_child(s_fm_btn, 0);
    lv_label_set_text(fml, "FM");

    s_name_clip = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_name_clip);
    lv_obj_set_size(s_name_clip, TH_NAME_W, TH_BAR_H);
    lv_obj_set_pos(s_name_clip, 8 + TH_BTN_W + 6, TH_BAR_Y);
    lv_obj_set_style_bg_color(s_name_clip, lv_color_hex(_UI_TEXT), 0);
    lv_obj_set_style_bg_opa(s_name_clip, 10, 0);
    lv_obj_set_style_radius(s_name_clip, 13, 0);
    lv_obj_set_style_border_width(s_name_clip, 0, 0);
    lv_obj_clear_flag(s_name_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_name_clip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(s_name_clip, LV_SCROLLBAR_MODE_OFF);

    s_name = lv_label_create(s_name_clip);
    eos_label_set_font_size(s_name, EOS_FONT_SIZE_MICRO);
    lv_obj_set_style_text_color(s_name, lv_color_hex(_UI_TEXT), 0);
    lv_obj_set_style_text_opa(s_name, LV_OPA_COVER, 0);
    lv_obj_set_width(s_name, TH_NAME_W);
    lv_obj_set_pos(s_name, 0, 0);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(s_name, "Texthub");
    lv_obj_remove_flag(s_name, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_name, LV_OBJ_FLAG_SCROLLABLE);

    s_next_btn = _make_bar_btn(s_root, 240 - 8 - TH_BTN_W, TH_BTN_W, _next_btn_cb, NULL);
    lv_obj_t *nx = lv_obj_get_child(s_next_btn, 0);
    lv_obj_set_style_text_font(nx, &EOS_FONT_ICON, 0);
    lv_label_set_text(nx, RI_ARROW_RIGHT_S_LINE);

    /* text card */
    s_card = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_card);
    lv_obj_set_size(s_card, 212, 166);
    lv_obj_set_pos(s_card, 14, 60);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(_UI_CARD), 0);
    lv_obj_set_style_bg_opa(s_card, _UI_CARD_OPA, 0);
    lv_obj_set_style_radius(s_card, 12, 0);
    lv_obj_set_style_border_width(s_card, 0, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_add_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_card, LV_DIR_VER);
    lv_obj_add_event_cb(s_card, _card_scroll_cb, LV_EVENT_SCROLL_END, NULL);

    s_body = lv_label_create(s_card);
    eos_label_set_font_size(s_body, EOS_FONT_SIZE_MICRO);
    lv_obj_set_style_text_color(s_body, lv_color_hex(_UI_TEXT), 0);
    lv_obj_set_style_text_opa(s_body, 235, 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(s_body, 188);
    lv_obj_set_pos(s_body, 12, 8);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(s_body, "");
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);

    /* bottom indicator */
    s_bottom = lv_label_create(s_root);
    eos_label_set_font_size(s_bottom, EOS_FONT_SIZE_TINY);
    lv_obj_set_style_text_color(s_bottom, lv_color_hex(_UI_DIM), 0);
    lv_obj_set_style_text_opa(s_bottom, 200, 0);
    lv_obj_set_style_text_align(s_bottom, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(s_bottom, 96, 12);
    lv_obj_set_pos(s_bottom, 72, 229);
    lv_label_set_long_mode(s_bottom, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(s_bottom, "");
    lv_obj_remove_flag(s_bottom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_bottom, LV_OBJ_FLAG_SCROLLABLE);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

static bool _on_swipe_back(eos_activity_t *self, lv_dir_t dir)
{
    (void)self;
    (void)dir;
    if (s_fm_open) { _fm_close(); return true; }
    return false;
}

static void _on_enter(eos_activity_t *act)
{
    s_act = act;
    _build_ui(act);

    /* try history first */
    if (eos_storage_is_file(TH_HIST_FILE)) {
        char *last = eos_storage_read_file(TH_HIST_FILE);
        if (last) {
            size_t len = strlen(last);
            while (len > 0 && (last[len - 1] == '\n' || last[len - 1] == '\r')) {
                last[--len] = '\0';
            }
            if (len > 1 && strncmp(last, TH_DIR "/", strlen(TH_DIR) + 1) == 0) {
                if (eos_storage_is_file(last) && _open_path(last)) {
                    eos_free(last);
                    return;
                }
            }
            eos_free(last);
        }
    }
    /* no valid record -> File Manager */
    EOS_LOG_I("Texthub: no valid history, open FM");
    if (s_cur_path) {
        _free_text();
        eos_free(s_cur_path); s_cur_path = NULL;
        eos_free(s_cur_dir);  s_cur_dir  = NULL;
        s_cur_idx = s_cur_total = 0;
        lv_label_set_text(s_name, "Texthub");
        lv_label_set_text(s_bottom, "");
    }
    _show_centered_msg("NO FILES\n[FM] to browse");
    _fm_open();
}

static void _on_destroy(eos_activity_t *act)
{
    (void)act;
    _fm_close();
    _free_text();
    if (s_cur_path) { eos_free(s_cur_path); s_cur_path = NULL; }
    if (s_cur_dir)  { eos_free(s_cur_dir);  s_cur_dir  = NULL; }
    s_cur_idx = s_cur_total = 0;
}

static const eos_activity_lifecycle_t s_lifecycle = {
    .on_enter      = _on_enter,
    .on_destroy    = _on_destroy,
    .on_swipe_back = _on_swipe_back,
};

void eos_texthub_enter(void)
{
    EOS_LOG_I("Texthub: enter");
    eos_activity_t *act = eos_activity_create(&s_lifecycle);
    eos_activity_set_type(act, EOS_ACTIVITY_TYPE_APP);
    eos_activity_set_app_header_visible(act, false);
    eos_activity_set_title(act, "Texthub");
    eos_activity_enter(act);
}
