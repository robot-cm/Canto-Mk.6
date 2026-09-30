/*
 * eos_round_keyboard.c — 圆形屏半圆键盘
 *
 * 适配 240x240 圆屏, 键盘占屏幕下半部分半圆:
 *   - 键区为半圆内嵌矩形(不铺满半圆), 4 行 x 5 列
 *   - 矩形左右两侧各一个翻页箭头(查看 QWERTY/符号 的左右两页)
 *   - 右箭头下方是输入法切换键(英文→符号→数字 循环)
 *   - 支持 EN(两页) / SYM(两页) / NUM(单页) 三种输入法
 */

#include "eos_round_keyboard.h"

#include "eos_theme.h"
#include "eos_font.h"
#include "eos_mem.h"
#include "eos_log.h"

#include <string.h>
#include <stdio.h>

#define EOS_LOG_TAG "RoundKB"

/* ── 布局常量(240x240 圆屏, 键盘占下半圆 y=120~240) ── */
#define _RKB_H      120  /* 键盘总高(下半屏) */
#define _AREA_W     148  /* 键区矩形宽 */
#define _AREA_H     88   /* 键区矩形高 */
#define _AREA_X     46   /* 键区 x=(240-148)/2 */
#define _AREA_Y     8    /* 键区距键盘顶部 */
#define _KEY_W      27   /* 字母/数字/符号键宽 */
#define _KEY_H      18   /* 字母/数字/符号键高 */
#define _KEY_GAP    2
#define _ROW_H      (_KEY_H + _KEY_GAP)      /* 字母行行高 20 */
#define _CTRL_Y     66                       /* 第4行(控制/候选) y */
#define _CTRL_H     22                       /* 第4行键高 */
#define _ARROW_W    18
#define _ARROW_H    18
#define _ARROW_L_X  22   /* 左箭头 x */
#define _ARROW_R_X  196  /* 右箭头/模式键 x */
#define _ARROW_Y    32   /* 箭头 y */
#define _MODE_Y     58   /* 模式键 y(右箭头下方) */

/* ── 键定义: EN 两页 / 数字 / 符号两页 ── */
static const char *const _EN_P0[15] = {"q", "w", "e", "r", "t", "a", "s", "d", "f", "g", "z", "x", "c", "v", "b"};
static const char *const _EN_P1[15] = {"y", "u", "i", "o", "p", "h", "j", "k", "l", "'", "n", "m", "@", ".", "-"};
static const char *const _NUM[14]    = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", ".", "-", "_", "@"};
static const char *const _SYM_P0[15] = {"!", "@", "#", "$", "%", "&", "*", "(", ")", "/", "{", "}", "[", "]", "<"};
static const char *const _SYM_P1[15] = {"?", "+", "=", "-", "_", ";", ":", "'", "\"", ",", ".", "^", "~", "|", "\\"};
/* SYM 第 3 页: 方程/不等式运算专用(math page) */
static const char *const _SYM_P2[15] = {"<", ">", "<=", ">=", "=", "(", ")", "{", "}", "[", "]", "^", "_", "+", "-"};

/* 控制键标记 */
#define _CTRL_BACK  "@back"
#define _CTRL_SPACE "@space"
#define _CTRL_OK    "@ok"

typedef struct
{
    lv_obj_t *key_area;
    lv_obj_t *left_arrow;
    lv_obj_t *right_arrow;
    lv_obj_t *mode_btn;
    lv_obj_t *ta;
    eos_rkb_mode_t mode;
    uint8_t page;
} rkb_t;

/* 前向声明(重建函数早于回调定义) */
static void _rkb_char_cb(lv_event_t *e);
static void _rkb_ctrl_cb(lv_event_t *e);

/* ── 工具函数 ── */

static rkb_t *_ctx_from_key(lv_obj_t *key)
{
    if (!key)
        return NULL;
    lv_obj_t *area = lv_obj_get_parent(key);  /* key_area */
    lv_obj_t *root = area ? lv_obj_get_parent(area) : NULL;
    return root ? (rkb_t *)lv_obj_get_user_data(root) : NULL;
}

static const char *_mode_text(eos_rkb_mode_t mode)
{
    switch (mode)
    {
    case EOS_RKB_MODE_SYM: return "#";
    case EOS_RKB_MODE_NUM: return "1";
    default: return "A";
    }
}

