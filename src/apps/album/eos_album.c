/**
 * @file eos_album.c
 * @brief Album - native C photo viewer with a lazy file manager
 *
 * - On enter: reads /sdcard/history/album/history.txt; if it holds a valid
 *   image path, that image opens directly. Otherwise the file manager opens.
 * - File manager (like TextHub FM): rooted at /sdcard/album/, lazy-loads the
 *   directory tree on demand, lists only directories and images (.jpg/.jpeg/
 *   .png), no thumbnails. Tap a folder to expand/collapse, tap an image to
 *   open it. Wide rows (36px) for comfortable reading.
 * - Viewer: prev/next arrows, tap to zoom, drag to pan, trash to delete,
 *   counter bottom-left, FM button bottom-right. English only.
 *
 * - JPEG: decoded to RGB565 with the bundled TJpgDec (streamed from SD,
 *         output capped at ALBUM_DECODE_MAX per side to stay OOM-safe).
 * - PNG: decoded by LVGL (LodePNG) from path, dimension pre-checked.
 */

#include "eos_album.h"

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
#include "ui/system/eos_round_clip.h" /* eos_round_clip() */
#include "lvgl.h"

#include "tjpgd/tjpgd.h" /* bundled TJpgDec (RGB565, scale 1/2/4/8) */

#define EOS_LOG_TAG "Album"

/* ------------------------------------------------------------------ */
/* Config                                                             */
/* ------------------------------------------------------------------ */

#define ALBUM_DIR          "/sdcard/album/"
#define ALBUM_HIST_DIR     "/sdcard/history/album/"
#define ALBUM_HIST_FILE    "/sdcard/history/album/history.txt"
#define ALBUM_MAX          128           /* max images in the viewer list     */
#define ALBUM_IMG_MAX      150           /* fit box (px) for the image        */
#define ALBUM_DECODE_MAX   1024          /* JPEG decode cap per side (px)     */
#define ALBUM_PNG_MAX      1024          /* PNG side cap (LVGL decodes full)  */

/* File manager (lazy tree) */
#define FM_MAX_CHILD       256           /* max entries read per directory     */
#define FM_MAX_VISIBLE     120           /* max rows rendered at once          */
#define FM_ROW_H           36            /* row height (spacious)              */
#define FM_DEPTH_MAX       6             /* max tree depth                     */

/* Zoom levels (zoom=1.0 means fit-to-box). Used for the magnifier. */
static const uint32_t ALBUM_ZOOM_LEVELS[] = {256, 384, 512, 768}; /* 1.0 / 1.5 / 2.0 / 3.0 (Q16) */
#define ALBUM_ZOOM_N (sizeof(ALBUM_ZOOM_LEVELS) / sizeof(ALBUM_ZOOM_LEVELS[0]))
#define ALBUM_ZOOM_IDX_DEF 0
#define ALBUM_NAME_W       132           /* scrolling name label width      */
#define ALBUM_WORK_SIZE    8192          /* TJpgDec working pool (PSRAM)    */

#define _UI_BG        0x12121A
#define _UI_BTN_BG    0x1E1E2E
#define _UI_BTN_TRASH 0xB03A2E
#define _UI_PANEL     0x1A1A24
#define _UI_TEXT      0xFFFFFF
#define _UI_DIM       0x8A8AA0
#define _UI_ACCENT    0x6FA8FF

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    eos_file_t fp;
    uint8_t   *out;
    uint16_t   out_w;
} _dec_ctx_t;

static eos_activity_t *s_act;
static lv_obj_t *s_root, *s_img, *s_name, *s_counter, *s_msg;
static lv_obj_t *s_prev_btn, *s_next_btn, *s_trash_btn, *s_fm_btn;

/* magnifier state (zoom > 1.0 lets the image overflow -> pan via joystick) */
static int  s_zoom_idx = ALBUM_ZOOM_IDX_DEF;
static int  s_pan_x = 0, s_pan_y = 0;   /* image offset from center, px */
static int  s_img_w = 0, s_img_h = 0;   /* current image decoded size */

static char (*s_paths)[EOS_FS_PATH_MAX]; /* viewer list of full paths (PSRAM) */
static int   s_count;
static int   s_index;

static uint8_t       *s_pixels;          /* current JPEG decode buffer */
static lv_image_dsc_t s_dsc;

