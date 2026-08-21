/**
 * @file eos_wos_notification.c
 * @brief WOS notification layer implementation
 */
#include "eos_wos_notification.h"

#include "eos_wos_theme.h"
#include "eos_overlay_layer.h"
#include "eos_log.h"
#include <string.h>

#define EOS_LOG_TAG "WosNotify"

#define NOTIFY_W   216
#define NOTIFY_H   56

static lv_obj_t *s_card = NULL;
static lv_timer_t *s_hold_timer = NULL;

static void _translate_y_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void _slide_out(lv_obj_t *card);

static void _hold_done_cb(lv_timer_t *tm)
{
    if (tm)
        lv_timer_delete(tm);
    s_hold_timer = NULL;
    if (s_card && lv_obj_is_valid(s_card))
        _slide_out(s_card);
}

static void _delete_ready_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (obj && lv_obj_is_valid(obj))
        lv_obj_delete(obj);
}

static void _slide_out(lv_obj_t *card)
{
    if (!card || !lv_obj_is_valid(card))
        return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_values(&a, 0, -NOTIFY_H);
    lv_anim_set_exec_cb(&a, _translate_y_cb);
    lv_anim_set_ready_cb(&a, _delete_ready_cb);
    lv_anim_start(&a);
}

static void _slide_in(lv_obj_t *card)
{
    if (!card || !lv_obj_is_valid(card))
        return;
    lv_obj_set_style_translate_y(card, -NOTIFY_H, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_values(&a, -NOTIFY_H, 0);
    lv_anim_set_exec_cb(&a, _translate_y_cb);
    lv_anim_start(&a);
}

void wos_notification_dismiss(void)
{
    if (s_hold_timer)
    {
        lv_timer_delete(s_hold_timer);
        s_hold_timer = NULL;
    }
    if (s_card && lv_obj_is_valid(s_card))
        _slide_out(s_card);
    s_card = NULL;
}

void wos_notification_show(const char *title, const char *body, uint32_t hold_ms)
{
    lv_obj_t *layer = eos_overlay_get_notification_layer();
    if (!layer)
        return;

    /* dismiss any current notification first */
    if (s_card && lv_obj_is_valid(s_card))
    {
        lv_obj_delete(s_card);
        s_card = NULL;
    }
    if (s_hold_timer)
    {
        lv_timer_delete(s_hold_timer);
        s_hold_timer = NULL;
    }

    lv_obj_t *card = lv_obj_create(layer);
    lv_obj_set_size(card, NOTIFY_W, NOTIFY_H);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, WOS_STATUSBAR_H + 4);
    wos_style_glass_card(card, WOS_RADIUS_CARD, 224);
    /* Cards are transient; don't let them swallow touches/gestures. */
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    if (title)
    {
        lv_obj_t *tl = lv_label_create(card);
        wos_style_text_primary(tl, WOS_FONT_MD);
        lv_label_set_text(tl, title);
    }
    if (body)
    {
        lv_obj_t *bl = lv_label_create(card);
        wos_style_text_secondary(bl, WOS_FONT_SM);
        lv_label_set_text(bl, body);
    }

    s_card = card;
    _slide_in(card);

    if (hold_ms > 0)
    {
        s_hold_timer = lv_timer_create(_hold_done_cb, hold_ms, NULL);
        lv_timer_set_repeat_count(s_hold_timer, 1);
    }
    EOS_LOG_I("wos: notification shown (%s)", title ? title : "?");
}

void wos_notification_init(void)
{
    /* The layer already exists; nothing else to pre-build. */
    EOS_LOG_I("wos: notification layer ready");
}
