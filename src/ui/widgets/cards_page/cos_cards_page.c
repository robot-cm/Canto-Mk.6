/**
 * @file cos_cards_page.c
 * @brief Small-cards (smart stack) page revealed by up-swipe from the bottom
 *        edge — Redmi-Watch style.
 *
 *        Cards are owned by a small registry. Built-in cards and app-provided
 *        cards go through the SAME cos_cards_page_register_card() path; the
 *        stack is ordered by `priority` and rebuilt whenever the registry
 *        changes. Apps fully control their card content via the `build`
 *        callback, so no internal layout knowledge is required to adapt.
 */

#include "cos_cards_page.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COS_LOG_TAG "CardsPage"
#include "cos_log.h"
#include "cos_theme.h"
#include "cos_service_time.h"
#include "cos_service_battery.h"
#include "cos_event.h"
#include "cos_crown.h"
#include "cos_overlay_layer.h"
#include "cos_liquid_glass.h"
#include "cos_mem.h"

/* Cards-page text is Chinese (活动/心率/电量, 周X, 一波/分 …). The project's
 * latin-only montserrat_14 cannot render CJK glyphs and produced tofu (□)
 * boxes. Use the backend-agnostic small font (cos_label_set_font_size): on
 * the C_MULTI backend it resolves to source_han_sans_22 (bitmap, real device),
 * on the TTF backend it resolves to the bundled CJK TTF (simhei). This keeps
 * the cards page building on BOTH targets — referencing the bitmap symbol
 * directly would fail to link in the simulator, where source_han_sans_*.c is
 * deliberately excluded from the font sources (TTF is used instead). */
#include "cos_font.h"

/* Macros and Definitions -------------------------------------*/
#define _HEADER_HEIGHT 28
#define _DOT_SIZE      10
#define _CARD_GAP      8

/* Card registry ---------------------------------------------*/
struct cos_card_t
{
    cos_card_desc_t desc; /**< Copy of the descriptor (id/title/accent/build/click/user_data). */
    lv_obj_t       *obj;  /**< Live LVGL object, or NULL if not yet built / unregistered. */
    bool            used; /**< Slot occupied. */
};

static struct cos_card_t _cards[COS_CARDS_PAGE_MAX_CARDS];

static cos_cards_page_t *cards_page_instance = NULL;

/* Forward decls for overlay descriptor -----------------------*/
static void _cards_page_overlay_pull_back(void);
static void _cards_page_overlay_hide(void);
static void _cards_page_overlay_on_focus(void);
static bool _cards_page_overlay_is_open(void);
static lv_obj_t *_cards_page_overlay_get_scrollable(void);
static lv_obj_t *_cards_page_overlay_get_foreground_obj(void);

static const cos_chrome_overlay_t _cards_page_overlay = {
    .pull_back = _cards_page_overlay_pull_back,
    .hide = _cards_page_overlay_hide,
    .on_focus = _cards_page_overlay_on_focus,
    .is_open = _cards_page_overlay_is_open,
    .get_scrollable = _cards_page_overlay_get_scrollable,
    .get_foreground_obj = _cards_page_overlay_get_foreground_obj,
    .name = "cards_page",
};

/* ============== Header ============== */

static void _cards_page_update_time_cb(lv_timer_t *t)
{
    cos_cards_page_t *cp = (cos_cards_page_t *)lv_timer_get_user_data(t);
    if (!cp || !cp->header_time || !lv_obj_is_valid(cp->header_time))
        return;

    cos_datetime_t now = cos_time_get();
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.min);
    lv_label_set_text(cp->header_time, buf);

    if (cp->header_date && lv_obj_is_valid(cp->header_date))
    {
        /* 1=Mon ... 7=Sun in cos_time; show "M/D 周X" */
        const char *wk_cn[] = {"", "一", "二", "三", "四", "五", "六", "日"};
        int w = now.day_of_week;
        if (w < 1 || w > 7) w = 1;
        char dbuf[24];
        snprintf(dbuf, sizeof(dbuf), "%d/%d 周%s", now.month, now.day, wk_cn[w]);
        lv_label_set_text(cp->header_date, dbuf);
    }
}

