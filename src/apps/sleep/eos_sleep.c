/**
 * @file eos_sleep.c
 * @brief Sleep app - bedtime schedule manager (native C app)
 *
 * UI (240x240 round display):
 *   List page : each rule card shows "HH:MM - HH:MM", weekday summary and
 *               ON/OFF; tap card to edit, tap "x" to delete, "Add" to create.
 *   Wizard    : 5 pages - START TIME -> END TIME -> WEEKDAYS -> REPEAT -> DONE.
 *
 * Rules are stored in system config under key "sleeps" (JSON array),
 * consumed by the Core Sleep service (eos_service_sleep.c).
 */

#include "eos_sleep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lvgl.h"
#include "cJSON.h"
#define EOS_LOG_TAG "SleepApp"
#include "eos_log.h"
#include "eos_activity.h"
#include "eos_service_config.h"
#include "eos_font.h"

/* Macros and Definitions -------------------------------------*/

#define EOS_SLEEP_CONFIG_KEY "sleeps"
#define EOS_SLEEP_MAX_RULES 8
#define EOS_SLEEP_WIZARD_PAGES 5
#define _UI_CONTENT_Y 44 /* 系统 header 之下(圆屏紧凑) */
#define _UI_PANEL_W 212   /* 圆形安全区宽度 */
#define _UI_PANEL_X 14    /* 居中: (240-212)/2 */
#define _UI_LIST_H 120    /* 列表可视高度(圆屏) */
#define _UI_CARD_W 200
#define _UI_CARD_H 38
#define _UI_CARD_GAP 4
#define _UI_BTN_W 96
#define _UI_BTN_H 28
#define _UI_ROLLER_W 76
#define _UI_ROLLER_H 96
#define _UI_DAY_BTN 26
#define _UI_DAY_GAP 2

#define _HOURS_OPTS "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23"
#define _MINS_OPTS "00\n05\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55"
#define _REP_OPTS "Always\n1 time\n2 times\n3 times\n4 times\n5 times\n6 times\n7 times"

#define _UI_ACCENT lv_palette_main(LV_PALETTE_LIGHT_BLUE)

/* Typedefs ---------------------------------------------------*/

typedef struct
{
    int id;
    int sh, sm;   /* start hour/min */
    int eh, em;   /* end hour/min */
    uint8_t days; /* bit0=Mon ... bit6=Sun */
    bool on;
    int rep; /* -1 = always */
} sleep_rule_t;

/* Variables --------------------------------------------------*/

static eos_activity_t *s_act = NULL;
static sleep_rule_t s_rules[EOS_SLEEP_MAX_RULES];
static int s_count = 0;

static lv_obj_t *s_list = NULL;   /* 规则列表容器 */
static lv_obj_t *s_wizard = NULL; /* 向导容器 */
static lv_obj_t *s_pages[EOS_SLEEP_WIZARD_PAGES];
static lv_obj_t *s_day_btns[7];
static bool s_day_sel[7];

static int s_edit_index = -1; /* -1=新建 */
static int s_wiz_page = 0;
static lv_obj_t *s_rl_sh = NULL, *s_rl_sm = NULL;
static lv_obj_t *s_rl_eh = NULL, *s_rl_em = NULL;
static lv_obj_t *s_rl_rep = NULL;
static lv_obj_t *s_summary = NULL;

/* Local function prototypes ----------------------------------*/

static void _rules_load(void);
static void _rules_save(void);
static void _ui_rebuild_list(void);
static void _wiz_show_page(int page);
static void _wiz_prefill(void);
static void _on_save(void);
static void _on_add_click(lv_event_t *e);
static void _on_item_click(lv_event_t *e);
static void _on_del_click(lv_event_t *e);
static void _on_day_click(lv_event_t *e);
static void _on_wiz_back(lv_event_t *e);
static void _on_wiz_next(lv_event_t *e);

/* Local functions --------------------------------------------*/

