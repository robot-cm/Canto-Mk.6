/**
 * @file cos_eqsolver.c
 * @brief Equation Solver native C app (com.cantomk6.eqsolver) for Canto Mk.6.
 *
 * Round 240x240 UI: an input screen to add/edit equations, a solve action that
 * runs the self-contained solver, and a result screen. Equation editing reuses
 * the existing round keyboard + textarea (the same IME the Dictionary app uses).
 *
 * Exits cleanly via swipe back; no FreeRTOS tasks, no extra memory pools beyond
 * small stack buffers.
 */

#include "cos_eqsolver.h"
#include "cos_eqsolver_parser.h"
#include "cos_eqsolver_solver.h"

#include "cos_activity.h"
#include "cos_round_clip.h"
#include "cos_font.h"
#include "cos_theme.h"
#include "cos_basic_widgets.h"
#include "cos_round_keyboard.h"

#include "cos_mem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef COS_APP_EQSOLVER_ENABLED

#define MAX_EQS EQSOLVER_MAX_EQS
#define MAX_EQ_LEN 64
#define ROW_TXT_MAX 29

typedef enum
{
    SCR_INPUT,
    SCR_RESULT
} eq_screen_t;

typedef struct
{
    cos_activity_t *activity;
    lv_obj_t *cont; /* content container inside the round-clipped view */
    char eqs[MAX_EQS][MAX_EQ_LEN];
    int eq_count;
    int edit_index; /* index being edited, or == eq_count when adding */
    eq_screen_t screen;
    char result_buf[1024];
} eqsolver_ctx_t;

/* Equation editor sub-activity context. */
typedef struct
{
    cos_activity_t *activity;
    eqsolver_ctx_t *parent;
    int edit_index;
    lv_obj_t *textarea;
} eq_editor_ctx_t;

/* Single-instance shortcut (only one app instance can run at a time). */
static eqsolver_ctx_t *g_ctx = NULL;

/* ---------------- forward declarations ---------------- */
static void _on_enter(cos_activity_t *act);
static void _on_destroy(cos_activity_t *act);
static bool _on_swipe_back(cos_activity_t *act, lv_dir_t dir);

static void _editor_on_enter(cos_activity_t *act);
static void _editor_on_destroy(cos_activity_t *act);
static bool _editor_on_swipe_back(cos_activity_t *act, lv_dir_t dir);

static void _build_input(eqsolver_ctx_t *ctx);
static void _build_result(eqsolver_ctx_t *ctx);
static void _clear_cont(eqsolver_ctx_t *ctx);
static void _open_editor(eqsolver_ctx_t *ctx, int index);
static void _row_clicked_cb(lv_event_t *e);
static void _add_clicked_cb(lv_event_t *e);
static void _solve_clicked_cb(lv_event_t *e);
static void _back_clicked_cb(lv_event_t *e);
static void _editor_save_cb(lv_event_t *e);
static void _editor_cancel_cb(lv_event_t *e);

/* ---------------- helpers ---------------- */

static void _clear_cont(eqsolver_ctx_t *ctx)
{
    if (ctx->cont)
    {
        lv_obj_clean(ctx->cont);
    }
}

static void _store_equation(eqsolver_ctx_t *ctx, int index, const char *text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }
    if (index == ctx->eq_count)
    {
        if (ctx->eq_count < MAX_EQS)
        {
            strncpy(ctx->eqs[ctx->eq_count], text, MAX_EQ_LEN - 1);
            ctx->eqs[ctx->eq_count][MAX_EQ_LEN - 1] = '\0';
            ctx->eq_count++;
        }
    }
    else if (index >= 0 && index < ctx->eq_count)
    {
        strncpy(ctx->eqs[index], text, MAX_EQ_LEN - 1);
        ctx->eqs[index][MAX_EQ_LEN - 1] = '\0';
    }
}

/* ---------------- editor sub-activity ---------------- */