/* File manager node (lazy-loaded tree, children are one array) */
typedef struct _fm_node_t
{
    char name[EOS_FS_NAME_MAX];
    char path[EOS_FS_PATH_MAX];
    bool is_dir;
    bool loaded;
    bool expanded;
    int  depth;
    int  child_count;
    struct _fm_node_t *children; /* malloc'd array, freed with subtree */
} _fm_node_t;

typedef struct
{
    _fm_node_t *node;
    int         depth;
} _fm_item_t;

static bool s_fm_open;
static lv_obj_t *s_fm_mask, *s_fm_panel, *s_fm_title, *s_fm_list;
static _fm_node_t *s_fm_root;
static _fm_item_t *s_fm_items;
static int s_fm_item_count;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static const char *_base(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static bool _is_image_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot[1] == '\0') return false;
    const char *e = dot + 1;
    return (strcasecmp(e, "jpg") == 0) || (strcasecmp(e, "jpeg") == 0) ||
           (strcasecmp(e, "png") == 0);
}

static bool _is_jpeg_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot[1] == '\0') return false;
    const char *e = dot + 1;
    return (strcasecmp(e, "jpg") == 0) || (strcasecmp(e, "jpeg") == 0);
}

/* Read PNG width/height from IHDR without decoding the whole file. */
static bool _png_dim(const char *path, uint32_t *w, uint32_t *h)
{
    eos_file_t fp = eos_storage_file_open_read(path);
    if (fp == EOS_FILE_INVALID) return false;

    uint8_t buf[24];
    if (eos_storage_file_read(fp, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
        eos_storage_file_close(fp);
        return false;
    }
    eos_storage_file_close(fp);

    if (buf[0] != 0x89 || buf[1] != 'P' || buf[2] != 'N' || buf[3] != 'G') return false;

    *w = ((uint32_t)buf[16] << 24) | ((uint32_t)buf[17] << 16) |
         ((uint32_t)buf[18] << 8) | (uint32_t)buf[19];
    *h = ((uint32_t)buf[20] << 24) | ((uint32_t)buf[21] << 16) |
         ((uint32_t)buf[22] << 8) | (uint32_t)buf[23];
    return (*w > 0 && *h > 0);
}

/* ------------------------------------------------------------------ */
/* History (remember last opened image, like TextHub)                 */
/* ------------------------------------------------------------------ */

static void _write_history(const char *path)
{
    if (!path || !path[0]) return;
    eos_storage_mkdir_recursive(ALBUM_HIST_DIR);
    eos_storage_write_file(ALBUM_HIST_FILE, path, strlen(path));
}

static bool _read_history(char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';

    if (!eos_storage_is_file(ALBUM_HIST_FILE)) return false;

    char *content = eos_storage_read_file(ALBUM_HIST_FILE);
    if (!content) return false;

    /* trim trailing whitespace / newline */
    size_t len = strlen(content);
    while (len > 0 && (content[len - 1] == '\n' || content[len - 1] == '\r' ||
                       content[len - 1] == ' ')) {
        content[--len] = '\0';
    }

    bool valid = (len > 0 && len < out_size &&
                  strncmp(content, ALBUM_DIR, strlen(ALBUM_DIR)) == 0 &&
                  eos_storage_is_file(content));
    if (valid) {
        strncpy(out, content, out_size - 1);
        out[out_size - 1] = '\0';
    }
    eos_free(content);
    return valid;
}

/* ------------------------------------------------------------------ */
/* JPEG decode (TJpgDec, streamed from SD)                            */
/* ------------------------------------------------------------------ */

static size_t _jd_in(JDEC *jd, uint8_t *buf, size_t nd)
{
    _dec_ctx_t *c = (_dec_ctx_t *)jd->device;
    if (!buf) { /* skip nd bytes */
        uint32_t pos = 0;
        if (eos_storage_file_tell(c->fp, &pos) != EOS_OK) {
            EOS_LOG_E("Album: skip tell FAIL");
            return 0;
        }
        if (eos_storage_file_seek(c->fp, pos + nd) != EOS_OK) {
            EOS_LOG_E("Album: skip seek FAIL pos=%u nd=%u", pos, (unsigned)nd);
            return 0;
        }
        return nd;
    }
    ssize_t rd = eos_storage_file_read(c->fp, buf, nd);
    return rd > 0 ? (size_t)rd : 0;
}

static int _jd_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    _dec_ctx_t *c = (_dec_ctx_t *)jd->device;
    if (!c->out) return 0;

    uint16_t bw = rect->right - rect->left + 1;
    uint32_t stride = (uint32_t)c->out_w * 2;
    const uint8_t *src = (const uint8_t *)bitmap;
    uint8_t *dst = c->out + ((uint32_t)rect->top * c->out_w + rect->left) * 2;

    for (uint16_t y = rect->top; y <= rect->bottom; y++) {
        memcpy(dst, src, bw * 2);
        src += bw * 2;
        dst += stride;
    }
    return 1; /* continue */
}