static void _rules_load(void)
{
    s_count = 0;
    memset(s_rules, 0, sizeof(s_rules));

    cJSON *arr = eos_config_get_json(EOS_SLEEP_CONFIG_KEY);
    if (!arr)
        return;
    if (!cJSON_IsArray(arr))
    {
        cJSON_Delete(arr);
        return;
    }

    int n = cJSON_GetArraySize(arr);
    if (n > EOS_SLEEP_MAX_RULES)
        n = EOS_SLEEP_MAX_RULES;

    for (int i = 0; i < n; i++)
    {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        if (!it || !cJSON_IsObject(it))
            continue;
        sleep_rule_t *r = &s_rules[s_count];
        cJSON *v;
        r->id = (v = cJSON_GetObjectItem(it, "id")) ? v->valueint : s_count;
        r->sh = (v = cJSON_GetObjectItem(it, "sh")) ? v->valueint : 0;
        r->sm = (v = cJSON_GetObjectItem(it, "sm")) ? v->valueint : 0;
        r->eh = (v = cJSON_GetObjectItem(it, "eh")) ? v->valueint : 0;
        r->em = (v = cJSON_GetObjectItem(it, "em")) ? v->valueint : 0;
        r->days = (v = cJSON_GetObjectItem(it, "days")) ? (uint8_t)v->valueint : 0x7F;
        r->on = (v = cJSON_GetObjectItem(it, "on")) ? cJSON_IsTrue(v) : true;
        r->rep = (v = cJSON_GetObjectItem(it, "rep")) ? v->valueint : -1;
        if (r->sh < 0 || r->sh > 23 || r->sm < 0 || r->sm > 59 ||
            r->eh < 0 || r->eh > 23 || r->em < 0 || r->em > 59)
            continue;
        s_count++;
    }
    cJSON_Delete(arr);
}

static void _rules_save(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr)
        return;
    for (int i = 0; i < s_count; i++)
    {
        sleep_rule_t *r = &s_rules[i];
        cJSON *it = cJSON_CreateObject();
        cJSON_AddNumberToObject(it, "id", r->id);
        cJSON_AddBoolToObject(it, "on", r->on);
        cJSON_AddNumberToObject(it, "sh", r->sh);
        cJSON_AddNumberToObject(it, "sm", r->sm);
        cJSON_AddNumberToObject(it, "eh", r->eh);
        cJSON_AddNumberToObject(it, "em", r->em);
        cJSON_AddNumberToObject(it, "days", r->days);
        cJSON_AddNumberToObject(it, "rep", r->rep);
        cJSON_AddItemToArray(arr, it);
    }
    eos_config_set_json(EOS_SLEEP_CONFIG_KEY, arr);
}

static const char *_days_str(uint8_t days)
{
    static const char *names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static char buf[48];
    if (days == 0x7F)
        return "Every day";
    buf[0] = 0;
    int n = 0;
    for (int i = 0; i < 7; i++)
    {
        if (days & (1u << i))
        {
            if (n)
                strcat(buf, " ");
            strcat(buf, names[i]);
            n++;
        }
    }
    if (!n)
        strcpy(buf, "No day");
    return buf;
}

/* ---- 向导状态同步 ---- */

static void _wiz_sync_days(void)
{
    for (int i = 0; i < 7; i++)
    {
        if (!s_day_btns[i])
            continue;
        if (s_day_sel[i])
            lv_obj_add_state(s_day_btns[i], LV_STATE_CHECKED);
        else
            lv_obj_remove_state(s_day_btns[i], LV_STATE_CHECKED);
    }
}

