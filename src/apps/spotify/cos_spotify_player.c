/**
 * @file cos_spotify_player.c
 * @brief Spotify 播放界面实现:文件树 + 播放态(三行歌词 + 进度条 + 控制键)。
 *
 * 交付批次:Batch 3(音量弧/速度弧在 Batch 4 追加)。
 *
 * ── 刷新模型 ────────────────────────────────────────────────
 *   App 的 lv_timer(50ms)调用 cos_spotify_player_tick():
 *     1. audio_pump()      解码一片 → PCM 环形缓冲
 *     2. audio_flush()     环形缓冲 → UAC 等时端点
 *     3. _update_lyrics()  按播放位置切换/滚动歌词
 *     4. _update_progress()更新进度条
 *   全部在 ui_task 内,无新增任务。
 *
 * ── 歌词三行 ────────────────────────────────────────────────
 *   三行固定位置(上/中/下),中间行为当前行并高亮。
 *   上一行/下一行分别显示相邻歌词,越界时显示空白。
 *   中间行超宽时水平滚动,速度由该行持续时间决定。
 */
#include "cos_spotify_player.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <stdio.h>
#include <string.h>

#include "cos_mem.h"
#include "cos_font.h"
#include "cos_service_storage.h"
#include "ui/system/cos_round_clip.h"

#include "cos_spotify_audio.h"
#include "cos_spotify_lrc.h"
#include "cos_spotify_tree.h"
#include "cos_spotify_arc.h"

#define COS_LOG_TAG "SpotifyPlayer"
#include "cos_log.h"

/* ── 配色 ─────────────────────────────────────────────────── */

#define C_BG        0x0A0A0C
#define C_TEXT      0xFFFFFF
#define C_DIM       0x6E7378
#define C_ACCENT    0x1ED760
#define C_LRC_CUR   0xFFFFFF
#define C_LRC_OTHER 0x5F6469
#define C_BAR_BG    0x2A2D31
#define C_BAR_FG    0x1ED760
#define C_BTN       0x1C1C1E

/* ── 布局(240×240) ────────────────────────────────────────── */

#define SCR_W        240
#define SCR_H        240

/* 顶部歌名 */
#define TITLE_Y      10
#define TITLE_W      176
#define TITLE_H      22

/* 歌词三行 */
#define LRC_TOP_Y    56
#define LRC_MID_Y    88
#define LRC_BOT_Y    120
#define LRC_ROW_H    30
#define LRC_W        168          /* 可用宽度(留出两侧弧条区域) */

/* 进度条 */
#define PROG_Y       166
#define PROG_W       170
#define PROG_H       6

/* 控制按钮行 */
#define CTRL_Y       186
#define CTRL_BTN_W   34
#define CTRL_BTN_H   34
#define CTRL_GAP     6

/* 滚动刷新 */
#define SCROLL_STEP_MS  50

/* ── 结构 ─────────────────────────────────────────────────── */

struct cos_spotify_player_s
{
    lv_obj_t *parent;

    /* 两个页面 */
    lv_obj_t *screen_tree;
    lv_obj_t *screen_play;

    /* ── 文件树页 ── */
    lv_obj_t *tree_list;
    lv_obj_t *tree_title;
    cos_spotify_tree_t *tree;
    cos_spotify_tree_item_t *items;   /* 快照:node* + depth */

    /* ── 播放页 ── */
    lv_obj_t *lbl_song;      /* 顶部歌名(超宽滚动) */
    lv_obj_t *lbl_lrc_top;
    lv_obj_t *lbl_lrc_mid;
    lv_obj_t *lbl_lrc_bot;
    lv_obj_t *prog_track;
    lv_obj_t *prog_fill;
    lv_obj_t *btn_prev, *btn_rew, *btn_play, *btn_ff, *btn_next;
    lv_obj_t *btn_back;      /* 返回文件树 */

    /* 播放状态 */
    bool playing;
    bool paused;
    char *cur_path;

    /* 歌词 */
    cos_spotify_lrc_t *lrc;
    int   lrc_index;         /* 当前行下标; -1 = 未开始 */
    int   lrc_scroll;        /* 中间行滚动像素偏移(>=0) */
    int   lrc_scroll_max;    /* 本行需要滚动的最大偏移 */
    uint32_t lrc_row_start;  /* 当前行开始显示的 tick */
    uint32_t lrc_row_dur;    /* 当前行显示时长(ms) */

    /* 歌名滚动 */
    int   song_scroll;
    int   song_scroll_max;

    /* 进度拖动 */
    bool  scrubbing;

    /* 邻近歌曲(上/下一首) */
    const char *const *siblings;
    int   sibling_count;
    int   sibling_idx;

    /* 文件树:展开/收起后需要重建列表,但重建不能在事件回调内做
     * (会删除事件目标对象),故用此标志延迟到下一次 tick。 */
    bool tree_dirty;

