/**
 * @file eos_files.c
 * @brief Minimal file browser (native app)
 *
 * A two-mode native app:
 *   - LIST mode: shows the entries of the current directory. Tapping a
 *     directory descends into it; tapping a file opens the text viewer.
 *   - VIEW mode: shows the file's text in a scrollable label.
 *
 * Navigation: "Up" goes to the parent directory, "Exit" leaves the app,
 * and a "Back" button (shown only in VIEW mode) returns to the list.
 */

#include "eos_files.h"

#include <stdio.h>
#include <string.h>

#include "eos_log.h"
#include "eos_config.h"
#include "eos_service_config.h"   /* P0.5 相册: album_pick 回传 */
#include "cJSON.h"                /* P0.5 相册: 写 app 私有 config.json */
#include "eos_activity.h"
#include "eos_mem.h"
#include "eos_service_storage.h"
#include "eos_storage_paths.h"
#include "lvgl.h"
#include "ui/system/eos_round_clip.h" /* eos_round_clip() */

/* ------------------------------------------------------------------ */
/* Config                                                             */
/* ------------------------------------------------------------------ */
#define EOS_LOG_TAG "Files"

#define FILES_MAX 64              /* max entries per directory listing   */

/* ------------------------------------------------------------------ */
/* State (single-instance browser)                                    */
/* ------------------------------------------------------------------ */
typedef enum
{
    FILES_STATE_LIST,
    FILES_STATE_VIEW
} files_state_t;

static char s_paths[FILES_MAX][EOS_FS_PATH_MAX];  /* full paths for current list */
static int  s_fcount = 0;

typedef struct
{
    eos_activity_t *activity;
    lv_obj_t *title;
    lv_obj_t *content;   /* scrollable container holding rows / viewer text */
    lv_obj_t *back_btn;  /* visible only in VIEW mode                       */
    files_state_t state;
    char cur_path[EOS_FS_PATH_MAX];
    char view_path[EOS_FS_PATH_MAX];
    char *view_text;     /* malloc'd by eos_storage_read_file; freed here   */
} files_ctx_t;

static files_ctx_t s_fctx = {0};

/* ------------------------------------------------------------------ */
/* Forward declarations (callbacks referenced by the list builder)    */
/* ------------------------------------------------------------------ */
static void _files_row_cb(lv_event_t *e);
static void _files_up_cb(lv_event_t *e);
static void _files_viewer_back_cb(lv_event_t *e);
static void _files_exit_cb(lv_event_t *e);

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
static void _files_clear_content(void)
{
    if (s_fctx.content)
        lv_obj_clean(s_fctx.content);
}

static void _files_set_title(const char *path)
{
    if (!s_fctx.title)
        return;
    size_t len = strlen(path);
    if (len <= 18)
    {
        lv_label_set_text(s_fctx.title, path);
    }
    else
    {
        char buf[22];
        snprintf(buf, sizeof(buf), "...%s", path + (len - 15));
        lv_label_set_text(s_fctx.title, buf);
    }
}