/** 打开向导前,按当前编辑规则(或新建默认值)预填各控件 */
static void _wiz_prefill(void)
{
    int sh = 22, sm = 0, eh = 7, em = 0, rep = -1;
    if (s_edit_index >= 0 && s_edit_index < s_count)
    {
        sleep_rule_t *r = &s_rules[s_edit_index];
        sh = r->sh;
        sm = r->sm;
        eh = r->eh;
        em = r->em;
        rep = r->rep;
        for (int i = 0; i < 7; i++)
            s_day_sel[i] = (r->days >> i) & 1u;
    }
    else
    {
        for (int i = 0; i < 7; i++)
            s_day_sel[i] = true;
    }

    if (s_rl_sh)
        lv_roller_set_selected(s_rl_sh, sh, LV_ANIM_OFF);
    if (s_rl_sm)
        lv_roller_set_selected(s_rl_sm, sm / 5, LV_ANIM_OFF);
    if (s_rl_eh)
        lv_roller_set_selected(s_rl_eh, eh, LV_ANIM_OFF);
    if (s_rl_em)
        lv_roller_set_selected(s_rl_em, em / 5, LV_ANIM_OFF);
    if (s_rl_rep)
        lv_roller_set_selected(s_rl_rep, rep < 0 ? 0 : rep, LV_ANIM_OFF);
    _wiz_sync_days();
    if (s_summary)
        lv_label_set_text(s_summary, "");
}

/** 打开向导:隐藏列表,显示向导并切到首页 */
static void _wiz_open(int index)
{
    s_edit_index = index;
    _wiz_prefill();
    if (s_list)
        lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    if (s_wizard)
        lv_obj_clear_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);
    _wiz_show_page(0);
}

/** 关闭向导:回列表 */
static void _wiz_close(void)
{
    if (s_wizard)
        lv_obj_add_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);
    if (s_list)
        lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
}

static void _wiz_show_page(int page)
{
    if (page < 0 || page >= EOS_SLEEP_WIZARD_PAGES)
        return;
    s_wiz_page = page;
    for (int i = 0; i < EOS_SLEEP_WIZARD_PAGES; i++)
    {
        if (!s_pages[i])
            continue;
        if (i == page)
            lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- 事件回调 ---- */

static void _on_add_click(lv_event_t *e)
{
    (void)e;
    _wiz_open(-1);
}

static void _on_item_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (idx < 0 || idx >= s_count)
        return;
    _wiz_open(idx);
}

static void _on_del_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (idx < 0 || idx >= s_count)
        return;
    for (int i = idx; i < s_count - 1; i++)
        s_rules[i] = s_rules[i + 1];
    s_count--;
    _rules_save();
    _ui_rebuild_list();
}

static void _on_day_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 6)
        return;
    s_day_sel[idx] = !s_day_sel[idx];
    if (s_day_sel[idx])
        lv_obj_add_state(s_day_btns[idx], LV_STATE_CHECKED);
    else
        lv_obj_remove_state(s_day_btns[idx], LV_STATE_CHECKED);
}

static void _on_wiz_back(lv_event_t *e)
{
    (void)e;
    if (s_wiz_page == 0)
        _wiz_close();
    else
        _wiz_show_page(s_wiz_page - 1);
}

static void _on_wiz_next(lv_event_t *e)
{
    (void)e;
    if (s_wiz_page < EOS_SLEEP_WIZARD_PAGES - 1)
        _wiz_show_page(s_wiz_page + 1);
    else
        _on_save();
}

static void _roller_to_time(lv_obj_t *rh, lv_obj_t *rm, int *h, int *m)
{
    char buf[8];
    lv_roller_get_selected_str(rh, buf, sizeof(buf));
    *h = atoi(buf);
    lv_roller_get_selected_str(rm, buf, sizeof(buf));
    *m = atoi(buf);
}