    /* 弧形调节(Batch 4) */
    cos_spotify_arc_t *arc_vol;    /* 右侧:音量 0–100      */
    cos_spotify_arc_t *arc_speed;  /* 左侧:速度 0.5–4.0x   */

    /* 回调 */
    cos_spotify_back_cb_t on_back;
    cos_spotify_exit_cb_t on_exit;
    void *user;
};

/* ── 工具 ─────────────────────────────────────────────────── */

static void _set_font(lv_obj_t *o, cos_font_size_t sz)
{
    cos_label_set_font_size(o, sz);
}

static lv_obj_t *_make_label(lv_obj_t *parent, const char *text,
                             uint32_t color, cos_font_size_t font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    _set_font(l, font);
    return l;
}

static lv_obj_t *_make_round_btn(lv_obj_t *parent, const char *txt,
                                 int w, int h, uint32_t bg)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
    _set_font(l, COS_FONT_SIZE_MICRO);
    lv_obj_center(l);
    return b;
}

/* ── 文件树页 ─────────────────────────────────────────────── */

static void _tree_row_clicked(lv_event_t *e);

/** 重建文件树列表(把摊平视图渲染为按钮行)。 */
static void _tree_rebuild(cos_spotify_player_t *p)
{
    if (p->tree_list == NULL)
    {
        return;
    }
    lv_obj_clean(p->tree_list);

    int count = 0;
    const cos_spotify_tree_item_t *items = cos_spotify_tree_items(p->tree, &count);
    if (items == NULL || count == 0)
    {
        lv_obj_t *l = _make_label(p->tree_list,
                                  "No music found.\nPut .mp3 or .wav files\nunder /sdcard/spotify/",
                                  C_DIM, COS_FONT_SIZE_MICRO);
        lv_obj_set_width(l, 180);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 30);
        return;
    }

    for (int i = 0; i < count; i++)
    {
        const cos_spotify_node_t *n = items[i].node;
        bool is_dir = cos_spotify_tree_node_is_dir(n);
        bool expanded = cos_spotify_tree_node_expanded(n);

        lv_obj_t *row = lv_button_create(p->tree_list);
        lv_obj_set_width(row, 176);
        lv_obj_set_height(row, 26);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_BTN), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        /* 缩进体现层级 */
        lv_obj_set_style_pad_left(row, 6 + items[i].depth * 10, 0);

        /* 前缀图标:目录 ▸/▾,文件 ♪ */
        char prefix[8];
        if (is_dir)
        {
            snprintf(prefix, sizeof(prefix), expanded ? "v " : "> ");
        }
        else
        {
            snprintf(prefix, sizeof(prefix), "%% ");
        }

        const char *name = cos_spotify_tree_node_name(n);
        char label[160];
        snprintf(label, sizeof(label), "%s%s", prefix, name);

        lv_obj_t *l = lv_label_create(row);
        lv_label_set_text(l, label);
        lv_obj_set_style_text_color(l, lv_color_hex(is_dir ? C_ACCENT : C_TEXT), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_width(l, 130);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        _set_font(l, COS_FONT_SIZE_MICRO);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

        /* 文件行额外加 ▶ 播放按钮 */
        if (!is_dir)
        {
            lv_obj_t *play = _make_round_btn(row, ">", 22, 22, C_ACCENT);
            lv_obj_align(play, LV_ALIGN_RIGHT_MID, -2, 0);
            lv_obj_add_event_cb(play, _tree_row_clicked, LV_EVENT_CLICKED, p);
            /* 记录路径到按钮 user_data 需要动态分配;改为把路径存到 row,
             * 并在回调中通过 lv_obj_get_child 反查。简化:把 index 存进
             * row 的 user_data,回调用 items 快照取路径。 */
        }

        /* 把行下标存进 row 的 user_data。点击回调会重新摊平取节点,
         * 因此即使期间发生收起,下标也只在"当前可见集合"内解释,安全。 */
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, _tree_row_clicked, LV_EVENT_CLICKED, p);
    }
}

/** 播放态入口(前向声明)。 */
static void _play_begin(cos_spotify_player_t *p, const char *path);
static void _show_tree(cos_spotify_player_t *p);
static void _show_play(cos_spotify_player_t *p);

/** 文件树行点击:目录 → 展开/收起;文件 ▶ → 播放。 */
static void _tree_row_clicked(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_current_target(e);
    if (p == NULL || target == NULL)
    {
        return;
    }
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    if (idx < 0)
    {
        return;
    }

    /* 从快照取节点(快照在每次 rebuild 后仍指向同一批 node) */
    int count = 0;
    const cos_spotify_tree_item_t *items = cos_spotify_tree_items(p->tree, &count);
    if (items == NULL || idx >= count)
    {
        return;
    }
    const cos_spotify_node_t *n = items[idx].node;
    if (n == NULL)
    {
        return;
    }

    if (cos_spotify_tree_node_is_dir(n))
    {
        cos_spotify_tree_toggle(p->tree, (cos_spotify_node_t *)n);
        /* 不能在此处立即 _tree_rebuild():
         * 本回调正运行在 target(该行按钮)的事件分发中,而 rebuild 会
         * _lv_obj_clean 掉包括 target 在内的所有行 —— 在事件处理中途删除
         * 事件目标对象会导致 LVGL 访问已释放内存而崩溃。
         * 因此置标志,交由下一次 tick(事件分发已结束)执行重建。 */
        p->tree_dirty = true;
    }
    else
    {
        _play_begin(p, cos_spotify_tree_node_path(n));
    }
}