static void _files_ensure_trailing_slash(void)
{
    size_t len = strlen(s_fctx.cur_path);
    if (len == 0)
    {
        s_fctx.cur_path[0] = '/';
        s_fctx.cur_path[1] = '\0';
    }
    else if (s_fctx.cur_path[len - 1] != '/')
    {
        s_fctx.cur_path[len] = '/';
        s_fctx.cur_path[len + 1] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* LIST mode                                                          */
/* ------------------------------------------------------------------ */
static void _files_build_list(void)
{
    _files_clear_content();
    s_fctx.state = FILES_STATE_LIST;
    if (s_fctx.back_btn)
        lv_obj_add_flag(s_fctx.back_btn, LV_OBJ_FLAG_HIDDEN);

    _files_set_title(s_fctx.cur_path);

    eos_dir_t dir = eos_storage_dir_open(s_fctx.cur_path);
    if (!dir)
    {
        lv_obj_t *e = lv_label_create(s_fctx.content);
        lv_label_set_text(e, "Cannot open directory");
        lv_obj_set_style_text_color(e, lv_color_white(), 0);
        return;
    }

    char name[64];
    s_fcount = 0;
    while (eos_storage_dir_read(dir, name, sizeof(name)) == EOS_OK && s_fcount < FILES_MAX)
    {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        char full[EOS_FS_PATH_MAX];
        snprintf(full, sizeof(full), "%s%s", s_fctx.cur_path, name);
        strncpy(s_paths[s_fcount], full, EOS_FS_PATH_MAX - 1);
        s_paths[s_fcount][EOS_FS_PATH_MAX - 1] = '\0';
        s_fcount++;
    }
    eos_storage_dir_close(dir);

    if (s_fcount == 0)
    {
        lv_obj_t *e = lv_label_create(s_fctx.content);
        lv_label_set_text(e, "(empty)");
        lv_obj_set_style_text_color(e, lv_color_hex(0x9AA4B2), 0);
        lv_obj_align(e, LV_ALIGN_TOP_MID, 0, 10);
        return;
    }

    for (int i = 0; i < s_fcount; i++)
    {
        bool is_dir = eos_storage_is_dir(s_paths[i]);

        lv_obj_t *row = lv_button_create(s_fctx.content);
        lv_obj_set_size(row, 210, 30);
        lv_obj_set_pos(row, 7, 4 + i * 34);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1F2937), 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

        /* type indicator dot: blue = directory, grey = file */
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, is_dir ? lv_color_hex(0x2F80ED) : lv_color_hex(0x9AA4B2), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 8, 0);

        const char *base = strrchr(s_paths[i], '/');
        base = base ? base + 1 : s_paths[i];

        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, base);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lab, lv_color_white(), 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 24, 0);

        /* point the row at its cached full path */
        lv_obj_set_user_data(row, s_paths[i]);
        lv_obj_add_event_cb(row, _files_row_cb, LV_EVENT_CLICKED, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* VIEW mode                                                          */
/* ------------------------------------------------------------------ */
static void _files_open_viewer(const char *path)
{
    /* copy the path before we clear the (still-alive) list content */
    strncpy(s_fctx.view_path, path, EOS_FS_PATH_MAX - 1);
    s_fctx.view_path[EOS_FS_PATH_MAX - 1] = '\0';

    _files_clear_content();
    s_fctx.state = FILES_STATE_VIEW;
    if (s_fctx.back_btn)
        lv_obj_remove_flag(s_fctx.back_btn, LV_OBJ_FLAG_HIDDEN);

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    _files_set_title(base);

    if (s_fctx.view_text)
    {
        eos_free(s_fctx.view_text);
        s_fctx.view_text = NULL;
    }

    char *txt = eos_storage_read_file(path);
    s_fctx.view_text = txt;   /* freed on destroy / next open */

    lv_obj_t *ta = lv_label_create(s_fctx.content);
    lv_label_set_long_mode(ta, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(ta, 212);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ta, lv_color_white(), 0);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_label_set_text(ta, txt ? txt : "(empty or unreadable file)");
}

/* ------------------------------------------------------------------ */
/* Event callbacks                                                    */
/* ------------------------------------------------------------------ */
static void _files_row_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    char *fp = (char *)lv_obj_get_user_data(row);
    if (!fp)
        return;

    if (eos_storage_is_dir(fp))
    {
        strncpy(s_fctx.cur_path, fp, EOS_FS_PATH_MAX - 1);
        s_fctx.cur_path[EOS_FS_PATH_MAX - 1] = '\0';
        _files_ensure_trailing_slash();
        _files_build_list();
        return;
    }

    /* ---- P0.5 相册: 仅拦截图片扩展名（.png/.jpg/.jpeg 不区分大小写）。
     * 命中 -> 写相册 app 私有 config.json 的 album_pick 供相册回读定位，
     * 然后 back 回相册。其余文件行为一字不改（走原文本查看器路径）。
     * NOTE: 必须写相册 app 私有 config（EOS_APP_DATA_DIR/<app_id>/config.json），
     * 与 JS 侧 eos.config.getStr 同路径；写系统 cfg.json 会跨层读不到（探针已证）。 ---- */
    {
        const char *dot = strrchr(fp, '.');
        if (dot && dot[1] != '\0')
        {
            char ext[8];
            size_t el = strlen(dot + 1);
            if (el >= sizeof(ext))
                el = sizeof(ext) - 1;
            for (size_t i = 0; i < el; i++)
            {
                char c = dot[1 + i];
                ext[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            }
            ext[el] = '\0';
            if (strcmp(ext, "png") == 0 || strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0)
            {
                const char *album_app_id = "com.elenix.album";
                char cfg_dir[EOS_FS_PATH_MAX];
                char cfg_path[EOS_FS_PATH_MAX];
                cJSON *cfg_root = NULL;

                snprintf(cfg_dir, sizeof(cfg_dir), EOS_APP_DATA_DIR "%s", album_app_id);
                eos_storage_mkdir_if_not_exist(cfg_dir);
                snprintf(cfg_path, sizeof(cfg_path), EOS_APP_DATA_DIR "%s/config.json", album_app_id);

                cfg_root = eos_storage_json_load(cfg_path);
                if (!cfg_root)
                {
                    cfg_root = cJSON_CreateObject();
                }
                if (cfg_root)
                {
                    cJSON *item = cJSON_GetObjectItem(cfg_root, "album_pick");
                    if (item)
                        cJSON_SetValuestring(item, fp);
                    else
                        cJSON_AddStringToObject(cfg_root, "album_pick", fp);
                    eos_storage_json_save(cfg_path, cfg_root);
                    cJSON_Delete(cfg_root);
                }
                eos_activity_back();
                return;
            }
        }
    }

    _files_open_viewer(fp);
}

static void _files_up_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    size_t len = strlen(s_fctx.cur_path);
    if (len > 1 && s_fctx.cur_path[len - 1] == '/')
    {
        s_fctx.cur_path[len - 1] = '\0';
        len--;
    }
    char *slash = strrchr(s_fctx.cur_path, '/');
    if (slash && slash != s_fctx.cur_path)
        *slash = '\0';
    else if (slash == s_fctx.cur_path)
        s_fctx.cur_path[1] = '\0';   /* parent of "/x" is "/" */

    _files_ensure_trailing_slash();
    _files_build_list();
}