static void _on_save(void)
{
    int sh, sm, eh, em;
    _roller_to_time(s_rl_sh, s_rl_sm, &sh, &sm);
    _roller_to_time(s_rl_eh, s_rl_em, &eh, &em);

    if (s_summary)
        lv_label_set_text(s_summary, "");

    if (sh == eh && sm == em)
    {
        if (s_summary)
            lv_label_set_text(s_summary, "Start and end must\ndiffer");
        return;
    }
    uint8_t days = 0;
    for (int i = 0; i < 7; i++)
        if (s_day_sel[i])
            days |= (1u << i);
    if (days == 0)
    {
        if (s_summary)
            lv_label_set_text(s_summary, "Pick at least\none day");
        return;
    }

    char buf[8];
    lv_roller_get_selected_str(s_rl_rep, buf, sizeof(buf));
    int rep = (buf[0] == 'A') ? -1 : atoi(buf);

    sleep_rule_t *r;
    if (s_edit_index >= 0)
    {
        r = &s_rules[s_edit_index];
    }
    else
    {
        if (s_count >= EOS_SLEEP_MAX_RULES)
        {
            if (s_summary)
                lv_label_set_text(s_summary, "Max 8 rules");
            return;
        }
        r = &s_rules[s_count];
        s_count++;
        r->id = s_count - 1;
    }
    r->sh = sh;
    r->sm = sm;
    r->eh = eh;
    r->em = em;
    r->days = days;
    r->rep = rep;
    r->on = true;

    _rules_save();
    _ui_rebuild_list();
    _wiz_close();
    EOS_LOG_I("Sleep rule saved: %02d:%02d - %02d:%02d days=0x%02X rep=%d",
              sh, sm, eh, em, days, rep);
}

/* ---- UI 构建 ---- */

static void _ui_rebuild_list(void)
{
    if (!s_list)
        return;
    lv_obj_clean(s_list);

    int y = 0;
    for (int i = 0; i < s_count; i++)
    {
        sleep_rule_t *r = &s_rules[i];

        lv_obj_t *card = lv_obj_create(s_list);
        lv_obj_set_size(card, _UI_CARD_W, _UI_CARD_H);
        lv_obj_set_pos(card, 6, y);
        lv_obj_set_user_data(card, (void *)(intptr_t)(i + 1));
        lv_obj_add_event_cb(card, _on_item_click, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 0, 0);

        char buf[64];
        snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d", r->sh, r->sm, r->eh, r->em);
        lv_obj_t *tt = lv_label_create(card);
        eos_label_set_font_size(tt, EOS_FONT_SIZE_TINY);
        lv_label_set_text(tt, buf);
        lv_obj_set_pos(tt, 8, 4);
        lv_obj_set_style_text_color(tt, _UI_ACCENT, 0);

        lv_obj_t *ts = lv_label_create(card);
        eos_label_set_font_size(ts, EOS_FONT_SIZE_TINY);
        lv_label_set_text(ts, _days_str(r->days));
        lv_obj_set_pos(ts, 8, 21);
        lv_obj_set_style_text_color(ts, lv_palette_main(LV_PALETTE_GREY), 0);

        lv_obj_t *tst = lv_label_create(card);
        eos_label_set_font_size(tst, EOS_FONT_SIZE_TINY);
        lv_label_set_text(tst, r->on ? "ON" : "OFF");
        lv_obj_set_pos(tst, 150, 4);
        lv_obj_set_style_text_color(tst, r->on ? lv_palette_main(LV_PALETTE_GREEN)
                                               : lv_palette_main(LV_PALETTE_GREY),
                                    0);

        lv_obj_t *del = lv_button_create(card);
        lv_obj_set_size(del, 22, 22);
        lv_obj_set_pos(del, 172, 8);
        lv_obj_set_user_data(del, (void *)(intptr_t)(i + 1));
        lv_obj_add_event_cb(del, _on_del_click, LV_EVENT_CLICKED, NULL);
        lv_obj_t *dx = lv_label_create(del);
        eos_label_set_font_size(dx, EOS_FONT_SIZE_TINY);
        lv_label_set_text(dx, "x");
        lv_obj_center(dx);

        y += (_UI_CARD_H + _UI_CARD_GAP);
    }

    if (s_count == 0)
    {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "No sleep rules\nTap Add to create");
        lv_obj_center(empty);
        lv_obj_set_style_text_color(empty, lv_palette_main(LV_PALETTE_GREY), 0);
    }
}