static void _editor_on_enter(cos_activity_t *act)
{
    eq_editor_ctx_t *ed = (eq_editor_ctx_t *)cos_activity_get_user_data(act);
    if (!ed)
    {
        return;
    }
    ed->activity = act;

    lv_obj_t *view = cos_activity_get_view(act);
    cos_round_clip(view);

    /* Top region holds title, textarea and action buttons. The keyboard (IME)
     * occupies the bottom half (y 120..240); keep this region <= 120px tall so
     * it never overlaps the keyboard and blocks its keys. */
    lv_obj_t *top = lv_obj_create(view);
    lv_obj_set_size(top, 240, 120);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(top, 6, 0);
    lv_obj_set_style_pad_row(top, 4, 0);
    lv_obj_set_style_pad_top(top, 22, 0); /* 编辑界面元素下移约 16px (6+16) */

    char title[24];
    snprintf(title, sizeof(title), "Equation %d", ed->edit_index + 1);
    lv_obj_t *ttl = lv_label_create(top);
    lv_label_set_text(ttl, title);
    cos_label_set_font_size(ttl, COS_FONT_SIZE_MICRO);
    lv_obj_set_style_text_color(ttl, lv_color_white(), 0);

    lv_obj_t *ta = lv_textarea_create(top);
    lv_obj_set_size(ta, 170, 38);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "2x + 3y = 7");
    if (ed->edit_index < ed->parent->eq_count)
    {
        lv_textarea_set_text(ta, ed->parent->eqs[ed->edit_index]);
    }
    ed->textarea = ta;
    /* The round keyboard's "OK" key emits LV_EVENT_READY on the textarea; wire
     * it to the same save routine as the Save button. */
    lv_obj_add_event_cb(ta, _editor_save_cb, LV_EVENT_READY, (void *)ed);

    lv_obj_t *brow = lv_obj_create(top);
    lv_obj_set_size(brow, 170, 32);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(brow, 0, 0);

    lv_obj_t *save = cos_button_create(brow, "Save", _editor_save_cb, (void *)ed);
    lv_obj_set_width(save, 78);
    lv_obj_t *cancel = cos_button_create(brow, "Cancel", _editor_cancel_cb, (void *)ed);
    lv_obj_set_width(cancel, 78);

    /* Existing round keyboard (IME) at the bottom of the round screen. */
    lv_obj_t *kb = cos_round_keyboard_create(view);
    cos_round_keyboard_set_mode(kb, COS_RKB_MODE_EN);
    cos_round_keyboard_set_textarea(kb, ta);
}

static void _editor_on_destroy(cos_activity_t *act)
{
    eq_editor_ctx_t *ed = (eq_editor_ctx_t *)cos_activity_get_user_data(act);
    if (ed)
    {
        cos_free(ed);
    }
}

static bool _editor_on_swipe_back(cos_activity_t *act, lv_dir_t dir)
{
    (void)act;
    (void)dir;
    /* swipe back from editor = cancel */
    return false;
}

static void _open_editor(eqsolver_ctx_t *ctx, int index)
{
    ctx->edit_index = index;

    eq_editor_ctx_t *ed = (eq_editor_ctx_t *)cos_malloc(sizeof(eq_editor_ctx_t));
    if (!ed)
    {
        return;
    }
    memset(ed, 0, sizeof(*ed));
    ed->parent = ctx;
    ed->edit_index = index;

    static const cos_activity_lifecycle_t lifecycle = {
        .on_enter = _editor_on_enter,
        .on_destroy = _editor_on_destroy,
        .on_swipe_back = _editor_on_swipe_back,
    };
    cos_activity_t *act = cos_activity_create(&lifecycle);
    if (!act)
    {
        cos_free(ed);
        return;
    }
    cos_activity_set_user_data(act, ed);
    cos_activity_set_type(act, COS_ACTIVITY_TYPE_INPUT_PAGE);
    cos_activity_enter(act);
}

/* ---------------- UI builders ---------------- */

static void _build_input(eqsolver_ctx_t *ctx)
{
    lv_obj_t *cont = ctx->cont;

    /* Scrollable equation list. */
    lv_obj_t *list = lv_obj_create(cont);
    lv_obj_set_size(list, 200, 100);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(list, 10, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_style_pad_all(list, 4, 0);

    for (int i = 0; i < ctx->eq_count; i++)
    {
        char row[80];
        if (ctx->eqs[i][0] == '\0')
        {
            snprintf(row, sizeof(row), "%d: (tap to enter)", i + 1);
        }
        else if ((int)strlen(ctx->eqs[i]) > ROW_TXT_MAX)
        {
            snprintf(row, sizeof(row), "%d: %.29s...", i + 1, ctx->eqs[i]);
        }
        else
        {
            snprintf(row, sizeof(row), "%d: %s", i + 1, ctx->eqs[i]);
        }
        lv_obj_t *btn = cos_button_create(list, row, _row_clicked_cb, (void *)(intptr_t)i);
        lv_obj_set_width(btn, 192);
        lv_obj_set_height(btn, 30);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl)
        {
            cos_label_set_font_size(lbl, COS_FONT_SIZE_MICRO);
        }
    }

    /* Action buttons. */
    lv_obj_t *brow = lv_obj_create(cont);
    lv_obj_set_size(brow, 184, 40);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(brow, 0, 0);

    lv_obj_t *add = cos_button_create(brow, "Add", _add_clicked_cb, NULL);
    lv_obj_set_width(add, 86);
    lv_obj_t *solve = cos_button_create(brow, "Solve", _solve_clicked_cb, NULL);
    lv_obj_set_width(solve, 86);
}

