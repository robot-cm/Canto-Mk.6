/**
 * @file cos_keyboard.c
 * @brief System keyboard widget (English QWERTY + Chinese pinyin IME).
 */

#include "cos_keyboard.h"

#include <string.h>
#include "services/ime/cos_pinyin.h"

/* Key string constants --------------------------------------*/
#define KBK_BACK   "BACK"
#define KBK_ENTER  "ENTER"
#define KBK_MODE   "MODE"
#define KBK_SPACE  " "

#define KBK_PY_MAX   24
#define KBK_CAND_MAX 9

/* Widget context --------------------------------------------*/
typedef struct
{
    lv_obj_t *ta;                 /* bound textarea */
    lv_obj_t *cand_label;         /* pinyin display (ZH mode) */
    lv_obj_t *num_btns[KBK_CAND_MAX];
    lv_obj_t *mode_btn;
    cos_keyboard_mode_t mode;
    char      pybuf[KBK_PY_MAX];
    int       py_len;
    const char *cands[KBK_CAND_MAX];
    int       cand_count;
} cos_keyboard_ctx_t;

/* Helpers ----------------------------------------------------*/

static void _insert_text(lv_obj_t *ta, const char *txt)
{
    /* LVGL's textarea does not act on LV_EVENT_INSERT itself (that event is
     * only for user interception). The real insertion API is
     * lv_textarea_add_text(), which is what LVGL's own keyboard widget calls.
     * It works regardless of focus, so it is correct for both touch and the
     * headless self-test. */
    if (ta && txt)
        lv_textarea_add_text(ta, txt);
}

static void _clear_candidates(cos_keyboard_ctx_t *c)
{
    /* EN mode: number buttons become plain digit keys (1..9). */
    c->cand_count = 0;
    for (int i = 0; i < KBK_CAND_MAX; i++)
    {
        if (c->num_btns[i])
        {
            lv_obj_t *lbl = lv_obj_get_child(c->num_btns[i], 0);
            if (lbl)
            {
                char dig[2] = { (char)('1' + i), '\0' };
                lv_label_set_text(lbl, dig);
            }
            lv_obj_clear_state(c->num_btns[i], LV_STATE_DISABLED);
        }
    }
}

static void _update_candidates(cos_keyboard_ctx_t *c)
{
    /* refresh number-button labels from the pinyin buffer */
    if (c->mode != COS_KB_MODE_ZH)
        return;

    c->cand_count = 0;
    if (c->py_len > 0)
    {
        c->pybuf[c->py_len] = '\0';
        c->cand_count = cos_pinyin_lookup(c->pybuf, c->cands,
                                          KBK_CAND_MAX, &c->cand_count);
        if (c->cand_count > KBK_CAND_MAX)
            c->cand_count = KBK_CAND_MAX;
    }

    lv_label_set_text(c->cand_label, c->py_len > 0 ? c->pybuf : "");

    for (int i = 0; i < KBK_CAND_MAX; i++)
    {
        lv_obj_t *btn = c->num_btns[i];
        if (!btn)
            continue;
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (i < c->cand_count)
        {
            lv_label_set_text(lbl, c->cands[i]);
            lv_obj_clear_state(btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_label_set_text(lbl, "");
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
    }
}

static void _commit_candidate(cos_keyboard_ctx_t *c, int idx)
{
    if (c->mode != COS_KB_MODE_ZH)
        return;
    if (idx < 0 || idx >= c->cand_count)
        return;
    _insert_text(c->ta, c->cands[idx]);
    c->py_len = 0;
    _update_candidates(c);
}

static void _btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    const char *key = (const char *)lv_obj_get_user_data(btn);
    lv_obj_t *kb = lv_event_get_user_data(e); /* set on the button's event */
    if (key && kb)
        cos_keyboard_send_key(kb, key);
}

static lv_obj_t *_make_btn(lv_obj_t *row, const char *label, const char *key,
                           lv_obj_t *kb, int w)
{
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, w, 22);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    lv_obj_set_user_data(btn, (void *)key);
    lv_obj_add_event_cb(btn, _btn_event_cb, LV_EVENT_CLICKED, kb);
    return btn;
}

/* Public API -------------------------------------------------*/