static lv_obj_t *_rkb_make_key_at(lv_obj_t *area, int x, int y, int w, int h,
                                  const char *text, lv_event_cb_t cb, const void *ud,
                                  bool cjk)
{
    lv_obj_t *btn = lv_button_create(area);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2F38), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_user_data(btn, (void *)ud);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (cjk)
    {
        /* 中文候选: 20→18→16px 再收小 2, 仍用圆角裁剪收边 */
        eos_label_set_font_size(label, EOS_FONT_SIZE_EXTRA_SMALL);
        lv_obj_set_style_clip_corner(btn, true, 0);
    }
    else
    {
        /* 字母/数字/符号: 默认 26px → 22px → 20px 再收小 2
         * 复合符号(如 "<=" ">=")字号再收窄, 避免溢出按键 */
        eos_label_set_font_size(label, (strlen(text) >= 2)
                                        ? EOS_FONT_SIZE_EXTRA_SMALL
                                        : EOS_FONT_SIZE_SMALL);
    }
    return btn;
}

/* ── 第4行: 控制键 ── */

static void _rkb_make_ctrl_row(rkb_t *ctx)
{
    /* [⌫ 32] [空格 48] [确定 32] 居中 */
    int x = (_AREA_W - 112) / 2;
    _rkb_make_key_at(ctx->key_area, x, _CTRL_Y, 32, _CTRL_H, LV_SYMBOL_BACKSPACE,
                     _rkb_ctrl_cb, _CTRL_BACK, false);
    _rkb_make_key_at(ctx->key_area, x + 34, _CTRL_Y, 48, _CTRL_H, "_",
                     _rkb_ctrl_cb, _CTRL_SPACE, false);
    _rkb_make_key_at(ctx->key_area, x + 84, _CTRL_Y, 32, _CTRL_H, LV_SYMBOL_OK,
                     _rkb_ctrl_cb, _CTRL_OK, false);
}

/* ── 重建键区 ── */

/* 每个模式包含的页数(用于翻页箭头循环) */
static int _rkb_mode_pages(eos_rkb_mode_t mode)
{
    if (mode == EOS_RKB_MODE_SYM) return 3; /* 第 3 页为方程/不等式运算页 */
    if (mode == EOS_RKB_MODE_NUM) return 1;
    return 2; /* EN 两页 */
}

static void _rkb_rebuild(rkb_t *ctx)
{
    lv_obj_clean(ctx->key_area);
    const char *const *keys = NULL;
    int count = 0;
    int i;

    switch (ctx->mode)
    {
    case EOS_RKB_MODE_EN:
        keys = ctx->page ? _EN_P1 : _EN_P0;
        count = 15;
        break;
    case EOS_RKB_MODE_SYM:
        if (ctx->page == 1)
            keys = _SYM_P1;
        else if (ctx->page == 2)
            keys = _SYM_P2;
        else
            keys = _SYM_P0;
        count = 15;
        break;
    case EOS_RKB_MODE_NUM:
        keys = _NUM;
        count = 14;
        break;
    }

    for (i = 0; i < count; i++)
    {
        _rkb_make_key_at(ctx->key_area,
                         2 + (i % 5) * (_KEY_W + _KEY_GAP),
                         4 + (i / 5) * _ROW_H,
                         _KEY_W, _KEY_H, keys[i], _rkb_char_cb, keys[i], false);
    }

    _rkb_make_ctrl_row(ctx);
}

/* ── 箭头禁用状态(数字模式单页无翻页) ── */

static void _rkb_update_arrows(rkb_t *ctx)
{
    if (ctx->mode == EOS_RKB_MODE_NUM)
    {
        lv_obj_add_state(ctx->left_arrow, LV_STATE_DISABLED);
        lv_obj_add_state(ctx->right_arrow, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(ctx->left_arrow, LV_STATE_DISABLED);
        lv_obj_remove_state(ctx->right_arrow, LV_STATE_DISABLED);
    }
}

/* ── 事件回调 ── */

static void _rkb_char_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *s = lv_obj_get_user_data(btn);
    rkb_t *ctx = _ctx_from_key(btn);
    if (!s || !ctx || !ctx->ta)
        return;

    lv_textarea_add_text(ctx->ta, s);
}