static void _build_result(eqsolver_ctx_t *ctx)
{
    lv_obj_t *cont = ctx->cont;

    lv_obj_t *list = lv_obj_create(cont);
    lv_obj_set_size(list, 200, 100);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(list, 10, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_style_pad_all(list, 8, 0);

    lv_obj_t *res = lv_label_create(list);
    lv_label_set_long_mode(res, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(res, 184);
    lv_label_set_text(res, ctx->result_buf[0] ? ctx->result_buf : "No result");
    cos_label_set_font_size(res, COS_FONT_SIZE_MICRO);
    lv_obj_set_style_text_color(res, lv_color_white(), 0);

    lv_obj_t *brow = lv_obj_create(cont);
    lv_obj_set_size(brow, 184, 40);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(brow, 0, 0);

    lv_obj_t *back = cos_button_create(brow, "Back", _back_clicked_cb, NULL);
    lv_obj_set_width(back, 140);
}

/* ---------------- input screen callbacks ---------------- */

static void _row_clicked_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_ctx)
    {
        _open_editor(g_ctx, idx);
    }
}

static void _add_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (g_ctx && g_ctx->eq_count < MAX_EQS)
    {
        _open_editor(g_ctx, g_ctx->eq_count);
    }
}

static void _solve_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (!g_ctx)
    {
        return;
    }
    const char *ptrs[MAX_EQS];
    int n = 0;
    for (int i = 0; i < g_ctx->eq_count; i++)
    {
        if (g_ctx->eqs[i][0] != '\0')
        {
            ptrs[n++] = g_ctx->eqs[i];
        }
    }
    if (n == 0)
    {
        snprintf(g_ctx->result_buf, sizeof(g_ctx->result_buf),
                 "Enter at least one equation.");
        g_ctx->screen = SCR_RESULT;
        _clear_cont(g_ctx);
        _build_result(g_ctx);
        return;
    }
    cos_eqsolve(ptrs, n, g_ctx->result_buf, sizeof(g_ctx->result_buf));
    g_ctx->screen = SCR_RESULT;
    _clear_cont(g_ctx);
    _build_result(g_ctx);
}

static void _back_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (g_ctx)
    {
        g_ctx->screen = SCR_INPUT;
        _clear_cont(g_ctx);
        _build_input(g_ctx);
    }
}

static void _editor_save_cb(lv_event_t *e)
{
    eq_editor_ctx_t *ed = (eq_editor_ctx_t *)lv_event_get_user_data(e);
    if (!ed)
    {
        return;
    }
    const char *t = lv_textarea_get_text(ed->textarea);
    _store_equation(ed->parent, ed->edit_index, t);

    ed->parent->screen = SCR_INPUT;
    _clear_cont(ed->parent);
    _build_input(ed->parent);
    cos_activity_back();
}

static void _editor_cancel_cb(lv_event_t *e)
{
    (void)e;
    cos_activity_back();
}

/* ---------------- main activity lifecycle ---------------- */

static void _on_enter(cos_activity_t *act)
{
    eqsolver_ctx_t *ctx = (eqsolver_ctx_t *)cos_malloc(sizeof(eqsolver_ctx_t));
    if (!ctx)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->activity = act;
    ctx->eq_count = 1; /* start with one empty equation row */
    ctx->screen = SCR_INPUT;
    g_ctx = ctx;
    cos_activity_set_user_data(act, ctx);

    lv_obj_t *view = cos_activity_get_view(act);
    cos_round_clip(view);

    lv_obj_t *cont = lv_obj_create(view);
    lv_obj_set_size(cont, 240, 240);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_style_pad_top(cont, 24, 0); /* 整体元素下移约 16px (8+16) */
    ctx->cont = cont;

    _build_input(ctx);
}

static bool _on_swipe_back(cos_activity_t *act, lv_dir_t dir)
{
    (void)dir;
    eqsolver_ctx_t *ctx = (eqsolver_ctx_t *)cos_activity_get_user_data(act);
    if (ctx && ctx->screen == SCR_RESULT)
    {
        /* stay inside the app, go back to the input screen */
        ctx->screen = SCR_INPUT;
        _clear_cont(ctx);
        _build_input(ctx);
        return true;
    }
    /* input screen: allow the framework to exit the app */
    return false;
}

static void _on_destroy(cos_activity_t *act)
{
    eqsolver_ctx_t *ctx = (eqsolver_ctx_t *)cos_activity_get_user_data(act);
    if (ctx)
    {
        cos_free(ctx);
    }
    if (g_ctx == ctx)
    {
        g_ctx = NULL;
    }
}

/* ---------------- entry ---------------- */

void cos_eqsolver_enter(void)
{
    static const cos_activity_lifecycle_t lifecycle = {
        .on_enter = _on_enter,
        .on_destroy = _on_destroy,
        .on_swipe_back = _on_swipe_back,
    };
    cos_activity_t *act = cos_activity_create(&lifecycle);
    if (!act)
    {
        return;
    }
    cos_activity_set_type(act, COS_ACTIVITY_TYPE_APP);
    cos_activity_enter(act);
}

#endif /* COS_APP_EQSOLVER_ENABLED */