static lv_obj_t *_cards_page_create_header(cos_cards_page_t *cp, lv_obj_t *parent)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), _HEADER_HEIGHT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(hdr, 16, 0);
    lv_obj_set_style_pad_right(hdr, 16, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    cp->header_time = lv_label_create(hdr);
    lv_label_set_text(cp->header_time, "--:--");
    lv_obj_set_style_text_color(cp->header_time, lv_color_hex(0xFFFFFF), 0);
    cos_label_set_font_size(cp->header_time, COS_FONT_SIZE_SMALL);

    cp->header_date = lv_label_create(hdr);
    lv_label_set_text(cp->header_date, "--/-- 周-");
    lv_obj_set_style_text_color(cp->header_date, lv_color_hex(0xAEB4BF), 0);
    cos_label_set_font_size(cp->header_date, COS_FONT_SIZE_SMALL);

    return hdr;
}

/* ============== Card stack construction ============== */

static void _card_clicked_cb(lv_event_t *e)
{
    struct cos_card_t *slot = (struct cos_card_t *)lv_event_get_user_data(e);
    if (slot && slot->used && slot->desc.click)
        slot->desc.click((cos_card_handle_t)slot, slot->desc.user_data);
}

static lv_obj_t *_make_card(cos_cards_page_t *cp, struct cos_card_t *slot)
{
    lv_obj_t *card = lv_obj_create(cp->cards);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, COS_CARDS_PAGE_CARD_W, COS_CARDS_PAGE_CARD_H);
    lv_obj_set_style_bg_color(card, slot->desc.accent, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, COS_CARDS_PAGE_CARD_RADIUS, 0);
    cos_liquid_glass_card_subtle(card);   /* subtle glassy corners only */
    lv_obj_set_style_pad_left(card, 12, 0);
    lv_obj_set_style_pad_right(card, 12, 0);
    lv_obj_set_style_pad_top(card, 8, 0);
    lv_obj_set_style_pad_bottom(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (slot->desc.build)
    {
        slot->desc.build(card, slot->desc.user_data);
    }
    else
    {
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, slot->desc.title ? slot->desc.title : "Card");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        cos_label_set_font_size(lbl, COS_FONT_SIZE_SMALL);
    }

    if (slot->desc.click)
    {
        lv_obj_add_event_cb(card, _card_clicked_cb, LV_EVENT_CLICKED, slot);
    }
    return card;
}

static int _count_used(void)
{
    int n = 0;
    for (int i = 0; i < COS_CARDS_PAGE_MAX_CARDS; i++)
        if (_cards[i].used) n++;
    return n;
}

static void _rebuild_stack(cos_cards_page_t *cp)
{
    if (!cp || !cp->cards)
        return;

    /* 1. delete current card objects (header is NOT in the registry) */
    for (int i = 0; i < COS_CARDS_PAGE_MAX_CARDS; i++)
    {
        if (_cards[i].used && _cards[i].obj && lv_obj_is_valid(_cards[i].obj))
            lv_obj_delete(_cards[i].obj);
        _cards[i].obj = NULL;
    }

    /* 2. collect used slots and sort by priority ascending */
    int order[COS_CARDS_PAGE_MAX_CARDS];
    int cnt = 0;
    for (int i = 0; i < COS_CARDS_PAGE_MAX_CARDS; i++)
        if (_cards[i].used) order[cnt++] = i;

    for (int a = 1; a < cnt; a++)
    {
        int key = order[a], b = a - 1;
        while (b >= 0 && _cards[order[b]].desc.priority > _cards[key].desc.priority)
        {
            order[b + 1] = order[b];
            b--;
        }
        order[b + 1] = key;
    }

    /* 3. create in priority order */
    for (int k = 0; k < cnt; k++)
        _cards[order[k]].obj = _make_card(cp, &_cards[order[k]]);

    cp->card_count = (uint8_t)cnt;
}