static void _rkb_ctrl_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *tag = lv_obj_get_user_data(btn);
    rkb_t *ctx = _ctx_from_key(btn);
    if (!tag || !ctx)
        return;

    if (strcmp(tag, _CTRL_BACK) == 0)
    {
        if (ctx->ta)
        {
            lv_textarea_delete_char(ctx->ta);
        }
    }
    else if (strcmp(tag, _CTRL_SPACE) == 0)
    {
        if (ctx->ta)
            lv_textarea_add_char(ctx->ta, ' ');
    }
    else if (strcmp(tag, _CTRL_OK) == 0)
    {
        /* 通知宿主: 键盘"确定"被按下 */
        if (ctx->ta)
            lv_obj_send_event(ctx->ta, LV_EVENT_READY, NULL);
    }
}

static void _rkb_arrow_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(btn);
    rkb_t *ctx = root ? (rkb_t *)lv_obj_get_user_data(root) : NULL;
    if (!ctx || ctx->mode == EOS_RKB_MODE_NUM)
        return;
    int npages = _rkb_mode_pages(ctx->mode);
    /* 左箭头(user_data==1)上一页, 右箭头下一页, 循环翻页 */
    if ((intptr_t)lv_obj_get_user_data(btn) == 1)
        ctx->page = (ctx->page - 1 + npages) % npages;
    else
        ctx->page = (ctx->page + 1) % npages;
    _rkb_rebuild(ctx);
}

static void _rkb_mode_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *root = lv_obj_get_parent(btn);
    rkb_t *ctx = root ? (rkb_t *)lv_obj_get_user_data(root) : NULL;
    if (!ctx)
        return;
    ctx->mode = (eos_rkb_mode_t)(((int)ctx->mode + 1) % 3);
    ctx->page = 0;

    lv_obj_t *l = lv_obj_get_child(ctx->mode_btn, 0);
    if (l)
        lv_label_set_text(l, _mode_text(ctx->mode));

    _rkb_update_arrows(ctx);
    _rkb_rebuild(ctx);
}

static void _rkb_delete_cb(lv_event_t *e)
{
    lv_obj_t *root = lv_event_get_target(e);
    rkb_t *ctx = (rkb_t *)lv_obj_get_user_data(root);
    if (ctx)
        eos_free(ctx);
}

/* ── 公共 API ── */

