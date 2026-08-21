/**
 * @file eos_gallery.c
 * @brief Minimal image viewer (native app)
 *
 * Scans EOS_GALLERY_DIR for PNG images (LodePNG decoder is enabled in the
 * simulator build) and displays them fit-scaled on the round display with
 * prev/next navigation and an image counter.
 */

#include "eos_gallery.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "eos_log.h"
#include "eos_config.h"
#include "eos_activity.h"
#include "eos_service_storage.h"
#include "eos_storage_paths.h"
#include "lvgl.h"
#include "ui/system/eos_round_clip.h" /* eos_round_clip() */

/* ------------------------------------------------------------------ */
/* Config                                                             */
/* ------------------------------------------------------------------ */
#define EOS_LOG_TAG "Gallery"

#define GALLERY_MAX   64          /* max images cached per session       */
#define GALLERY_TARGET 150        /* fit box (px) for the round display  */

/* ------------------------------------------------------------------ */
/* State (single-instance viewer)                                     */
/* ------------------------------------------------------------------ */
static char s_paths[GALLERY_MAX][EOS_FS_PATH_MAX];
static int  s_count = 0;
static int  s_index = 0;

typedef struct
{
    eos_activity_t *activity;
    lv_obj_t *img;
    lv_obj_t *title;
    lv_obj_t *counter;
} gallery_ctx_t;

static gallery_ctx_t s_gctx = {0};

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
static bool _gallery_is_png(const char *name)
{
    size_t len = strlen(name);
    if (len < 4)
        return false;
    const char *ext = name + len - 4;
    return (strncmp(ext, ".png", 4) == 0) || (strncmp(ext, ".PNG", 4) == 0);
}

static int _gallery_cmp(const char *a, const char *b)
{
    /* case-insensitive compare so the list is stable & sorted */
    for (;;)
    {
        if (*a == '\0' && *b == '\0')
            return 0;
        char ca = (char)tolower((unsigned char)*a);
        char cb = (char)tolower((unsigned char)*b);
        if (ca != cb)
            return (ca - cb);
        a++;
        b++;
    }
}

static void _gallery_scan(void)
{
    s_count = 0;
    eos_dir_t dir = eos_storage_dir_open(EOS_GALLERY_DIR);
    if (!dir)
        return;

    char name[64];
    while (eos_storage_dir_read(dir, name, sizeof(name)) == EOS_OK && s_count < GALLERY_MAX)
    {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        char full[EOS_FS_PATH_MAX];
        snprintf(full, sizeof(full), "%s%s", EOS_GALLERY_DIR, name);
        if (!eos_storage_is_file(full))
            continue;
        if (!_gallery_is_png(name))
            continue;

        strncpy(s_paths[s_count], full, EOS_FS_PATH_MAX - 1);
        s_paths[s_count][EOS_FS_PATH_MAX - 1] = '\0';
        s_count++;
    }
    eos_storage_dir_close(dir);

    /* simple selection sort (small N) */
    for (int i = 0; i < s_count - 1; i++)
    {
        for (int j = i + 1; j < s_count; j++)
        {
            if (_gallery_cmp(s_paths[i], s_paths[j]) > 0)
            {
                char tmp[EOS_FS_PATH_MAX];
                memcpy(tmp, s_paths[i], sizeof(tmp));
                memcpy(s_paths[i], s_paths[j], sizeof(tmp));
                memcpy(s_paths[j], tmp, sizeof(tmp));
            }
        }
    }
}

