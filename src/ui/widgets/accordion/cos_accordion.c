/**
 * @file cos_accordion.c
 * @brief Accordion widget implementation
 */

#include "cos_accordion.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "cos_icon.h"
#include "cos_mem.h"
#include "cos_theme.h"
/* Macros and Definitions -------------------------------------*/
#define _TITLE_BAR_HEIGHT 50
#define _CONTENT_MIN_HEIGHT 0
#define _ANIM_DURATION 220
#define _TITLE_MARGIN_LEFT 15
#define _ARROW_MARGIN_RIGHT 15

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/
static void _title_bar_click_cb(lv_event_t *e);
static void _update_arrow_icon(cos_accordion_t *accordion);
static void _update_content_height(cos_accordion_t *accordion, bool anim);
static void _container_delete_cb(lv_event_t *e);

cos_accordion_t *cos_accordion_create(lv_obj_t *parent, const char *title)
{
    cos_accordion_t *accordion = (cos_accordion_t *)cos_malloc(sizeof(cos_accordion_t));
    if (!accordion)
    {
        return NULL;
    }

    accordion->container = lv_obj_create(parent);
    lv_obj_remove_style_all(accordion->container);
    lv_obj_set_width(accordion->container, lv_pct(100));
    lv_obj_set_height(accordion->container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(accordion->container, COS_COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(accordion->container, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(accordion->container, _container_delete_cb, LV_EVENT_DELETE, accordion);

    accordion->title_bar = lv_obj_create(accordion->container);
    lv_obj_remove_style_all(accordion->title_bar);
    lv_obj_set_width(accordion->title_bar, lv_pct(100));
    lv_obj_set_height(accordion->title_bar, _TITLE_BAR_HEIGHT);
    lv_obj_set_style_bg_color(accordion->title_bar, lv_color_hex(0x91A4BF), 0);
    lv_obj_align(accordion->title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(accordion->title_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(accordion->title_bar, _title_bar_click_cb, LV_EVENT_CLICKED, accordion);

    accordion->title_label = lv_label_create(accordion->title_bar);
    lv_label_set_text(accordion->title_label, title);
    lv_obj_set_style_text_color(accordion->title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(accordion->title_label, LV_ALIGN_LEFT_MID, _TITLE_MARGIN_LEFT, 0);

    accordion->arrow_label = lv_label_create(accordion->title_bar);
    lv_label_set_text(accordion->arrow_label, RI_ARROW_DOWN_S_LINE);
    lv_obj_set_style_text_color(accordion->arrow_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(accordion->arrow_label, LV_ALIGN_RIGHT_MID, -_ARROW_MARGIN_RIGHT, 0);

    accordion->content = lv_obj_create(accordion->container);
    lv_obj_remove_style_all(accordion->content);
    lv_obj_set_width(accordion->content, lv_pct(100));
    lv_obj_set_height(accordion->content, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(accordion->content, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(accordion->content, LV_ALIGN_TOP_MID, 0, _TITLE_BAR_HEIGHT);

    accordion->content_height = lv_obj_get_height(accordion->content);
    if (accordion->content_height <= 0)
    {
        accordion->content_height = 100;
    }

    lv_obj_set_height(accordion->content, 0);

    accordion->state = COS_ACCORDION_STATE_CLOSED;

    return accordion;
}

static void _title_bar_click_cb(lv_event_t *e)
{
    cos_accordion_t *accordion = (cos_accordion_t *)lv_event_get_user_data(e);
    if (!accordion)
    {
        return;
    }

    cos_accordion_toggle(accordion, true);
}

static void _update_arrow_icon(cos_accordion_t *accordion)
{
    if (!accordion || !accordion->arrow_label)
    {
        return;
    }

    if (accordion->state == COS_ACCORDION_STATE_OPEN)
    {
        lv_label_set_text(accordion->arrow_label, RI_ARROW_UP_S_LINE);
    }
    else
    {
        lv_label_set_text(accordion->arrow_label, RI_ARROW_DOWN_S_LINE);
    }
}

static void _update_content_height(cos_accordion_t *accordion, bool anim)
{
    if (!accordion || !accordion->content)
    {
        return;
    }

    if (accordion->state == COS_ACCORDION_STATE_OPEN)
    {
        if (anim)
        {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, accordion->content);
            lv_anim_set_values(&a, 0, accordion->content_height);
            lv_anim_set_duration(&a, _ANIM_DURATION);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_height);
            lv_anim_start(&a);
        }
        else
        {
            lv_obj_set_height(accordion->content, accordion->content_height);
        }
    }
    else
    {
        if (anim)
        {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, accordion->content);
            lv_anim_set_values(&a, accordion->content_height, 0);
            lv_anim_set_duration(&a, _ANIM_DURATION);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_height);
            lv_anim_set_completed_cb(&a, NULL);
            lv_anim_start(&a);
        }
        else
        {
            lv_obj_set_height(accordion->content, 0);
        }
    }
}

static void _container_delete_cb(lv_event_t *e)
{
    cos_accordion_t *accordion = (cos_accordion_t *)lv_event_get_user_data(e);
    if (!accordion)
    {
        return;
    }

    cos_free(accordion);
}

void cos_accordion_toggle(cos_accordion_t *accordion, bool anim)
{
    if (!accordion)
    {
        return;
    }

    cos_accordion_state_t new_state =
        (accordion->state == COS_ACCORDION_STATE_OPEN) ? COS_ACCORDION_STATE_CLOSED : COS_ACCORDION_STATE_OPEN;

    cos_accordion_set_state(accordion, new_state, anim);
}

void cos_accordion_set_state(cos_accordion_t *accordion, cos_accordion_state_t state, bool anim)
{
    if (!accordion)
    {
        return;
    }

    if (accordion->state == state)
    {
        return;
    }

    accordion->state = state;
    _update_arrow_icon(accordion);
    _update_content_height(accordion, anim);
}

cos_accordion_state_t cos_accordion_get_state(cos_accordion_t *accordion)
{
    if (!accordion)
    {
        return COS_ACCORDION_STATE_CLOSED;
    }

    return accordion->state;
}

void cos_accordion_delete(cos_accordion_t *accordion)
{
    if (!accordion)
    {
        return;
    }

    if (accordion->container && lv_obj_is_valid(accordion->container))
    {
        lv_obj_delete(accordion->container);
    }
}