static bool _decode_jpeg(const char *path, lv_image_dsc_t *dsc)
{
    eos_file_t fp = eos_storage_file_open_read(path);
    if (fp == EOS_FILE_INVALID) {
        EOS_LOG_E("Album: open FAIL %s", path);
        return false;
    }
    EOS_LOG_I("Album: decode %s", path);

    /* TJpgDec working pool (huffman tables + MCU buffer), PSRAM to save IRAM */
    uint8_t *work = (uint8_t *)eos_malloc(ALBUM_WORK_SIZE);
    if (!work) {
        EOS_LOG_E("Album: work malloc FAIL size=%u", (unsigned)ALBUM_WORK_SIZE);
        eos_storage_file_close(fp);
        return false;
    }

    _dec_ctx_t ctx;
    ctx.fp    = fp;
    ctx.out   = NULL;
    ctx.out_w = 0;

    JDEC jd;
    JRESULT rc = jd_prepare(&jd, _jd_in, work, ALBUM_WORK_SIZE, &ctx);
    if (rc != JDR_OK) {
        EOS_LOG_E("Album: jd_prepare FAIL rc=%d", (int)rc);
        eos_free(work);
        eos_storage_file_close(fp);
        return false;
    }

    uint16_t w = jd.width, h = jd.height;
    /* TJpgDec scale = right shift count: 0=full, 1=1/2, 2=1/4, 3=1/8 (max) */
    uint8_t scale = 0;
    uint16_t ow = w, oh = h;
    while (scale < 3 && (ow > ALBUM_DECODE_MAX || oh > ALBUM_DECODE_MAX)) {
        scale++;
        ow >>= 1;
        oh >>= 1;
    }
    if (ow == 0 || oh == 0 || ow > ALBUM_DECODE_MAX || oh > ALBUM_DECODE_MAX) {
        EOS_LOG_E("Album: jpeg %ux%u too large even at 1/8", w, h);
        eos_free(work);
        eos_storage_file_close(fp);
        return false; /* too large even at 1/8 */
    }

    uint32_t npix = (uint32_t)ow * oh * 2;
    uint8_t *pix = (uint8_t *)eos_malloc(npix);
    if (!pix) {
        EOS_LOG_E("Album: pix malloc FAIL size=%u", (unsigned)npix);
        eos_free(work);
        eos_storage_file_close(fp);
        return false;
    }

    ctx.out   = pix;
    ctx.out_w = ow;

    EOS_LOG_I("Album: jpeg %ux%u scale=%u -> %ux%u", w, h, scale, ow, oh);

    JRESULT drc = jd_decomp(&jd, _jd_out, scale);
    if (drc != JDR_OK) {
        EOS_LOG_E("Album: jd_decomp FAIL rc=%d w=%u h=%u scale=%u", (int)drc, w, h, scale);
        eos_free(pix);
        eos_free(work);
        eos_storage_file_close(fp);
        return false;
    }

    eos_free(work);
    eos_storage_file_close(fp);

    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = LV_COLOR_FORMAT_RGB565;
    dsc->header.w      = ow;
    dsc->header.h      = oh;
    dsc->header.stride = ow * 2;
    dsc->data          = pix;
    dsc->data_size     = npix;
    return true;
}

/* ------------------------------------------------------------------ */
/* Viewer list: flat scan of ONE directory (no recursion)             */
/* ------------------------------------------------------------------ */

static int _cmp_path(const void *a, const void *b)
{
    const char *pa = _base((const char *)a);
    const char *pb = _base((const char *)b);
    return strcasecmp(pa, pb);
}