static void _show_tree(cos_spotify_player_t *p)
{
    if (p->screen_play)
    {
        lv_obj_add_flag(p->screen_play, LV_OBJ_FLAG_HIDDEN);
    }
    if (p->screen_tree)
    {
        lv_obj_clear_flag(p->screen_tree, LV_OBJ_FLAG_HIDDEN);
    }
    _tree_rebuild(p);
}

static void _show_play(cos_spotify_player_t *p)
{
    if (p->screen_tree)
    {
        lv_obj_add_flag(p->screen_tree, LV_OBJ_FLAG_HIDDEN);
    }
    if (p->screen_play)
    {
        lv_obj_clear_flag(p->screen_play, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ── 播放控制 ─────────────────────────────────────────────── */

static void _update_play_button(cos_spotify_player_t *p)
{
    if (p->btn_play == NULL)
    {
        return;
    }
    lv_obj_t *l = lv_obj_get_child(p->btn_play, 0);
    if (l)
    {
        lv_label_set_text(l, p->paused ? ">" : "||");
    }
}

/** 加载歌词(同名 .lrc)。 */
static void _load_lrc(cos_spotify_player_t *p, const char *audio_path)
{
    if (p->lrc)
    {
        cos_spotify_lrc_free(p->lrc);
        p->lrc = NULL;
    }
    p->lrc_index = -1;
    p->lrc_scroll = 0;
    p->lrc_scroll_max = 0;

    /* 把扩展名换成 .lrc */
    size_t len = strlen(audio_path);
    char *lrc_path = (char *)cos_malloc(len + 8);
    if (lrc_path == NULL)
    {
        return;
    }
    strcpy(lrc_path, audio_path);
    char *dot = strrchr(lrc_path, '.');
    if (dot)
    {
        *dot = '\0';
    }
    strcat(lrc_path, ".lrc");

    p->lrc = cos_spotify_lrc_load(lrc_path);
    cos_free(lrc_path);
}

/** 开始播放指定文件。 */
static void _play_begin(cos_spotify_player_t *p, const char *path)
{
    if (p == NULL || path == NULL)
    {
        return;
    }

    /* 先停掉当前播放 */
    cos_spotify_audio_close();

    cos_spotify_ade_err_t err = COS_SPOTIFY_ADE_OK;
    if (!cos_spotify_audio_open(path, &err))
    {
        COS_LOG_W("play: open failed (%d) %s", (int)err, path);
        /* 显示错误但不退出 App:让用户换一首 */
        if (p->lbl_song)
        {
            lv_label_set_text(p->lbl_song, "Cannot play this file");
        }
        return;
    }

    if (p->cur_path)
    {
        cos_free(p->cur_path);
    }
    p->cur_path = (char *)cos_malloc(strlen(path) + 1);
    if (p->cur_path)
    {
        strcpy(p->cur_path, path);
    }

    /* 刷新同目录歌曲列表,定位当前曲目 */
    p->siblings = cos_spotify_tree_siblings(path, &p->sibling_count);
    p->sibling_idx = 0;
    for (int i = 0; i < p->sibling_count; i++)
    {
        if (strcmp(p->siblings[i], path) == 0)
        {
            p->sibling_idx = i;
            break;
        }
    }

    _load_lrc(p, path);

    p->playing = true;
    p->paused = false;
    p->scrubbing = false;
    p->song_scroll = 0;
    p->song_scroll_max = 0;

    /* 顶部歌名 = 文件名(去扩展名) */
    if (p->lbl_song)
    {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        char name[128];
        snprintf(name, sizeof(name), "%s", base);
        char *d = strrchr(name, '.');
        if (d)
        {
            *d = '\0';
        }
        lv_label_set_text(p->lbl_song, name);
        lv_obj_set_style_text_color(p->lbl_song, lv_color_hex(C_TEXT), 0);
    }

    /* 重置歌词显示 */
    if (p->lbl_lrc_top) lv_label_set_text(p->lbl_lrc_top, "");
    if (p->lbl_lrc_mid)
    {
        lv_label_set_text(p->lbl_lrc_mid, p->lrc ? "" : "Lack of Lyrics");
        lv_obj_set_style_text_color(p->lbl_lrc_mid, lv_color_hex(C_LRC_CUR), 0);
    }
    if (p->lbl_lrc_bot) lv_label_set_text(p->lbl_lrc_bot, "");

    if (p->cur_path)
    {
        COS_LOG_I("play: %s", p->cur_path);
    }
    _update_play_button(p);
    _show_play(p);
}

static void _btn_play_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    if (p == NULL || !p->playing)
    {
        return;
    }
    p->paused = !p->paused;
    _update_play_button(p);
}

static void _btn_back_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    if (p == NULL)
    {
        return;
    }
    if (p->on_back)
    {
        p->on_back(p->user);
    }
}

static void _play_index(cos_spotify_player_t *p, int idx)
{
    if (p->siblings == NULL || p->sibling_count <= 0)
    {
        return;
    }
    if (idx < 0)
    {
        idx = p->sibling_count - 1;
    }
    if (idx >= p->sibling_count)
    {
        idx = 0;
    }
    _play_begin(p, p->siblings[idx]);
}

static void _btn_prev_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    if (p)
    {
        _play_index(p, p->sibling_idx - 1);
    }
}