lv_obj_t *eos_round_keyboard_create(lv_obj_t *parent)
{
    rkb_t *ctx = eos_malloc_zeroed(sizeof(rkb_t));
    if (!ctx)
        return NULL;
    ctx->mode = EOS_RKB_MODE_EN;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, 240, _RKB_H);
    lv_obj_align(root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_user_data(root, ctx);
    lv_obj_add_event_cb(root, _rkb_delete_cb, LV_EVENT_DELETE, NULL);

    /* 键区矩形: 深灰, 半圆内嵌 */
    ctx->key_area = lv_obj_create(root);
    lv_obj_set_size(ctx->key_area, _AREA_W, _AREA_H);
    lv_obj_set_pos(ctx->key_area, _AREA_X, _AREA_Y);
    lv_obj_remove_flag(ctx->key_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ctx->key_area, lv_color_hex(0x191D24), 0);
    lv_obj_set_style_radius(ctx->key_area, 14, 0);
    lv_obj_set_style_border_width(ctx->key_area, 0, 0);
    lv_obj_set_style_pad_all(ctx->key_area, 0, 0);

    /* 左右翻页箭头 + 输入法切换键 */
    ctx->left_arrow = lv_button_create(root);
    lv_obj_set_pos(ctx->left_arrow, _ARROW_L_X, _ARROW_Y);
    lv_obj_set_size(ctx->left_arrow, _ARROW_W, _ARROW_H);
    lv_obj_set_style_bg_color(ctx->left_arrow, lv_color_hex(0x2A2F38), 0);
    lv_obj_set_style_radius(ctx->left_arrow, 6, 0);
    lv_obj_set_style_border_width(ctx->left_arrow, 0, 0);
    lv_obj_set_style_pad_all(ctx->left_arrow, 0, 0);
    lv_obj_set_style_text_color(ctx->left_arrow, lv_color_white(), 0);
    lv_obj_set_user_data(ctx->left_arrow, (void *)(intptr_t)1);
    lv_obj_add_event_cb(ctx->left_arrow, _rkb_arrow_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_arrow = lv_label_create(ctx->left_arrow);
    lv_label_set_text(l_arrow, LV_SYMBOL_LEFT);
    lv_obj_center(l_arrow);

    ctx->right_arrow = lv_button_create(root);
    lv_obj_set_pos(ctx->right_arrow, _ARROW_R_X, _ARROW_Y);
    lv_obj_set_size(ctx->right_arrow, _ARROW_W, _ARROW_H);
    lv_obj_set_style_bg_color(ctx->right_arrow, lv_color_hex(0x2A2F38), 0);
    lv_obj_set_style_radius(ctx->right_arrow, 6, 0);
    lv_obj_set_style_border_width(ctx->right_arrow, 0, 0);
    lv_obj_set_style_pad_all(ctx->right_arrow, 0, 0);
    lv_obj_set_style_text_color(ctx->right_arrow, lv_color_white(), 0);
    lv_obj_set_user_data(ctx->right_arrow, (void *)(intptr_t)2);
    lv_obj_add_event_cb(ctx->right_arrow, _rkb_arrow_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *r_arrow = lv_label_create(ctx->right_arrow);
    lv_label_set_text(r_arrow, LV_SYMBOL_RIGHT);
    lv_obj_center(r_arrow);

    ctx->mode_btn = lv_button_create(root);
    lv_obj_set_pos(ctx->mode_btn, _ARROW_R_X, _MODE_Y);
    lv_obj_set_size(ctx->mode_btn, _ARROW_W, 20);
    lv_obj_set_style_bg_color(ctx->mode_btn, lv_color_hex(0x2A2F38), 0);
    lv_obj_set_style_radius(ctx->mode_btn, 6, 0);
    lv_obj_set_style_border_width(ctx->mode_btn, 0, 0);
    lv_obj_set_style_pad_all(ctx->mode_btn, 0, 0);
    lv_obj_set_style_text_color(ctx->mode_btn, lv_color_white(), 0);
    lv_obj_set_style_clip_corner(ctx->mode_btn, true, 0);
    lv_obj_add_event_cb(ctx->mode_btn, _rkb_mode_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *m_label = lv_label_create(ctx->mode_btn);
    lv_label_set_text(m_label, "A");
    eos_label_set_font_size(m_label, EOS_FONT_SIZE_EXTRA_SMALL);
    lv_obj_center(m_label);

    _rkb_update_arrows(ctx);
    _rkb_rebuild(ctx);
    return root;
}

void eos_round_keyboard_set_textarea(lv_obj_t *kb, lv_obj_t *ta)
{
    rkb_t *ctx = kb ? (rkb_t *)lv_obj_get_user_data(kb) : NULL;
    if (ctx)
        ctx->ta = ta;
}

void eos_round_keyboard_set_mode(lv_obj_t *kb, eos_rkb_mode_t mode)
{
    rkb_t *ctx = kb ? (rkb_t *)lv_obj_get_user_data(kb) : NULL;
    if (!ctx || mode > EOS_RKB_MODE_NUM)
        return;
    ctx->mode = mode;
    ctx->page = 0;
    lv_obj_t *l = lv_obj_get_child(ctx->mode_btn, 0);
    if (l)
        lv_label_set_text(l, _mode_text(mode));
    _rkb_update_arrows(ctx);
    _rkb_rebuild(ctx);
}

eos_rkb_mode_t eos_round_keyboard_get_mode(lv_obj_t *kb)
{
    rkb_t *ctx = kb ? (rkb_t *)lv_obj_get_user_data(kb) : NULL;
    return ctx ? ctx->mode : EOS_RKB_MODE_EN;
}

void eos_round_keyboard_send_key(lv_obj_t *kb, const char *key)
{
    rkb_t *ctx = kb ? (rkb_t *)lv_obj_get_user_data(kb) : NULL;
    if (!ctx || !key || !ctx->ta)
        return;
    if (strcmp(key, "@back") == 0)
        lv_textarea_delete_char(ctx->ta);
    else if (strcmp(key, "@space") == 0)
        lv_textarea_add_char(ctx->ta, ' ');
    else
        lv_textarea_add_text(ctx->ta, key);
}