lv_obj_t *cos_keyboard_create(lv_obj_t *parent)
{
    cos_keyboard_ctx_t *c = lv_malloc(sizeof(cos_keyboard_ctx_t));
    memset(c, 0, sizeof(*c));
    c->mode = COS_KB_MODE_EN;

    lv_obj_t *kb = lv_obj_create(parent);
    lv_obj_set_size(kb, 232, 152);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_flex_flow(kb, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(kb, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(kb, 8, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_90, 0);
    lv_obj_set_user_data(kb, c);

    /* candidate / pinyin label (ZH) */
    c->cand_label = lv_label_create(kb);
    lv_label_set_text(c->cand_label, "");
    lv_obj_set_style_text_font(c->cand_label, LV_FONT_DEFAULT, 0);

    /* number / candidate row */
    lv_obj_t *num_row = lv_obj_create(kb);
    lv_obj_set_size(num_row, 224, 24);
    lv_obj_set_flex_flow(num_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(num_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(num_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(num_row, LV_OPA_TRANSP, 0);
    for (int i = 0; i < KBK_CAND_MAX; i++)
    {
        char dig[2] = { (char)('1' + i), '\0' };
        lv_obj_t *b = _make_btn(num_row, " ", dig, kb, 22);
        c->num_btns[i] = b;
        lv_obj_add_state(b, LV_STATE_DISABLED);
    }

    /* letter rows */
    const char *rows[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
    for (int r = 0; r < 3; r++)
    {
        lv_obj_t *row = lv_obj_create(kb);
        lv_obj_set_size(row, 224, 24);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
        const char *s = rows[r];
        for (size_t i = 0; i < strlen(s); i++)
        {
            char ch[2] = { s[i], '\0' };
            _make_btn(row, ch, ch, kb, 20);
        }
    }

    /* control row */
    lv_obj_t *ctrl = lv_obj_create(kb);
    lv_obj_set_size(ctrl, 224, 24);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(ctrl, LV_OPA_TRANSP, 0);
    c->mode_btn = _make_btn(ctrl, "中英", KBK_MODE, kb, 40); /* toggles mode (label updates) */
    _make_btn(ctrl, "空格", KBK_SPACE, kb, 52);
    _make_btn(ctrl, "⌫", KBK_BACK, kb, 36);
    _make_btn(ctrl, "↵", KBK_ENTER, kb, 36);

    cos_keyboard_set_mode(kb, COS_KB_MODE_EN);
    return kb;
}

void cos_keyboard_set_textarea(lv_obj_t *kb, lv_obj_t *ta)
{
    cos_keyboard_ctx_t *c = lv_obj_get_user_data(kb);
    if (c)
        c->ta = ta;
}

void cos_keyboard_set_mode(lv_obj_t *kb, cos_keyboard_mode_t mode)
{
    cos_keyboard_ctx_t *c = lv_obj_get_user_data(kb);
    if (!c)
        return;
    c->mode = mode;
    c->py_len = 0;
    if (c->mode_btn)
    {
        lv_obj_t *lbl = lv_obj_get_child(c->mode_btn, 0);
        if (lbl)
            lv_label_set_text(lbl, mode == COS_KB_MODE_EN ? "中" : "EN");
    }
    if (mode == COS_KB_MODE_ZH)
        _update_candidates(c);
    else
        _clear_candidates(c);
}

cos_keyboard_mode_t cos_keyboard_get_mode(lv_obj_t *kb)
{
    cos_keyboard_ctx_t *c = lv_obj_get_user_data(kb);
    return c ? c->mode : COS_KB_MODE_EN;
}

void cos_keyboard_send_key(lv_obj_t *kb, const char *key)
{
    cos_keyboard_ctx_t *c = lv_obj_get_user_data(kb);
    if (!c || !key)
        return;

    if (strcmp(key, KBK_MODE) == 0)
    {
        cos_keyboard_set_mode(kb, c->mode == COS_KB_MODE_EN
                                     ? COS_KB_MODE_ZH : COS_KB_MODE_EN);
        return;
    }

    if (c->mode == COS_KB_MODE_ZH)
    {
        if (strcmp(key, KBK_BACK) == 0)
        {
            if (c->py_len > 0)
            {
                c->py_len--;
                _update_candidates(c);
            }
            else
            {
                lv_textarea_delete_char(c->ta);
            }
            return;
        }
        if (strcmp(key, KBK_ENTER) == 0)
        {
            if (c->ta)
                lv_obj_send_event(c->ta, LV_EVENT_READY, NULL);
            return;
        }
        if (strcmp(key, KBK_SPACE) == 0)
        {
            if (c->cand_count > 0)
                _commit_candidate(c, 0);
            else
                _insert_text(c->ta, " ");
            return;
        }
        /* digit -> select candidate */
        if (key[0] >= '1' && key[0] <= '9' && key[1] == '\0')
        {
            int idx = key[0] - '1';
            if (c->cand_count > 0)
                _commit_candidate(c, idx);
            else
                _insert_text(c->ta, key); /* no candidates: insert digit */
            return;
        }
        /* letter -> accumulate pinyin */
        if (strlen(key) == 1 && ((key[0] >= 'a' && key[0] <= 'z') ||
                                 (key[0] >= 'A' && key[0] <= 'Z')))
        {
            if (c->py_len < (int)sizeof(c->pybuf) - 1)
            {
                c->pybuf[c->py_len++] = key[0];
                _update_candidates(c);
            }
            return;
        }
        return;
    }

    /* ---- EN mode ---- */
    if (strcmp(key, KBK_BACK) == 0)
    {
        lv_textarea_delete_char(c->ta);
    }
    else if (strcmp(key, KBK_ENTER) == 0)
    {
        if (c->ta)
            lv_obj_send_event(c->ta, LV_EVENT_READY, NULL);
    }
    else if (strcmp(key, KBK_SPACE) == 0)
    {
        _insert_text(c->ta, " ");
    }
    else if (strlen(key) == 1)
    {
        _insert_text(c->ta, key);
    }
}