static int _load_dir_pics(const char *dir)
{
    if (s_paths) {
        eos_free(s_paths);
        s_paths = NULL;
    }
    s_count = 0;
    s_index = 0;

    if (!eos_storage_is_dir(dir)) return 0;

    s_paths = (char (*)[EOS_FS_PATH_MAX])eos_malloc(ALBUM_MAX * EOS_FS_PATH_MAX);
    if (!s_paths) return 0;

    eos_dir_t d = eos_storage_dir_open(dir);
    if (!d) return 0;

    char name[EOS_FS_NAME_MAX];
    while (s_count < ALBUM_MAX &&
           eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK) {
        if (name[0] == '.') continue; /* skip dot entries */
        char full[EOS_FS_PATH_MAX];
        size_t dl = strlen(dir), nl = strlen(name);
        if (dl + 1 + nl + 1 > sizeof(full)) continue; /* path too long */
        memcpy(full, dir, dl);
        full[dl] = '/';
        strcpy(full + dl + 1, name);
        if (eos_storage_is_file(full) && _is_image_ext(name)) {
            strncpy(s_paths[s_count], full, EOS_FS_PATH_MAX - 1);
            s_paths[s_count][EOS_FS_PATH_MAX - 1] = '\0';
            s_count++;
        }
    }
    eos_storage_dir_close(d);

    if (s_count > 1) {
        qsort(s_paths, s_count, EOS_FS_PATH_MAX, _cmp_path);
    }
    return s_count;
}

static void _show_current(void);

/* Open a specific image: switch to its directory, locate it, show it. */
static bool _open_image_path(const char *path)
{
    if (!path || !path[0]) return false;

    /* derive directory from the path */
    char dir[EOS_FS_PATH_MAX];
    const char *slash = strrchr(path, '/');
    if (!slash) return false;
    size_t dl = (size_t)(slash - path);
    if (dl == 0) return false;
    if (dl >= sizeof(dir)) return false;
    memcpy(dir, path, dl);
    dir[dl] = '\0';

    if (_load_dir_pics(dir) <= 0) {
        return false;
    }

    /* locate index of the opened path */
    s_index = 0;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_paths[i], path) == 0) {
            s_index = i;
            break;
        }
    }
    _show_current();
    return true;
}

/* ------------------------------------------------------------------ */
/* Display                                                            */
/* ------------------------------------------------------------------ */

static uint32_t _fit_scale(uint32_t w, uint32_t h)
{
    uint32_t sx = (ALBUM_IMG_MAX * 256) / (w ? w : 1);
    uint32_t sy = (ALBUM_IMG_MAX * 256) / (h ? h : 1);
    uint32_t s = sx < sy ? sx : sy;
    if (s < 1) s = 1;
    if (s > 1024) s = 1024;
    return s;
}

/* Apply current zoom + pan to the image object. zoom=1.0 keeps it centered
 * (fit). zoom>1.0 enlarges and lets the image overflow the round clip, so the
 * user pans with the joystick. */
static void _apply_view(void)
{
    if (!s_img || s_img_w <= 0 || s_img_h <= 0) return;
    uint32_t base = _fit_scale(s_img_w, s_img_h);
    uint32_t z = ALBUM_ZOOM_LEVELS[s_zoom_idx];
    lv_image_set_scale(s_img, (uint32_t)((uint64_t)base * z / 256));

    int disp_w = (int)((uint64_t)s_img_w * base * z / 256 / 256);
    int disp_h = (int)((uint64_t)s_img_h * base * z / 256 / 256);

    /* clamp pan so the image keeps covering the center (no empty gap) */
    int max_x = (disp_w - ALBUM_IMG_MAX) / 2;
    int max_y = (disp_h - ALBUM_IMG_MAX) / 2;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (s_pan_x >  max_x) s_pan_x =  max_x;
    if (s_pan_x < -max_x) s_pan_x = -max_x;
    if (s_pan_y >  max_y) s_pan_y =  max_y;
    if (s_pan_y < -max_y) s_pan_y = -max_y;

    /* image is centered via lv_obj_center in _show_current; apply pan offset here */
    lv_obj_set_x(s_img, s_pan_x);
    lv_obj_set_y(s_img, s_pan_y);
}

static void _free_pixels(void)
{
    if (s_pixels) {
        eos_free(s_pixels);
        s_pixels = NULL;
    }
}

static void _show_msg(const char *txt)
{
    if (!s_msg) return;
    lv_label_set_text(s_msg, txt);
    lv_obj_remove_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    if (s_img) lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
}

static void _clear_msg(void)
{
    if (!s_msg) return;
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    if (s_img) lv_obj_remove_flag(s_img, LV_OBJ_FLAG_HIDDEN);
}

static void _show_empty(void)
{
    _free_pixels();
    lv_image_set_src(s_img, NULL);
    lv_label_set_text(s_name, "");
    lv_label_set_text(s_counter, "0/0");
    _show_msg("NO IMAGES\nTap FM to browse");
}