static lv_obj_t *_make_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *t = lv_label_create(parent);
    eos_label_set_font_size(t, EOS_FONT_SIZE_SMALL);
    lv_label_set_text(t, text);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_color(t, _UI_ACCENT, 0);
    return t;
}

static lv_obj_t *_make_nav_btn(lv_obj_t *parent, const char *text,
                               lv_event_cb_t cb, int x, int y)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, _UI_BTN_W, _UI_BTN_H);
    lv_obj_set_pos(b, x, y);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    eos_label_set_font_size(l, EOS_FONT_SIZE_TINY);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *_make_roller(lv_obj_t *parent, const char *opts, int selected)
{
    lv_obj_t *rl = lv_roller_create(parent);
    lv_roller_set_options(rl, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(rl, selected, LV_ANIM_OFF);
    lv_obj_set_size(rl, _UI_ROLLER_W, _UI_ROLLER_H);
    return rl;
}

static lv_obj_t *_make_time_page(lv_obj_t *parent, const char *title,
                                 lv_obj_t **out_rl_h, lv_obj_t **out_rl_m,
                                 int h, int m)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, _UI_PANEL_W, 208);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    _make_title(page, title);

    *out_rl_h = _make_roller(page, _HOURS_OPTS, h);
    lv_obj_set_pos(*out_rl_h, 18, 64);
    *out_rl_m = _make_roller(page, _MINS_OPTS, m / 5);
    lv_obj_set_pos(*out_rl_m, _UI_PANEL_W - _UI_ROLLER_W - 18, 64);

    _make_nav_btn(page, "Back", _on_wiz_back, 16, 168);
    _make_nav_btn(page, "Next", _on_wiz_next, 116, 168);
    return page;
}

/* ---- 生命周期 ---- */