static void _gallery_show(int idx)
{
    if (s_count == 0)
    {
        if (s_gctx.title)
            lv_label_set_text(s_gctx.title, "No images");
        if (s_gctx.counter)
            lv_label_set_text(s_gctx.counter, "");
        return;
    }

    if (idx < 0)
        idx = s_count - 1;
    if (idx >= s_count)
        idx = 0;
    s_index = idx;

    const char *path = s_paths[idx];
    lv_image_set_src(s_gctx.img, path);

    /* fit-scale into a GALLERY_TARGET box (256 == 1:1) */
    lv_image_header_t hdr;
    if (lv_image_decoder_get_info(path, &hdr) == LV_RESULT_OK && hdr.w > 0 && hdr.h > 0)
    {
        int32_t sx = (GALLERY_TARGET * 256) / (int32_t)hdr.w;
        int32_t sy = (GALLERY_TARGET * 256) / (int32_t)hdr.h;
        int32_t s  = sx < sy ? sx : sy;
        if (s < 1)
            s = 1;
        if (s > 1024)
            s = 1024;
        lv_image_set_scale(s_gctx.img, (uint32_t)s);
    }
    else
    {
        lv_image_set_scale(s_gctx.img, 256);
    }

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (s_gctx.title)
        lv_label_set_text(s_gctx.title, base);

    if (s_gctx.counter)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d/%d", idx + 1, s_count);
        lv_label_set_text(s_gctx.counter, buf);
    }
}

/* ------------------------------------------------------------------ */
/* Event callbacks                                                    */
/* ------------------------------------------------------------------ */
static void _gallery_prev_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    _gallery_show(s_index - 1);
}

static void _gallery_next_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    _gallery_show(s_index + 1);
}

static void _gallery_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    eos_activity_back();
}

/* ------------------------------------------------------------------ */
/* Activity lifecycle                                                 */
/* ------------------------------------------------------------------ */
static void _gallery_on_enter(eos_activity_t *activity)
{
    lv_obj_t *view = eos_activity_get_view(activity);
    eos_round_clip(view);
    lv_obj_set_style_bg_color(view, lv_color_black(), 0);

    s_gctx.activity = activity;

    /* title (current file name) */
    lv_obj_t *title = lv_label_create(view);
    lv_label_set_text(title, "Gallery");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    s_gctx.title = title;

    /* back (top-left) */
    lv_obj_t *back = lv_button_create(view);
    lv_obj_set_size(back, 44, 28);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 8, 14);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2F80ED), 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, _gallery_back_cb, LV_EVENT_CLICKED, NULL);

    /* image (centered) */
    lv_obj_t *img = lv_image_create(view);
    lv_obj_center(img);
    s_gctx.img = img;

    /* prev (bottom-left) / counter (bottom-mid) / next (bottom-right) */
    lv_obj_t *prev = lv_button_create(view);
    lv_obj_set_size(prev, 56, 32);
    lv_obj_align(prev, LV_ALIGN_BOTTOM_LEFT, 14, -16);
    lv_obj_set_style_bg_color(prev, lv_color_hex(0x374151), 0);
    lv_obj_t *pl = lv_label_create(prev);
    lv_label_set_text(pl, "<");
    lv_obj_center(pl);
    lv_obj_add_event_cb(prev, _gallery_prev_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next = lv_button_create(view);
    lv_obj_set_size(next, 56, 32);
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -14, -16);
    lv_obj_set_style_bg_color(next, lv_color_hex(0x374151), 0);
    lv_obj_t *nl = lv_label_create(next);
    lv_label_set_text(nl, ">");
    lv_obj_center(nl);
    lv_obj_add_event_cb(next, _gallery_next_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *counter = lv_label_create(view);
    lv_label_set_text(counter, "0/0");
    lv_obj_set_style_text_font(counter, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(counter, lv_color_hex(0x9AA4B2), 0);
    lv_obj_align(counter, LV_ALIGN_BOTTOM_MID, 0, -22);
    s_gctx.counter = counter;

    _gallery_scan();
    _gallery_show(0);

    EOS_LOG_I("Gallery opened (%d images)", s_count);
}

static void _gallery_on_destroy(eos_activity_t *activity)
{
    LV_UNUSED(activity);
    memset(&s_gctx, 0, sizeof(s_gctx));
    s_count = 0;
    s_index = 0;
}

static const eos_activity_lifecycle_t s_gallery_lc = {
    .on_enter = _gallery_on_enter,
    .on_destroy = _gallery_on_destroy,
};

void eos_gallery_enter(void)
{
    eos_activity_t *a = eos_activity_create(&s_gallery_lc);
    if (!a)
        return;
    eos_activity_set_type(a, EOS_ACTIVITY_TYPE_APP);
    eos_activity_enter(a);
}