/* ============== Construction ============== */

static cos_cards_page_t *cos_cards_page_create(lv_obj_t *parent)
{
    cos_cards_page_t *cp = cos_malloc_zeroed(sizeof(cos_cards_page_t));
    COS_CHECK_PTR_RETURN_VAL(cp, NULL);

    /* Swipe panel — pulled UP from the bottom edge (the remaining 50px
     * touch strip) reveals the page. */
    cos_swipe_panel_t *swipe_panel = cos_swipe_panel_create(parent);
    cos_swipe_panel_set_dir(swipe_panel, COS_SWIPE_DIR_UP);
    cos_swipe_panel_hide_handle_bar(swipe_panel);
    /* Let the whole cards page be dragged down to dismiss once open — not
     * just the 50px top strip. */
    cos_swipe_panel_set_full_drag(swipe_panel, true);
    cos_crown_encoder_register_slide_widget(swipe_panel->sw);
    cp->swipe_panel = swipe_panel;

    /* Card stack container inside the swipe panel */
    lv_obj_t *content = lv_obj_create(swipe_panel->swipe_obj);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(content, 30, 0);
    lv_obj_set_style_pad_bottom(content, 20, 0);
    lv_obj_set_style_pad_column(content, 0, 0);
    lv_obj_set_style_pad_row(content, _CARD_GAP, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    cp->cards = content;

    /* Header */
    cp->header = _cards_page_create_header(cp, content);

    /* 1Hz timer to refresh time/date */
    static lv_timer_t *t = NULL;
    if (!t)
    {
        t = lv_timer_create(_cards_page_update_time_cb, 1000, cp);
    }
    else
    {
        lv_timer_set_user_data(t, cp);
        lv_timer_resume(t);
    }
    _cards_page_update_time_cb(t);

    return cp;
}

/* ============== Public API ============== */

void cos_cards_page_init(void)
{
    if (cards_page_instance)
        return;
    cards_page_instance = cos_cards_page_create(cos_overlay_get_overlay_layer());
    if (!cards_page_instance)
        return;

    /* NO built-in cards are seeded at boot: these cards have no matching apps
     * yet. When an app that owns a card is installed it registers its card
     * through the SAME public cos_cards_page_register_card() API, and the
     * framework rebuilds the stack. The underlying register / unregister /
     * de-dup / rebuild machinery is kept fully intact. */
    cos_cards_page_refresh();
    /* 按设计移除底部边缘上滑打开卡片页：创建后立即隐藏触摸条，系统任何
     * 界面（含主界面）都不再显示该边缘条，底部上滑不再触发任何动作。
     * register_card API 保留，未来若恢复该交互只需去掉此行并让 watchface
     * 重新调用 show()。 */
    cos_cards_page_hide();
    COS_LOG_I("Cards page initialized [%p] (hidden, up-swipe edge disabled)", cards_page_instance);
}

cos_cards_page_t *cos_cards_page_get_instance(void)
{
    return cards_page_instance;
}

cos_card_handle_t cos_cards_page_register_card(const cos_card_desc_t *desc)
{
    if (!desc || !desc->id)
        return NULL;

    /* De-duplicate by id: replace in place. */
    for (int i = 0; i < COS_CARDS_PAGE_MAX_CARDS; i++)
    {
        if (_cards[i].used && _cards[i].desc.id && strcmp(_cards[i].desc.id, desc->id) == 0)
        {
            _cards[i].desc = *desc;
            _cards[i].obj = NULL;
            _rebuild_stack(cards_page_instance);
            return (cos_card_handle_t)&_cards[i];
        }
    }

    int free = -1;
    for (int i = 0; i < COS_CARDS_PAGE_MAX_CARDS; i++)
    {
        if (!_cards[i].used)
        {
            free = i;
            break;
        }
    }
    if (free < 0)
        return NULL;

    _cards[free].desc = *desc;
    _cards[free].used = true;
    _cards[free].obj = NULL;
    _rebuild_stack(cards_page_instance);
    return (cos_card_handle_t)&_cards[free];
}

void cos_cards_page_unregister_card(cos_card_handle_t card)
{
    if (!card)
        return;
    struct cos_card_t *slot = (struct cos_card_t *)card;
    slot->used = false;
    if (slot->obj && lv_obj_is_valid(slot->obj))
        lv_obj_delete(slot->obj);
    slot->obj = NULL;
    _rebuild_stack(cards_page_instance);
}

void cos_cards_page_refresh(void)
{
    _rebuild_stack(cards_page_instance);
}

lv_obj_t *cos_cards_page_add_card(lv_color_t accent, const char *title)
{
    static char auto_ids[COS_CARDS_PAGE_MAX_CARDS][24];
    static int auto_seq = 0;
    int seq = auto_seq++ % COS_CARDS_PAGE_MAX_CARDS;
    snprintf(auto_ids[seq], sizeof(auto_ids[seq]), "auto_%d", seq);

    cos_card_desc_t d = {
        .id = auto_ids[seq],
        .title = title,
        .accent = accent,
        .priority = (uint8_t)(20 + seq),
        .build = NULL,
        .click = NULL,
        .user_data = NULL,
    };
    cos_card_handle_t h = cos_cards_page_register_card(&d);
    if (!h)
        return NULL;
    return ((struct cos_card_t *)h)->obj;
}

void cos_cards_page_show(void)
{
    COS_CHECK_PTR_RETURN(cards_page_instance);
    lv_obj_remove_flag(cos_slide_widget_get_touch_obj(cards_page_instance->swipe_panel->sw), LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cards_page_instance->swipe_panel->swipe_obj, LV_OBJ_FLAG_HIDDEN);
}

void cos_cards_page_hide(void)
{
    COS_CHECK_PTR_RETURN(cards_page_instance);
    lv_obj_add_flag(cos_slide_widget_get_touch_obj(cards_page_instance->swipe_panel->sw), LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_get_y(cards_page_instance->swipe_panel->swipe_obj) < COS_DISPLAY_HEIGHT)
        lv_obj_add_flag(cards_page_instance->swipe_panel->swipe_obj, LV_OBJ_FLAG_HIDDEN);
}

void cos_cards_page_slide_up(void)
{
    COS_CHECK_PTR_RETURN(cards_page_instance);
    cos_swipe_panel_slide_down(cards_page_instance->swipe_panel);
}

/* ============== Overlay descriptor ============== */

static void _cards_page_overlay_pull_back(void)
{
    if (cards_page_instance && cards_page_instance->swipe_panel)
        cos_swipe_panel_pull_back(cards_page_instance->swipe_panel);
}

static void _cards_page_overlay_hide(void)
{
    cos_cards_page_hide();
}

static void _cards_page_overlay_on_focus(void)
{
    COS_LOG_D("Cards page focused");
}

static bool _cards_page_overlay_is_open(void)
{
    if (cards_page_instance && cards_page_instance->swipe_panel &&
        cards_page_instance->swipe_panel->sw)
    {
        return cos_slide_widget_get_state(cards_page_instance->swipe_panel->sw) == COS_SLIDE_WIDGET_STATE_OPEN;
    }
    return false;
}

static lv_obj_t *_cards_page_overlay_get_scrollable(void)
{
    return cards_page_instance ? cards_page_instance->cards : NULL;
}

static lv_obj_t *_cards_page_overlay_get_foreground_obj(void)
{
    if (cards_page_instance && cards_page_instance->swipe_panel &&
        cards_page_instance->swipe_panel->sw)
    {
        return cos_slide_widget_get_touch_obj(cards_page_instance->swipe_panel->sw);
    }
    return NULL;
}

const cos_chrome_overlay_t *cos_cards_page_get_overlay_descriptor(void)
{
    return &_cards_page_overlay;
}