static void _show_index(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", s_index + 1, s_count);
    lv_label_set_text(s_counter, buf);
}

static void _show_name(void)
{
    const char *b = _base(s_paths[s_index]);
    lv_label_set_text(s_name, b);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
}

static void _show_current(void)
{
    if (s_count <= 0) {
        _show_empty();
        return;
    }
    if (s_index < 0) s_index = 0;
    if (s_index >= s_count) s_index = s_count - 1;

    _free_pixels(); /* release previous decode buffer first */
    _clear_msg();

    const char *path = s_paths[s_index];

    if (_is_jpeg_ext(path)) {
        if (_decode_jpeg(path, &s_dsc)) {
            s_pixels = (uint8_t *)s_dsc.data; /* keep alive until next show */
            s_img_w = s_dsc.header.w;
            s_img_h = s_dsc.header.h;
            lv_image_set_src(s_img, &s_dsc);
            lv_image_set_pivot(s_img, s_dsc.header.w / 2, s_dsc.header.h / 2);
            lv_image_set_scale(s_img, _fit_scale(s_dsc.header.w, s_dsc.header.h));
        } else {
            lv_image_set_src(s_img, NULL);
            s_img_w = s_img_h = 0;
            _show_msg("DECODE FAIL");
        }
    } else { /* PNG -> LVGL (LodePNG) */
        uint32_t pw = 0, ph = 0;
        if (_png_dim(path, &pw, &ph)) {
            if (pw > ALBUM_PNG_MAX || ph > ALBUM_PNG_MAX) {
                lv_image_set_src(s_img, NULL);
                s_img_w = s_img_h = 0;
                _show_msg("TOO LARGE");
            } else {
                lv_image_header_t hdr;
                if (lv_image_decoder_get_info(path, &hdr) == LV_RESULT_OK && hdr.w > 0) {
                    s_img_w = hdr.w;
                    s_img_h = hdr.h;
                    lv_image_set_src(s_img, path);
                    lv_image_set_pivot(s_img, hdr.w / 2, hdr.h / 2);
                    lv_image_set_scale(s_img, _fit_scale(hdr.w, hdr.h));
                } else {
                    lv_image_set_src(s_img, NULL);
                    s_img_w = s_img_h = 0;
                    _show_msg("DECODE FAIL");
                }
            }
        } else {
            lv_image_set_src(s_img, NULL);
            s_img_w = s_img_h = 0;
            _show_msg("DECODE FAIL");
        }
    }

    lv_obj_center(s_img);
    s_zoom_idx = ALBUM_ZOOM_IDX_DEF;
    s_pan_x = 0;
    s_pan_y = 0;
    _apply_view();
    _show_name();
    _show_index();

    /* remember last viewed image for next launch */
    _write_history(s_paths[s_index]);
}

/* ------------------------------------------------------------------ */
/* File manager (lazy tree, no thumbnails)                            */
/* ------------------------------------------------------------------ */

static void _fm_children_free(_fm_node_t *n)
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

static int _fm_cmp(const void *a, const void *b)
{
    const _fm_node_t *na = (const _fm_node_t *)a;
    const _fm_node_t *nb = (const _fm_node_t *)b;
    if (na->is_dir != nb->is_dir) return nb->is_dir - na->is_dir;
    return strcasecmp(na->name, nb->name);
}

/* Lazy load: read one directory into an array of children nodes. */
static bool _fm_node_load(_fm_node_t *n)
{
    if (n->loaded) return true;
    if (n->depth >= FM_DEPTH_MAX) { n->loaded = true; return false; }

    /* pass 1: count entries */
    eos_dir_t d = eos_storage_dir_open(n->path);
    if (!d) {
        n->loaded = true;
        EOS_LOG_W("Album: opendir FAIL %s", n->path);
        return false;
    }
    int cnt = 0;
    char name[EOS_FS_NAME_MAX];
    while (eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK) {
        if (name[0] == '\0' || name[0] == '.') continue;
        if (++cnt > FM_MAX_CHILD) { cnt = FM_MAX_CHILD; break; }
    }
    eos_storage_dir_close(d);
    EOS_LOG_I("Album: dir %s raw=%d", n->path, cnt);

    if (cnt == 0) { n->loaded = true; return false; }

    _fm_node_t *arr = (_fm_node_t *)eos_malloc_zeroed((size_t)cnt * sizeof(_fm_node_t));
    if (!arr) { n->loaded = true; return false; }

    /* pass 2: fill, keeping only directories + images */
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
        if (!is_dir && !_is_image_ext(name)) continue; /* only dirs + images */

        _fm_node_t *c = &arr[i];
        strncpy(c->name, name, sizeof(c->name) - 1);
        c->name[sizeof(c->name) - 1] = '\0';
        strncpy(c->path, full, sizeof(c->path) - 1);
        c->path[sizeof(c->path) - 1] = '\0';
        c->is_dir = is_dir;
        c->depth  = n->depth + 1;
        i++;
        if (i >= FM_MAX_CHILD) break;
    }
    eos_storage_dir_close(d);

    n->children = arr;
    n->child_count = i;
    n->loaded = true;
    if (i > 1) {
        qsort(arr, (size_t)i, sizeof(_fm_node_t), _fm_cmp);
    }
    EOS_LOG_I("Album: dir %s children=%d", n->path, i);
    return i > 0;
}

