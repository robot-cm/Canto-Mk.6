/**
 * @file eos_wos_statusbar.c
 * @brief WOS status bar implementation
 */
#include "eos_wos_statusbar.h"

#include "eos_wos_theme.h"
#include "eos_overlay_layer.h"
#include "eos_log.h"
#include "eos_service_time.h"
#include <stdio.h>
#include <string.h>

#define EOS_LOG_TAG "WosStatus"

static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_clock = NULL;
static lv_timer_t *s_timer = NULL;
static char s_title_buf[32] = "ElenixOS";

static void _clock_tick_cb(lv_timer_t *tm)
{
    (void)tm;
    wos_statusbar_update_clock();
}

void wos_statusbar_update_clock(void)
{
    if (!s_clock || !lv_obj_is_valid(s_clock))
        return;
    eos_datetime_t dt = eos_time_get();
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour, dt.min);
    lv_label_set_text(s_clock, buf);
}

void wos_statusbar_set_title(const char *title)
{
    if (!s_title || !title)
        return;
    strncpy(s_title_buf, title, sizeof(s_title_buf) - 1);
    s_title_buf[sizeof(s_title_buf) - 1] = '\0';
    lv_label_set_text(s_title, s_title_buf);
}

void wos_statusbar_init(void)
{
    if (s_bar)
        return;
    lv_obj_t *layer = eos_overlay_get_statusbar_layer();
    if (!layer)
        return;

    s_bar = lv_obj_create(layer);
    lv_obj_set_size(s_bar, lv_pct(100), WOS_STATUSBAR_H);
    lv_obj_set_pos(s_bar, 0, 0);
    lv_obj_set_style_bg_color(s_bar, WOS_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_40, 0); /* translucent glass */
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_pad_hor(s_bar, 12, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    /* Pure display: must not swallow touches/gestures aimed at the app page. */
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);

    /* flex row: title left, clock right */
    lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_title = lv_label_create(s_bar);
    wos_style_text_secondary(s_title, WOS_FONT_SM);
    lv_label_set_text(s_title, s_title_buf);

    s_clock = lv_label_create(s_bar);
    wos_style_text_primary(s_clock, WOS_FONT_SM);
    wos_statusbar_update_clock();

    s_timer = lv_timer_create(_clock_tick_cb, 30000, NULL);
    lv_timer_set_repeat_count(s_timer, -1);

    EOS_LOG_I("wos: status bar initialized");
}