static void _btn_next_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    if (p)
    {
        _play_index(p, p->sibling_idx + 1);
    }
}

static void _btn_rew_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    if (p)
    {
        cos_spotify_audio_seek_relative_ms(-15000);
    }
}

static void _btn_ff_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    if (p)
    {
        cos_spotify_audio_seek_relative_ms(+15000);
    }
}

/* ── 进度条 ───────────────────────────────────────────────── */

static void _progress_set_from_pos(cos_spotify_player_t *p, int x)
{
    uint32_t dur = cos_spotify_audio_duration_ms();
    if (dur == 0 || p->prog_track == NULL)
    {
        return;
    }
    int track_w = lv_obj_get_width(p->prog_track);
    if (track_w <= 0)
    {
        return;
    }
    if (x < 0)
    {
        x = 0;
    }
    if (x > track_w)
    {
        x = track_w;
    }
    uint32_t target = (uint32_t)(((uint64_t)x * dur) / (uint32_t)track_w);
    cos_spotify_audio_seek_ms(target);
}

static void _progress_event_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    if (p == NULL)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL)
    {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t area;
    lv_obj_get_coords(p->prog_track, &area);
    int rel = pt.x - area.x1;

    if (code == LV_EVENT_PRESSED)
    {
        p->scrubbing = true;
        _progress_set_from_pos(p, rel);
    }
    else if (code == LV_EVENT_PRESSING)
    {
        if (p->scrubbing)
        {
            _progress_set_from_pos(p, rel);
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        p->scrubbing = false;
    }
}

/* ── 歌词刷新 ─────────────────────────────────────────────── */

/**
 * @brief 计算文本像素宽度(用于决定是否需要滚动)。
 * @note 直接取标签自身的字体度量,与显示效果一致。
 *       若标签尚未设置字体,回退到默认字体。
 */
static int _text_width_of(lv_obj_t *label, const char *txt)
{
    if (txt == NULL || txt[0] == '\0')
    {
        return 0;
    }
    const lv_font_t *f = LV_FONT_DEFAULT;
    if (label != NULL)
    {
        const lv_font_t *sf = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        if (sf != NULL)
        {
            f = sf;
        }
    }
    if (f == NULL)
    {
        return 0;
    }
    lv_point_t sz;
    lv_text_get_size(&sz, txt, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

/** 切换中间行到第 idx 行,并重置滚动状态。 */
static void _lrc_goto(cos_spotify_player_t *p, int idx)
{
    p->lrc_index = idx;
    p->lrc_scroll = 0;
    p->lrc_row_start = lv_tick_get();

    const char *cur = cos_spotify_lrc_line(p->lrc, idx);
    const char *prev = cos_spotify_lrc_line(p->lrc, idx - 1);
    const char *next = cos_spotify_lrc_line(p->lrc, idx + 1);

    if (p->lbl_lrc_top)
    {
        lv_label_set_text(p->lbl_lrc_top, prev ? prev : "");
    }
    if (p->lbl_lrc_mid)
    {
        lv_label_set_text(p->lbl_lrc_mid, cur ? cur : "");
        lv_obj_set_style_text_color(p->lbl_lrc_mid, lv_color_hex(C_LRC_CUR), 0);
    }
    if (p->lbl_lrc_bot)
    {
        lv_label_set_text(p->lbl_lrc_bot, next ? next : "");
    }

    /* 计算本行需要滚动的距离 */
    p->lrc_row_dur = cos_spotify_lrc_duration(p->lrc, idx);
    int tw = _text_width_of(p->lbl_lrc_mid, cur);
    p->lrc_scroll_max = (tw > LRC_W) ? (tw - LRC_W) : 0;

    if (p->lbl_lrc_mid)
    {
        lv_obj_align(p->lbl_lrc_mid, LV_ALIGN_TOP_LEFT, 0, LRC_MID_Y);
    }
}

static void _update_lyrics(cos_spotify_player_t *p)
{
    if (p->lbl_lrc_mid == NULL)
    {
        return;
    }

    if (p->lrc == NULL)
    {
        /* 无歌词:中间行固定提示 */
        if (p->lrc_index != -2)
        {
            lv_label_set_text(p->lbl_lrc_mid, "Lack of Lyrics");
            lv_obj_set_style_text_color(p->lbl_lrc_mid, lv_color_hex(C_LRC_OTHER), 0);
            lv_obj_align(p->lbl_lrc_mid, LV_ALIGN_CENTER, 0, 12);
            if (p->lbl_lrc_top) lv_label_set_text(p->lbl_lrc_top, "");
            if (p->lbl_lrc_bot) lv_label_set_text(p->lbl_lrc_bot, "");
            p->lrc_index = -2;
        }
        return;
    }

    uint32_t pos = cos_spotify_audio_position_ms();
    int idx = cos_spotify_lrc_index_at(p->lrc, pos);
    if (idx < 0)
    {
        idx = 0;   /* 前奏:直接显示第一行 */
    }
    if (idx != p->lrc_index)
    {
        _lrc_goto(p, idx);
    }

    /* 水平滚动:速度 = (最大偏移) / (该行时长) */
    if (p->lrc_scroll_max > 0)
    {
        uint32_t row_elapsed = lv_tick_get() - p->lrc_row_start;
        uint32_t dur = p->lrc_row_dur ? p->lrc_row_dur : 1u;
        /* 用 90% 的行时长走完全程,末尾稍作停留 */
        uint32_t travel = (dur * 9u) / 10u;
        if (travel < 1u)
        {
            travel = 1u;
        }
        int off = (int)(((uint64_t)row_elapsed * (uint32_t)p->lrc_scroll_max) / travel);
        if (off > p->lrc_scroll_max)
        {
            off = p->lrc_scroll_max;
        }
        if (off != p->lrc_scroll)
        {
            p->lrc_scroll = off;
            lv_obj_align(p->lbl_lrc_mid, LV_ALIGN_TOP_LEFT, -off, LRC_MID_Y);
        }
    }
    else if (p->lbl_lrc_mid)
    {
        /* 不超宽:居中显示 */
        lv_obj_align(p->lbl_lrc_mid, LV_ALIGN_TOP_MID, 0, LRC_MID_Y);
    }
}

/* ── 歌名滚动 ─────────────────────────────────────────────── */

static void _update_song_title(cos_spotify_player_t *p)
{
    if (p->lbl_song == NULL)
    {
        return;
    }
    const char *txt = lv_label_get_text(p->lbl_song);
    int tw = _text_width_of(p->lbl_song, txt);
    if (tw <= TITLE_W)
    {
        lv_obj_align(p->lbl_song, LV_ALIGN_TOP_MID, 0, TITLE_Y);
        return;
    }
    /* 超宽:按固定速度滚动(歌名长度稳定,用 ~40px/s) */
    int max_off = tw - TITLE_W;
    static uint32_t last = 0;
    uint32_t now = lv_tick_get();
    if (last == 0)
    {
        last = now;
    }
    uint32_t dt = now - last;
    if (dt >= SCROLL_STEP_MS)
    {
        last = now;
        p->song_scroll += 2;   /* 每 50ms 走 2px → 40px/s */
        if (p->song_scroll > max_off + 24)
        {
            p->song_scroll = -24;   /* 循环,带一点间隔 */
        }
    }
    int off = p->song_scroll;
    if (off < 0)
    {
        off = 0;
    }
    lv_obj_align(p->lbl_song, LV_ALIGN_TOP_LEFT, 32 - off, TITLE_Y);
}

/* ── 进度条刷新 ───────────────────────────────────────────── */

static void _update_progress(cos_spotify_player_t *p)
{
    if (p->prog_fill == NULL || p->prog_track == NULL)
    {
        return;
    }
    if (p->scrubbing)
    {
        return;   /* 拖动中不覆盖用户位置 */
    }
    uint32_t dur = cos_spotify_audio_duration_ms();
    uint32_t pos = cos_spotify_audio_position_ms();
    int track_w = lv_obj_get_width(p->prog_track);
    if (track_w <= 0)
    {
        return;
    }
    int w = 0;
    if (dur > 0)
    {
        if (pos > dur)
        {
            pos = dur;
        }
        w = (int)(((uint64_t)pos * (uint32_t)track_w) / dur);
    }
    if (w < 0)
    {
        w = 0;
    }
    lv_obj_set_width(p->prog_fill, w);
}

/* ── 页面构建 ─────────────────────────────────────────────── */

static void _build_tree_screen(cos_spotify_player_t *p)
{
    p->screen_tree = lv_obj_create(p->parent);
    lv_obj_set_size(p->screen_tree, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(p->screen_tree, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(p->screen_tree, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p->screen_tree, 0, 0);
    lv_obj_set_style_pad_all(p->screen_tree, 0, 0);
    lv_obj_center(p->screen_tree);
    cos_round_clip(p->screen_tree);

    p->tree_title = _make_label(p->screen_tree, "Spotify", C_ACCENT, COS_FONT_SIZE_EXTRA_SMALL);
    lv_obj_align(p->tree_title, LV_ALIGN_TOP_MID, 0, 12);

    p->tree_list = lv_obj_create(p->screen_tree);
    lv_obj_set_size(p->tree_list, 196, 148);
    lv_obj_align(p->tree_list, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_opa(p->tree_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p->tree_list, 0, 0);
    lv_obj_set_style_pad_all(p->tree_list, 2, 0);
    lv_obj_set_style_pad_row(p->tree_list, 3, 0);
    lv_obj_set_flex_flow(p->tree_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(p->tree_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p->tree_list, LV_SCROLLBAR_MODE_AUTO);
}

/* 音量弧回调:数值 0–100 → 音频管线软件增益。 */
static void _arc_vol_changed(void *user, float value)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)user;
    (void)p;
    int vol = (int)(value + 0.5f);
    cos_spotify_audio_set_volume(vol);
    COS_LOG_I("player: volume = %d", vol);
}

/* 速度弧回调:数值 0.5–4.0x → 解码侧变速。 */
static void _arc_speed_changed(void *user, float value)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)user;
    (void)p;
    cos_spotify_audio_set_speed(value);
    COS_LOG_I("player: speed = %.2fx", (double)value);
}

static void _build_play_screen(cos_spotify_player_t *p)
{
    p->screen_play = lv_obj_create(p->parent);
    lv_obj_set_size(p->screen_play, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(p->screen_play, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(p->screen_play, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p->screen_play, 0, 0);
    lv_obj_set_style_pad_all(p->screen_play, 0, 0);
    lv_obj_center(p->screen_play);
    cos_round_clip(p->screen_play);

    /* 顶部歌名(容器 + 裁剪,避免超宽溢出) */
    lv_obj_t *title_box = lv_obj_create(p->screen_play);
    lv_obj_set_size(title_box, TITLE_W, TITLE_H);
    lv_obj_align(title_box, LV_ALIGN_TOP_MID, 0, TITLE_Y);
    lv_obj_set_style_bg_opa(title_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_box, 0, 0);
    lv_obj_set_style_pad_all(title_box, 0, 0);
    lv_obj_set_scrollbar_mode(title_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(title_box, LV_OBJ_FLAG_SCROLLABLE);

    p->lbl_song = _make_label(title_box, " ", C_TEXT, COS_FONT_SIZE_EXTRA_SMALL);
    lv_obj_align(p->lbl_song, LV_ALIGN_TOP_MID, 0, 0);

    /* 三行歌词 */
    p->lbl_lrc_top = _make_label(p->screen_play, "", C_LRC_OTHER, COS_FONT_SIZE_MICRO);
    lv_obj_set_width(p->lbl_lrc_top, LRC_W);
    lv_obj_set_style_text_align(p->lbl_lrc_top, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(p->lbl_lrc_top, LV_LABEL_LONG_CLIP);
    lv_obj_align(p->lbl_lrc_top, LV_ALIGN_TOP_MID, 0, LRC_TOP_Y);

    /* 中间行:用容器做裁剪窗,标签在其内水平平移实现滚动 */
    lv_obj_t *mid_box = lv_obj_create(p->screen_play);
    lv_obj_set_size(mid_box, LRC_W, LRC_ROW_H);
    lv_obj_align(mid_box, LV_ALIGN_TOP_MID, 0, LRC_MID_Y);
    lv_obj_set_style_bg_opa(mid_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mid_box, 0, 0);
    lv_obj_set_style_pad_all(mid_box, 0, 0);
    lv_obj_remove_flag(mid_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(mid_box, LV_SCROLLBAR_MODE_OFF);

    p->lbl_lrc_mid = _make_label(mid_box, "Lack of Lyrics", C_LRC_CUR,
                                 COS_FONT_SIZE_EXTRA_SMALL);
    lv_label_set_long_mode(p->lbl_lrc_mid, LV_LABEL_LONG_CLIP);
    lv_obj_align(p->lbl_lrc_mid, LV_ALIGN_TOP_MID, 0, 0);

    p->lbl_lrc_bot = _make_label(p->screen_play, "", C_LRC_OTHER, COS_FONT_SIZE_MICRO);
    lv_obj_set_width(p->lbl_lrc_bot, LRC_W);
    lv_obj_set_style_text_align(p->lbl_lrc_bot, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(p->lbl_lrc_bot, LV_LABEL_LONG_CLIP);
    lv_obj_align(p->lbl_lrc_bot, LV_ALIGN_TOP_MID, 0, LRC_BOT_Y);

    /* 进度条 */
    p->prog_track = lv_obj_create(p->screen_play);
    lv_obj_set_size(p->prog_track, PROG_W, PROG_H);
    lv_obj_align(p->prog_track, LV_ALIGN_TOP_MID, 0, PROG_Y);
    lv_obj_set_style_bg_color(p->prog_track, lv_color_hex(C_BAR_BG), 0);
    lv_obj_set_style_bg_opa(p->prog_track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p->prog_track, PROG_H / 2, 0);
    lv_obj_set_style_border_width(p->prog_track, 0, 0);
    lv_obj_set_style_pad_all(p->prog_track, 0, 0);
    lv_obj_add_flag(p->prog_track, LV_OBJ_FLAG_CLICKABLE);
    /* 触摸热区上下扩展,便于圆屏点按 */
    lv_obj_set_ext_click_area(p->prog_track, 12);
    lv_obj_add_event_cb(p->prog_track, _progress_event_cb, LV_EVENT_ALL, p);

    p->prog_fill = lv_obj_create(p->prog_track);
    lv_obj_set_height(p->prog_fill, PROG_H);
    lv_obj_set_width(p->prog_fill, 0);
    lv_obj_align(p->prog_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(p->prog_fill, lv_color_hex(C_BAR_FG), 0);
    lv_obj_set_style_bg_opa(p->prog_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p->prog_fill, PROG_H / 2, 0);
    lv_obj_set_style_border_width(p->prog_fill, 0, 0);
    lv_obj_set_style_pad_all(p->prog_fill, 0, 0);
    lv_obj_remove_flag(p->prog_fill, LV_OBJ_FLAG_CLICKABLE);

    /* 控制按钮:⏮ ⏪15 ⏯ ⏩15 ⏭ */
    const char *txts[5] = {"<<", "-15", "||", "+15", ">>"};
    lv_obj_t *btns[5] = {NULL};
    for (int i = 0; i < 5; i++)
    {
        btns[i] = _make_round_btn(p->screen_play, txts[i], CTRL_BTN_W, CTRL_BTN_H,
                                  (i == 2) ? C_ACCENT : C_BTN);
    }
    int total = 5 * CTRL_BTN_W + 4 * CTRL_GAP;
    int x0 = (SCR_W - total) / 2;
    for (int i = 0; i < 5; i++)
    {
        lv_obj_align(btns[i], LV_ALIGN_TOP_LEFT,
                     x0 + i * (CTRL_BTN_W + CTRL_GAP), CTRL_Y);
    }
    p->btn_prev = btns[0];
    p->btn_rew = btns[1];
    p->btn_play = btns[2];
    p->btn_ff = btns[3];
    p->btn_next = btns[4];

    lv_obj_add_event_cb(p->btn_prev, _btn_prev_cb, LV_EVENT_CLICKED, p);
    lv_obj_add_event_cb(p->btn_rew, _btn_rew_cb, LV_EVENT_CLICKED, p);
    lv_obj_add_event_cb(p->btn_play, _btn_play_cb, LV_EVENT_CLICKED, p);
    lv_obj_add_event_cb(p->btn_ff, _btn_ff_cb, LV_EVENT_CLICKED, p);
    lv_obj_add_event_cb(p->btn_next, _btn_next_cb, LV_EVENT_CLICKED, p);

    /* 左上角返回文件树 */
    p->btn_back = lv_button_create(p->screen_play);
    lv_obj_set_size(p->btn_back, 30, 30);
    lv_obj_align(p->btn_back, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_set_style_radius(p->btn_back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p->btn_back, lv_color_hex(C_BTN), 0);
    lv_obj_set_style_bg_opa(p->btn_back, LV_OPA_70, 0);
    lv_obj_set_style_border_width(p->btn_back, 0, 0);
    lv_obj_set_style_shadow_width(p->btn_back, 0, 0);
    lv_obj_t *bl = lv_label_create(p->btn_back);
    lv_label_set_text(bl, "<");
    lv_obj_set_style_text_color(bl, lv_color_hex(C_TEXT), 0);
    _set_font(bl, COS_FONT_SIZE_MICRO);
    lv_obj_center(bl);
    lv_obj_add_event_cb(p->btn_back, _btn_back_cb, LV_EVENT_CLICKED, p);

    /* ── 弧形调节条(Batch 4) ──
     * 右侧 = 音量(0–100,默认 50)
     * 左侧 = 播放速度(0.5x–4.0x,默认 1.0x)
     * 初始值从系统配置/音频管线读取,保证与上次设定一致。 */
    p->arc_vol = cos_spotify_arc_create(p->screen_play,
                                        true,                    /* right */
                                        0.0f, 100.0f, 50.0f,
                                        _arc_vol_changed, p);
    if (p->arc_vol)
    {
        cos_spotify_arc_set_value(p->arc_vol, (float)cos_spotify_audio_get_volume());
    }

    p->arc_speed = cos_spotify_arc_create(p->screen_play,
                                          false,                 /* left */
                                          0.5f, 4.0f, 1.0f,
                                          _arc_speed_changed, p);
    if (p->arc_speed)
    {
        cos_spotify_arc_set_value(p->arc_speed, cos_spotify_audio_get_speed());
    }

    lv_obj_add_flag(p->screen_play, LV_OBJ_FLAG_HIDDEN);
}

/* ── 对外 API ─────────────────────────────────────────────── */

cos_spotify_player_t *cos_spotify_player_create(lv_obj_t *parent,
                                               cos_spotify_back_cb_t on_back,
                                               cos_spotify_exit_cb_t on_exit,
                                               void *user)
{
    if (parent == NULL)
    {
        return NULL;
    }

    cos_spotify_player_t *p = (cos_spotify_player_t *)cos_malloc(sizeof(*p));
    if (p == NULL)
    {
        return NULL;
    }
    memset(p, 0, sizeof(*p));
    p->parent = parent;
    p->on_back = on_back;
    p->on_exit = on_exit;
    p->user = user;
    p->lrc_index = -1;

    /* 文件树:懒加载。
     * 刻意**不做预展开** —— 与 Texthub FM 行为一致:
     * 初始只显示 /sdcard/spotify 的第一层,用户点开哪个目录才读哪个目录,
     * 收起时立即释放子树。这样打开 App 只有 1 次 SD 目录读取。 */
    p->tree = cos_spotify_tree_create(NULL);
    if (p->tree == NULL)
    {
        cos_free(p);
        return NULL;
    }

    _build_tree_screen(p);
    _build_play_screen(p);
    _tree_rebuild(p);
    _show_tree(p);

    COS_LOG_I("player: created (%d songs)", cos_spotify_tree_song_count(p->tree));
    return p;
}

void cos_spotify_player_destroy(cos_spotify_player_t *p)
{
    if (p == NULL)
    {
        return;
    }

    /* 步骤 1-2:停止解码循环并清空 PCM 环形缓冲(在 audio_close 内完成) */
    cos_spotify_audio_close();

    /* 弧形控件:释放内部结构(LVGL 对象随父 screen 回收) */
    if (p->arc_vol)
    {
        cos_spotify_arc_destroy(p->arc_vol);
        p->arc_vol = NULL;
    }
    if (p->arc_speed)
    {
        cos_spotify_arc_destroy(p->arc_speed);
        p->arc_speed = NULL;
    }

    if (p->lrc)
    {
        cos_spotify_lrc_free(p->lrc);
    }
    if (p->tree)
    {
        cos_spotify_tree_destroy(p->tree);
    }
    if (p->cur_path)
    {
        cos_free(p->cur_path);
    }
    /* LVGL 对象由 activity 销毁父容器时统一回收 */
    cos_free(p);
}

void cos_spotify_player_play_file(cos_spotify_player_t *p, const char *path)
{
    if (p && path)
    {
        _play_begin(p, path);
    }
}

bool cos_spotify_player_is_playing(const cos_spotify_player_t *p)
{
    return p ? p->playing : false;
}

bool cos_spotify_player_show_browser(cos_spotify_player_t *p)
{
    if (p == NULL || p->screen_play == NULL)
    {
        return false;
    }
    if (lv_obj_has_flag(p->screen_play, LV_OBJ_FLAG_HIDDEN))
    {
        return false;   /* 已在文件树页 */
    }
    _show_tree(p);
    return true;
}

void cos_spotify_player_tick(cos_spotify_player_t *p)
{
    if (p == NULL)
    {
        return;
    }

    /* 延迟的文件树重建(展开/收起后)。此时上一轮事件分发已结束,
     * 可以安全地删除旧行对象。 */
    if (p->tree_dirty)
    {
        p->tree_dirty = false;
        _tree_rebuild(p);
    }

    /* 弧形调节的长按计时:无论是否在播放都要跑,否则暂停时长按会失灵 */
    if (p->arc_vol)
    {
        cos_spotify_arc_tick(p->arc_vol);
    }
    if (p->arc_speed)
    {
        cos_spotify_arc_tick(p->arc_speed);
    }

    if (!p->playing)
    {
        return;
    }

    /* 1) 解码分片 → 环形缓冲;2) 环形缓冲 → UAC 等时端点 */
    if (!p->paused)
    {
        cos_spotify_audio_pump();
        cos_spotify_audio_flush_to_uac();
    }

    /* 3) 曲末自动下一首 */
    if (cos_spotify_audio_cos())
    {
        COS_LOG_I("player: track end, next");
        int next = p->sibling_idx + 1;
        if (next >= p->sibling_count)
        {
            next = 0;
        }
        _play_index(p, next);
        return;
    }

    /* 4) 歌词 + 歌名 + 进度 */
    _update_lyrics(p);
    _update_song_title(p);
    _update_progress(p);
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