static void _fm_build_items(_fm_node_t *n, int depth, int *cnt)
{
    if (*cnt >= FM_MAX_VISIBLE) return;
    if (!n) return;
    s_fm_items[*cnt].node = n;
    s_fm_items[*cnt].depth = depth;
    (*cnt)++;

    /* only directories recurse (images are leaves: click opens them) */
    if (n->is_dir && n->expanded && n->children) {
        for (int i = 0; i < n->child_count && *cnt < FM_MAX_VISIBLE; i++) {
            _fm_build_items(&n->children[i], depth + 1, cnt);
        }
    }
}

static void _fm_row_cb(lv_event_t *e); /* fwd decl */

static void _fm_rebuild(void)
{
    if (!s_fm_open || !s_fm_list) {
        EOS_LOG_W("Album: rebuild skip open=%d list=%p", s_fm_open, (void *)s_fm_list);
        return;
    }

    /* clear old rows */
    lv_obj_clean(s_fm_list);

    if (!s_fm_root) {
        EOS_LOG_E("Album: rebuild root NULL");
        return;
    }
    if (!s_fm_root->loaded) _fm_node_load(s_fm_root);

    if (!s_fm_items) {
        s_fm_items = (_fm_item_t *)eos_malloc(FM_MAX_VISIBLE * sizeof(_fm_item_t));
        if (!s_fm_items) {
            EOS_LOG_E("Album: items alloc FAIL size=%u",
                      (unsigned)(FM_MAX_VISIBLE * sizeof(_fm_item_t)));
            return;
        }
    }
    int cnt = 0;
    _fm_build_items(s_fm_root, 0, &cnt);
    s_fm_item_count = cnt;

    int list_w = lv_obj_get_width(s_fm_list);
    if (list_w <= 0) list_w = 196;
    int name_w = list_w - 30 - 8; /* indent room + arrow */

    for (int i = 0; i < cnt; i++) {
        _fm_node_t *node = s_fm_items[i].node;
        int depth = s_fm_items[i].depth;

        lv_obj_t *row = lv_button_create(s_fm_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, list_w, FM_ROW_H);
        /* rows must not be scroll targets or they swallow the drag gesture */
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(_UI_TEXT), 0);
        lv_obj_set_style_bg_opa(row, i % 2 ? 10 : 0, 0);   /* faint zebra */
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_pos(row, 0, i * FM_ROW_H);

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
        lv_obj_set_width(lbl, name_w);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8 + depth * 12, 0);
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
    s_fm_title = NULL;
}