static void _files_viewer_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    _files_build_list();
}

static void _files_exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    eos_activity_back();
}

/* ------------------------------------------------------------------ */
/* Activity lifecycle                                                 */
/* ------------------------------------------------------------------ */
static void _files_on_enter(eos_activity_t *activity)
{
    lv_obj_t *view = eos_activity_get_view(activity);
    eos_round_clip(view);
    lv_obj_set_style_bg_color(view, lv_color_black(), 0);

    memset(&s_fctx, 0, sizeof(s_fctx));
    s_fctx.activity = activity;
    strncpy(s_fctx.cur_path, EOS_FILES_ROOT_DIR, EOS_FS_PATH_MAX - 1);
    s_fctx.cur_path[EOS_FS_PATH_MAX - 1] = '\0';
    _files_ensure_trailing_slash();

    /* title (current path) */
    lv_obj_t *title = lv_label_create(view);
    lv_label_set_text(title, "/");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    s_fctx.title = title;

    /* up (top-left) */
    lv_obj_t *up = lv_button_create(view);
    lv_obj_set_size(up, 40, 28);
    lv_obj_align(up, LV_ALIGN_TOP_LEFT, 8, 14);
    lv_obj_set_style_bg_color(up, lv_color_hex(0x374151), 0);
    lv_obj_t *ul = lv_label_create(up);
    lv_label_set_text(ul, "Up");
    lv_obj_set_style_text_font(ul, &lv_font_montserrat_14, 0);
    lv_obj_center(ul);
    lv_obj_add_event_cb(up, _files_up_cb, LV_EVENT_CLICKED, NULL);

    /* exit (top-right) — visible in LIST mode */
    lv_obj_t *exit = lv_button_create(view);
    lv_obj_set_size(exit, 44, 28);
    lv_obj_align(exit, LV_ALIGN_TOP_RIGHT, -8, 14);
    lv_obj_set_style_bg_color(exit, lv_color_hex(0x2F80ED), 0);
    lv_obj_t *el = lv_label_create(exit);
    lv_label_set_text(el, "Exit");
    lv_obj_set_style_text_font(el, &lv_font_montserrat_14, 0);
    lv_obj_center(el);
    lv_obj_add_event_cb(exit, _files_exit_cb, LV_EVENT_CLICKED, NULL);

    /* back (top-right) — visible only in VIEW mode */
    lv_obj_t *vb = lv_button_create(view);
    lv_obj_set_size(vb, 44, 28);
    lv_obj_align(vb, LV_ALIGN_TOP_RIGHT, -8, 14);
    lv_obj_set_style_bg_color(vb, lv_color_hex(0x2F80ED), 0);
    lv_obj_t *vl = lv_label_create(vb);
    lv_label_set_text(vl, "Back");
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_14, 0);
    lv_obj_center(vl);
    lv_obj_add_event_cb(vb, _files_viewer_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(vb, LV_OBJ_FLAG_HIDDEN);
    s_fctx.back_btn = vb;

    /* scrollable content area */
    lv_obj_t *content = lv_obj_create(view);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 224, 176);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    s_fctx.content = content;

    _files_build_list();

    EOS_LOG_I("Files opened at '%s'", s_fctx.cur_path);
}

static void _files_on_destroy(eos_activity_t *activity)
{
    LV_UNUSED(activity);
    if (s_fctx.view_text)
    {
        eos_free(s_fctx.view_text);
        s_fctx.view_text = NULL;
    }
    memset(&s_fctx, 0, sizeof(s_fctx));
}

static const eos_activity_lifecycle_t s_files_lc = {
    .on_enter = _files_on_enter,
    .on_destroy = _files_on_destroy,
};

void eos_files_enter(void)
{
    eos_activity_t *a = eos_activity_create(&s_files_lc);
    if (!a)
        return;
    eos_activity_set_type(a, EOS_ACTIVITY_TYPE_APP);
    eos_activity_enter(a);
}
