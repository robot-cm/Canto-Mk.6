/**
 * @file eos_album.c
 * @brief Album - native C photo viewer
 *
 * Scans /sdcard/album/ recursively for images, sorted by name.
 * - JPEG: decoded to RGB565 with the bundled TJpgDec (streamed from SD,
 *         output capped at ALBUM_DECODE_MAX per side to stay OOM-safe).
 * - PNG: decoded by LVGL (LodePNG) from path, dimension pre-checked.
 *
 * UI (240x240 round): top scrolling file name, centered image, left/right
 * arrows, bottom trash (delete current), bottom-left counter. English only.
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
#define ALBUM_MAX          128           /* max images in the list          */
#define ALBUM_MAX_DEPTH    4             /* max recursion depth             */
#define ALBUM_IMG_MAX      150           /* fit box (px) for the image      */
#define ALBUM_DECODE_MAX   1024          /* JPEG decode cap per side (px)   */
#define ALBUM_PNG_MAX      1024          /* PNG side cap (LVGL decodes full) */

/* Zoom levels (zoom=1.0 means fit-to-box). Used for the magnifier. */
static const uint32_t ALBUM_ZOOM_LEVELS[] = {256, 384, 512, 768}; /* 1.0 / 1.5 / 2.0 / 3.0 (Q16) */
#define ALBUM_ZOOM_N (sizeof(ALBUM_ZOOM_LEVELS) / sizeof(ALBUM_ZOOM_LEVELS[0]))
#define ALBUM_ZOOM_IDX_DEF 0
#define ALBUM_NAME_W       132           /* scrolling name label width      */
#define ALBUM_WORK_SIZE    8192          /* TJpgDec working pool (PSRAM)    */

#define _UI_BG        0x12121A
#define _UI_BTN_BG    0x1E1E2E
#define _UI_BTN_TRASH 0xB03A2E
#define _UI_TEXT      0xFFFFFF
#define _UI_DIM       0x8A8AA0

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
static lv_obj_t *s_prev_btn, *s_next_btn, *s_trash_btn;

/* magnifier state (zoom > 1.0 lets the image overflow -> pan via joystick) */
static int  s_zoom_idx = ALBUM_ZOOM_IDX_DEF;
static int  s_pan_x = 0, s_pan_y = 0;   /* image offset from center, px */
static int  s_img_w = 0, s_img_h = 0;   /* current image decoded size */

static char (*s_paths)[EOS_FS_PATH_MAX]; /* list of full paths (PSRAM) */
static int   s_count;
static int   s_index;

static uint8_t       *s_pixels;          /* current JPEG decode buffer */
static lv_image_dsc_t s_dsc;

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
/* Scan + sort                                                        */
/* ------------------------------------------------------------------ */

static void _scan_dir(const char *dir, int depth)
{
    if (depth > ALBUM_MAX_DEPTH || s_count >= ALBUM_MAX) return;

    eos_dir_t d = eos_storage_dir_open(dir);
    if (!d) return;

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
        if (eos_storage_is_dir(full)) {
            _scan_dir(full, depth + 1);
        } else if (eos_storage_is_file(full) && _is_image_ext(name)) {
            strncpy(s_paths[s_count], full, EOS_FS_PATH_MAX - 1);
            s_paths[s_count][EOS_FS_PATH_MAX - 1] = '\0';
            s_count++;
        }
    }
    eos_storage_dir_close(d);
}

static int _cmp_path(const void *a, const void *b)
{
    const char *pa = _base((const char *)a);
    const char *pb = _base((const char *)b);
    return strcasecmp(pa, pb);
}

static int _reload(void)
{
    if (s_paths) {
        eos_free(s_paths);
        s_paths = NULL;
    }
    s_count = 0;
    s_index = 0;

    s_paths = (char (*)[EOS_FS_PATH_MAX])eos_malloc(ALBUM_MAX * EOS_FS_PATH_MAX);
    if (!s_paths) return 0;

    if (eos_storage_is_dir(ALBUM_DIR)) {
        _scan_dir(ALBUM_DIR, 0);
    }
    qsort(s_paths, s_count, EOS_FS_PATH_MAX, _cmp_path);
    return s_count;
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
    _show_msg("NO IMAGES\nPut .jpg / .png\nunder /sdcard/album/");
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
}

/* ------------------------------------------------------------------ */
/* Events                                                             */
/* ------------------------------------------------------------------ */

static void _prev_cb(lv_event_t *e)
{
    if (s_count <= 0) return;
    s_index = (s_index + s_count - 1) % s_count;
    _show_current();
}

static void _next_cb(lv_event_t *e)
{
    if (s_count <= 0) return;
    s_index = (s_index + 1) % s_count;
    _show_current();
}

static void _trash_cb(lv_event_t *e)
{
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
    if (_reload() > 0) {
        _show_current();
    } else {
        _show_empty();
    }
}

static void _on_destroy(eos_activity_t *act)
{
    (void)act;
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
