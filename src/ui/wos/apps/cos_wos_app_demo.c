/**
 * @file cos_wos_app_demo.c
 * @brief WOS demo app — validates the framework:
 *   flex layout, glass cards, ledger timer, clean teardown.
 */
#include "cos_wos.h"
#include "cos_log.h"
#include "cos_mem.h"
#include <stdio.h>

#define COS_LOG_TAG "WosDemo"

typedef struct
{
    lv_obj_t *counter_label;
    int ticks;
} demo_ctx_t;

static void _tick_cb(lv_timer_t *tm)
{
    cos_wos_app_t *app = (cos_wos_app_t *)lv_timer_get_user_data(tm);
    if (!app || !app->user_data)
        return;
    demo_ctx_t *ctx = (demo_ctx_t *)app->user_data;
    if (!lv_obj_is_valid(ctx->counter_label))
        return;
    ctx->ticks++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", ctx->ticks);
    lv_label_set_text(ctx->counter_label, buf);
}

static void _demo_build(cos_wos_app_t *app)
{
    /* page root: full-screen flex column, safe area reserved */
    lv_obj_t *root = app->root;
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(root, WOS_STATUSBAR_H + 8, 0);
    lv_obj_set_style_pad_hor(root, 16, 0);
    lv_obj_set_style_pad_row(root, 10, 0);

    demo_ctx_t *ctx = (demo_ctx_t *)cos_malloc(sizeof(demo_ctx_t));
    ctx->ticks = 0;
    ctx->counter_label = NULL;
    app->user_data = ctx;

    /* title */
    lv_obj_t *title = lv_label_create(root);
    wos_style_text_primary(title, WOS_FONT_XL);
    lv_label_set_text(title, "Demo");

    /* glass card: status row */
    lv_obj_t *card = wos_card(root);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *card_title = lv_label_create(card);
    wos_style_text_secondary(card_title, WOS_FONT_MD);
    lv_label_set_text(card_title, "tick");

    ctx->counter_label = lv_label_create(card);
    wos_style_text_primary(ctx->counter_label, WOS_FONT_MD);
    lv_label_set_text(ctx->counter_label, "0");

    /* two glass cards demonstrating flex rows */
    const char *rows[] = { "Battery", "Bluetooth", "Wi-Fi" };
    int i;
    for (i = 0; i < 3; i++)
    {
        lv_obj_t *rc = wos_card(root);
        lv_obj_set_width(rc, lv_pct(100));
        lv_obj_set_flex_flow(rc, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(rc, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *rl = lv_label_create(rc);
        wos_style_text_secondary(rl, WOS_FONT_MD);
        lv_label_set_text(rl, rows[i]);
        lv_obj_t *rv = lv_label_create(rc);
        wos_style_text_primary(rv, WOS_FONT_MD);
        lv_label_set_text(rv, i == 0 ? "80%" : i == 1 ? "On" : "Off");
    }

    /* ledger timer — deleted automatically on close (cleanup audit) */
    wos_app_timer(app, _tick_cb, 1000, app);
}

static void _demo_destroy(cos_wos_app_t *app)
{
    if (app->user_data)
        cos_free(app->user_data);
    app->user_data = NULL;
}

static const cos_wos_app_desc_t s_demo_desc = {
    .id = "demo",
    .name = "Demo",
    .build = _demo_build,
    .destroy = _demo_destroy,
};

const cos_wos_app_desc_t *wos_demo_app_get_desc(void)
{
    return &s_demo_desc;
}