static void _fm_row_cb(lv_event_t *e)
{
    _fm_node_t *node = (_fm_node_t *)lv_event_get_user_data(e);
    if (!node) return;
    EOS_LOG_I("Album: click %s is_dir=%d expanded=%d", node->name,
              node->is_dir, node->expanded);

    if (node->is_dir) {
        if (node->expanded) {
            node->expanded = false;
            _fm_children_free(node);
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
        _open_image_path(path);
    }
}

static void _fm_close_cb(lv_event_t *e)
{
    (void)e;
    _fm_close();
}

static void _fm_open(void);

static void _fm_open_cb(lv_event_t *e)
{
    (void)e;
    _fm_open();
}

static void _fm_open(void)
{
    if (s_fm_open) return;
    s_fm_open = true;
    EOS_LOG_I("Album: fm open");

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
    lv_obj_set_size(s_fm_panel, 212, 208);
    lv_obj_set_pos(s_fm_panel, 14, 24);
    lv_obj_set_style_bg_color(s_fm_panel, lv_color_hex(_UI_PANEL), 0);
    lv_obj_set_style_bg_opa(s_fm_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_fm_panel, 14, 0);
    lv_obj_set_style_border_width(s_fm_panel, 0, 0);
    lv_obj_clear_flag(s_fm_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* panel sits above the mask; no event bubbling to mask by default */

    /* header: path + close */
    s_fm_title = lv_label_create(s_fm_panel);
    eos_label_set_font_size(s_fm_title, EOS_FONT_SIZE_MICRO);
    lv_obj_set_style_text_color(s_fm_title, lv_color_hex(_UI_DIM), 0);
    lv_obj_set_size(s_fm_title, 140, 18);
    lv_obj_set_pos(s_fm_title, 14, 10);
    lv_label_set_long_mode(s_fm_title, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(s_fm_title, ALBUM_DIR);
    lv_obj_remove_flag(s_fm_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_fm_title, LV_OBJ_FLAG_SCROLLABLE);

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

    /* list (5 rows of FM_ROW_H visible) */
    s_fm_list = lv_obj_create(s_fm_panel);
    lv_obj_remove_style_all(s_fm_list);
    lv_obj_set_size(s_fm_list, 200, 172);
    lv_obj_set_pos(s_fm_list, 6, 30);
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
    s_fm_root = (_fm_node_t *)eos_malloc_zeroed(sizeof(_fm_node_t));
    if (s_fm_root) {
        strncpy(s_fm_root->name, "album", sizeof(s_fm_root->name) - 1);
        strncpy(s_fm_root->path, ALBUM_DIR, sizeof(s_fm_root->path) - 1);
        s_fm_root->is_dir = true;
        s_fm_root->expanded = true;    /* show root level immediately */
        _fm_node_load(s_fm_root);      /* load root lazily now */
    } else {
        EOS_LOG_E("Album: root alloc FAIL size=%u", (unsigned)sizeof(_fm_node_t));
    }

    _fm_rebuild();
}

/* ------------------------------------------------------------------ */
/* Events                                                             */
/* ------------------------------------------------------------------ */

static void _prev_cb(lv_event_t *e)
{
    (void)e;
    if (s_count <= 0) return;
    s_index = (s_index + s_count - 1) % s_count;
    _show_current();
}

static void _next_cb(lv_event_t *e)
{
    (void)e;
    if (s_count <= 0) return;
    s_index = (s_index + 1) % s_count;
    _show_current();
}

static void _trash_cb(lv_event_t *e)
{
    (void)e;
    if (s_count <= 0) return;

    const char *path = s_paths[s_index];
    EOS_LOG_I("Album: delete %s", path);
    eos_storage_file_remove(path);

    for (int i = s_index; i < s_count - 1; i++) {
        strcpy(s_paths[i], s_paths[i + 1]);
    }
    s_count--;
    if (s_count <= 0) {
        s_index = 0;
        _show_empty();
        return;
    }
    if (s_index >= s_count) s_index = s_count - 1;
    _show_current();
}

static void _zoom_cb(lv_event_t *e)
{
    (void)e;
    if (s_count <= 0) return;
    s_zoom_idx = (s_zoom_idx + 1) % ALBUM_ZOOM_N;
    _apply_view();
    EOS_LOG_I("Album: zoom idx=%d", s_zoom_idx);
}

/* image touch gestures (single-point touch hardware):
 *  - tap (press+release, small move)     -> cycle zoom
 *  - drag (move while pressed, zoomed in) -> pan the magnified image
 * Only one touch point exists (CHSC6X), so no pinch. */
static int  s_press_x = 0, s_press_y = 0;
static bool s_dragging = false;

static void _img_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_press_x = p.x;
        s_press_y = p.y;
        s_dragging = false;
    }
    else if (code == LV_EVENT_PRESSING) {
        int dx = p.x - s_press_x;
        int dy = p.y - s_press_y;
        /* finger moved beyond threshold -> this press becomes a drag */
        if (!s_dragging && (int)lv_sqrt32((uint32_t)(dx * dx + dy * dy)) > 8) {
            s_dragging = true;
        }
        if (s_dragging && s_zoom_idx > 0) {
            /* content follows finger; scale factor = current zoom */
            int zz = (ALBUM_ZOOM_LEVELS[s_zoom_idx] + 127) / 256; /* 1,2,3 */
            s_pan_x += dx * zz;
            s_pan_y += dy * zz;
            s_press_x = p.x;
            s_press_y = p.y;
            _apply_view();
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        if (!s_dragging && s_count > 0) {
            _zoom_cb(NULL); /* tap -> cycle zoom */
        }
        s_dragging = false;
    }
}

/* ------------------------------------------------------------------ */
/* UI                                                                 */
/* ------------------------------------------------------------------ */

static lv_obj_t *_make_btn(lv_obj_t *parent, int w, int h, uint32_t bg,
                           lv_align_t align, int x, int y,
                           lv_event_cb_t cb, const char *icon)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_align(btn, align, x, y);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &EOS_FONT_ICON, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(_UI_TEXT), 0);
    lv_label_set_text(lbl, icon);
    lv_obj_center(lbl);
    return btn;
}