static void _on_enter(eos_activity_t *a)
{
    s_act = a;
    lv_obj_t *scr = eos_activity_get_view(a);
    if (!scr)
        return;

    _rules_load();

    /* 列表容器(圆形安全区:212x120 居中) */
    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, _UI_PANEL_W, _UI_LIST_H);
    lv_obj_set_pos(s_list, _UI_PANEL_X, _UI_CONTENT_Y);
    lv_obj_set_style_radius(s_list, 14, 0);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    /* Add 按钮 */
    lv_obj_t *add = lv_button_create(scr);
    lv_obj_set_size(add, _UI_BTN_W, _UI_BTN_H);
    lv_obj_set_pos(add, (_UI_PANEL_W - _UI_BTN_W) / 2 + _UI_PANEL_X, 196);
    lv_obj_add_event_cb(add, _on_add_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *add_l = lv_label_create(add);
    eos_label_set_font_size(add_l, EOS_FONT_SIZE_SMALL);
    lv_label_set_text(add_l, "+ Add");
    lv_obj_center(add_l);

    /* 向导容器(初始隐藏,圆屏安全区) */
    s_wizard = lv_obj_create(scr);
    lv_obj_set_size(s_wizard, _UI_PANEL_W, 208);
    lv_obj_set_pos(s_wizard, _UI_PANEL_X, _UI_CONTENT_Y);
    lv_obj_set_style_radius(s_wizard, 14, 0);
    lv_obj_set_style_bg_color(s_wizard, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(s_wizard, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_wizard, 0, 0);
    lv_obj_remove_flag(s_wizard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);

    /* 页0: START TIME */
    s_pages[0] = _make_time_page(s_wizard, "START TIME", &s_rl_sh, &s_rl_sm, 22, 0);
    /* 页1: END TIME */
    s_pages[1] = _make_time_page(s_wizard, "END TIME", &s_rl_eh, &s_rl_em, 7, 0);

    /* 页2: WEEKDAYS */
    lv_obj_t *p2 = lv_obj_create(s_wizard);
    lv_obj_set_size(p2, _UI_PANEL_W, 208);
    lv_obj_set_pos(p2, 0, 0);
    lv_obj_set_style_pad_all(p2, 0, 0);
    lv_obj_remove_flag(p2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p2, LV_OBJ_FLAG_HIDDEN);
    _make_title(p2, "WEEKDAYS");
    static const char *day_letters[7] = {"M", "T", "W", "T", "F", "S", "S"};
    for (int i = 0; i < 7; i++)
    {
        lv_obj_t *b = lv_button_create(p2);
        lv_obj_set_size(b, _UI_DAY_BTN, _UI_DAY_BTN);
        lv_obj_set_pos(b, _UI_PANEL_X + i * (_UI_DAY_BTN + _UI_DAY_GAP), 112);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, _on_day_click, LV_EVENT_CLICKED, NULL);
        lv_obj_t *bl = lv_label_create(b);
        eos_label_set_font_size(bl, EOS_FONT_SIZE_TINY);
        lv_label_set_text(bl, day_letters[i]);
        lv_obj_center(bl);
        s_day_btns[i] = b;
    }
    _make_nav_btn(p2, "Back", _on_wiz_back, 16, 168);
    _make_nav_btn(p2, "Next", _on_wiz_next, 116, 168);
    s_pages[2] = p2;

    /* 页3: REPEAT */
    lv_obj_t *p3 = lv_obj_create(s_wizard);
    lv_obj_set_size(p3, _UI_PANEL_W, 208);
    lv_obj_set_pos(p3, 0, 0);
    lv_obj_set_style_pad_all(p3, 0, 0);
    lv_obj_remove_flag(p3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p3, LV_OBJ_FLAG_HIDDEN);
    _make_title(p3, "REPEAT");
    s_rl_rep = _make_roller(p3, _REP_OPTS, 0);
    lv_obj_set_pos(s_rl_rep, (_UI_PANEL_W - _UI_ROLLER_W) / 2, 72);
    _make_nav_btn(p3, "Back", _on_wiz_back, 16, 168);
    _make_nav_btn(p3, "Next", _on_wiz_next, 116, 168);
    s_pages[3] = p3;

    /* 页4: DONE */
    lv_obj_t *p4 = lv_obj_create(s_wizard);
    lv_obj_set_size(p4, _UI_PANEL_W, 208);
    lv_obj_set_pos(p4, 0, 0);
    lv_obj_set_style_pad_all(p4, 0, 0);
    lv_obj_remove_flag(p4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p4, LV_OBJ_FLAG_HIDDEN);
    _make_title(p4, "DONE");
    s_summary = lv_label_create(p4);
    lv_label_set_text(s_summary, "");
    lv_obj_set_pos(s_summary, 12, 96);
    lv_obj_set_width(s_summary, _UI_PANEL_W - 24);
    lv_obj_set_style_text_align(s_summary, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_summary, lv_palette_main(LV_PALETTE_RED), 0);
    _make_nav_btn(p4, "Back", _on_wiz_back, 16, 168);
    _make_nav_btn(p4, "Save", _on_wiz_next, 116, 168);
    s_pages[4] = p4;

    _wiz_prefill();
    _ui_rebuild_list();
}

static void _on_destroy(eos_activity_t *a)
{
    (void)a;
    s_act = NULL;
    s_list = NULL;
    s_wizard = NULL;
    for (int i = 0; i < EOS_SLEEP_WIZARD_PAGES; i++)
        s_pages[i] = NULL;
}

static bool _on_swipe_back(eos_activity_t *self, lv_dir_t dir)
{
    (void)self;
    (void)dir;
    return false; /* 交给框架返回 */
}

static eos_activity_lifecycle_t s_lifecycle = {
    .on_enter = _on_enter,
    .on_destroy = _on_destroy,
    .on_swipe_back = _on_swipe_back,
};

void eos_sleep_enter(void)
{
    EOS_LOG_I("Sleep app: enter");
    eos_activity_t *act = eos_activity_create(&s_lifecycle);
    if (!act)
        return;
    eos_activity_set_title(act, "Sleep");
    eos_activity_enter(act);
}