static void _build_ui(eos_activity_t *act)
{
    s_root = eos_activity_get_view(act);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(_UI_BG), 0);
    eos_round_clip(s_root);

    /* scrolling file name */
    s_name = lv_label_create(s_root);
    lv_obj_set_width(s_name, ALBUM_NAME_W);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(s_name, lv_color_hex(_UI_TEXT), 0);
    lv_obj_set_style_text_font(s_name, &lv_font_montserrat_14, 0);
    lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 24);

    /* image (tap to zoom, drag to pan when zoomed in) */
    s_img = lv_image_create(s_root);
    lv_obj_center(s_img);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_img, _img_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_img, _img_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_img, _img_event_cb, LV_EVENT_RELEASED, NULL);

    /* left / right arrows */
    s_prev_btn = _make_btn(s_root, 38, 72, _UI_BTN_BG, LV_ALIGN_LEFT_MID, 2, 0,
                           _prev_cb, RI_ARROW_LEFT_S_LINE);
    s_next_btn = _make_btn(s_root, 38, 72, _UI_BTN_BG, LV_ALIGN_RIGHT_MID, -2, 0,
                           _next_cb, RI_ARROW_RIGHT_S_LINE);

    /* trash (center) at bottom */
    s_trash_btn = _make_btn(s_root, 48, 32, _UI_BTN_TRASH, LV_ALIGN_BOTTOM_MID, 0, -16,
                            _trash_cb, RI_DELETE_BIN_5_LINE);

    /* file manager button (bottom-right) */
    s_fm_btn = _make_btn(s_root, 48, 32, _UI_BTN_BG, LV_ALIGN_BOTTOM_RIGHT, -8, -16,
                         _fm_open_cb, RI_FOLDER_3_LINE);

    /* counter */
    s_counter = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_counter, lv_color_hex(_UI_DIM), 0);
    lv_obj_set_style_text_font(s_counter, &lv_font_montserrat_12, 0);
    lv_obj_align(s_counter, LV_ALIGN_BOTTOM_LEFT, 8, -16);

    /* message */
    s_msg = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_msg, lv_color_hex(_UI_DIM), 0);
    lv_obj_set_style_text_font(s_msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_msg, 180);
    lv_obj_center(s_msg);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/* 放大状态下右滑是"拖图看细节"手势，不能被系统当成退出；
 * 未放大时右滑仍按系统默认退出。 */
static bool _swipe_back_cb(eos_activity_t *self, lv_dir_t dir)
{
    (void)self;
    (void)dir;
    return s_zoom_idx > 0;
}

static void _on_enter(eos_activity_t *act)
{
    s_act = act;
    eos_activity_set_swipe_back_handler(act, _swipe_back_cb);
    _build_ui(act);

    /* resume last viewed image; otherwise open the file manager */
    char hist[EOS_FS_PATH_MAX];
    if (_read_history(hist, sizeof(hist)) && _is_image_ext(_base(hist))) {
        if (!_open_image_path(hist)) {
            _show_empty();
            _fm_open();
        }
    } else {
        _show_empty();
        _fm_open();
    }
}

static void _on_destroy(eos_activity_t *act)
{
    (void)act;
    if (s_fm_open) _fm_close();
    _free_pixels();
    if (s_paths) {
        eos_free(s_paths);
        s_paths = NULL;
    }
    s_count = 0;
    s_index = 0;
}

static const eos_activity_lifecycle_t s_lifecycle = {
    .on_enter   = _on_enter,
    .on_destroy = _on_destroy,
};

void eos_album_enter(void)
{
    EOS_LOG_I("Album: enter");
    eos_activity_t *act = eos_activity_create(&s_lifecycle);
    eos_activity_set_type(act, EOS_ACTIVITY_TYPE_APP);
    eos_activity_enter(act);
}
