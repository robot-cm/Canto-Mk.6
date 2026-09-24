/**
 * @file cos_radio_page.c
 * @brief Radio page - single-selection list page
 */

#include "cos_radio_page.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "RadioPage"
#include "cos_log.h"
#include "cos_port.h"
#include "cos_basic_widgets.h"
#include "cos_corner_radius.h"
#include "cos_app_header.h"
#include "cos_theme.h"
#include "cos_icon.h"
#include "cos_mem.h"
#include "cos_activity.h"

/* Macros and Definitions -------------------------------------*/
#define _RADIO_ITEM_HEIGHT 100
#define _RADIO_ITEM_PRESSED_SCALE 240

#define _RADIO_ITEM_TITLE_LABEL_INDEX 0
#define _RADIO_ITEM_CHECK_LABEL_INDEX 1
struct cos_radio_page_t
{
    lv_obj_t *screen;
    lv_obj_t *subtitle_label;
    lv_obj_t *radio_item_container;
    lv_obj_t *comment_label;
    lv_obj_t *last_right_label;
    lv_obj_t *last_item;
    cos_activity_t *activity;
    uint32_t selected_index;
    uint32_t item_number;
};
/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

static void _radio_item_check(cos_radio_page_t *rp, lv_obj_t *right_label)
{
    if (rp->last_right_label == right_label)
        return;
    lv_obj_remove_flag(right_label, LV_OBJ_FLAG_HIDDEN);
    if (rp->last_right_label)
    {
        lv_obj_add_flag(rp->last_right_label, LV_OBJ_FLAG_HIDDEN);
    }
    rp->last_right_label = right_label;
}

static void _radio_item_clicked_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    uint32_t index = (uint32_t)lv_obj_get_user_data(obj);
    cos_radio_page_t *rp = lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(rp);
    lv_obj_t *label = lv_obj_get_child(obj, _RADIO_ITEM_CHECK_LABEL_INDEX);
    COS_CHECK_PTR_RETURN(label);
    _radio_item_check(rp, label);
    lv_obj_send_event(rp->radio_item_container, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)index);
}

void cos_radio_page_check(cos_radio_page_t *rp, uint32_t index)
{
    COS_CHECK_PTR_RETURN(rp);
    lv_obj_t *item = lv_obj_get_child(rp->radio_item_container, index);
    COS_CHECK_PTR_RETURN(item);
    lv_obj_t *check_label = lv_obj_get_child(item, _RADIO_ITEM_CHECK_LABEL_INDEX);
    COS_CHECK_PTR_RETURN(check_label);
    _radio_item_check(rp, check_label);
}

uint32_t cos_radio_page_add_item(cos_radio_page_t *rp, const char *txt)
{
    COS_CHECK_PTR_RETURN_VAL(rp && txt && rp->radio_item_container, COS_INVALID_RADIO_INDEX);
    lv_obj_t *item = lv_button_create(rp->radio_item_container);
    lv_obj_set_size(item, lv_pct(100), _RADIO_ITEM_HEIGHT);
    lv_obj_set_style_bg_color(item, COS_THEME_BUTTON_COLOR, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(item, 0, 0);
    lv_obj_set_style_pad_hor(item, COS_LIST_CONTAINER_PAD_ALL, 0);
    lv_obj_set_style_margin_all(item, 0, 0);

    lv_obj_update_layout(item);
    lv_obj_set_style_transform_pivot_x(item, lv_obj_get_width(item) / 2, 0);
    lv_obj_set_style_transform_pivot_y(item, lv_obj_get_height(item) / 2, 0);
    lv_obj_set_style_transform_scale(item, LV_SCALE_NONE, LV_STATE_DEFAULT);
    lv_obj_set_style_transform_scale(item, _RADIO_ITEM_PRESSED_SCALE, LV_STATE_PRESSED);

    lv_obj_t *title_label = lv_label_create(item);
    lv_label_set_text(title_label, txt);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *check_label = lv_label_create(item);
    lv_label_set_text(check_label, RI_CHECK_FILL);
    lv_obj_set_style_text_color(check_label, COS_COLOR_TEXT_GREY, 0);
    lv_obj_align(check_label, LV_ALIGN_RIGHT_MID, 0, 0);
    if (rp->item_number == 0)
    {
        _radio_item_check(rp, check_label);
        cos_obj_set_corner_radius_bg(item,
                                     COS_ROUND_TOP_LEFT | COS_ROUND_TOP_RIGHT,
                                     COS_ITEM_RADIUS,
                                     COS_THEME_BUTTON_COLOR);
    }
    else
    {
        if (rp->item_number > 1)
        {
            cos_obj_remove_corner_radius_bg(rp->last_item);
        }
        cos_obj_set_corner_radius_bg(item,
                                     COS_ROUND_BOTTOM_LEFT | COS_ROUND_BOTTOM_RIGHT,
                                     COS_ITEM_RADIUS,
                                     COS_THEME_BUTTON_COLOR);
        lv_obj_add_flag(check_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_move_to_index(title_label, _RADIO_ITEM_TITLE_LABEL_INDEX);
    lv_obj_move_to_index(check_label, _RADIO_ITEM_CHECK_LABEL_INDEX);

    lv_obj_set_user_data(item, (void *)(intptr_t)rp->item_number);
    lv_obj_add_event_cb(item, _radio_item_clicked_cb, LV_EVENT_CLICKED, rp);

    rp->last_item = item;
    uint32_t item_index = rp->item_number;
    rp->item_number++;

    return item_index;
}

void cos_radio_page_set_subtitle(cos_radio_page_t *rp, const char *subtitle)
{
    COS_CHECK_PTR_RETURN(rp && subtitle && rp->subtitle_label);
    lv_label_set_text(rp->subtitle_label, subtitle);
}

void cos_radio_page_set_comment(cos_radio_page_t *rp, const char *comment)
{
    COS_CHECK_PTR_RETURN(rp && comment && rp->comment_label);
    lv_label_set_text(rp->comment_label, comment);
}

void cos_radio_page_add_event_cb(cos_radio_page_t *rp, lv_event_cb_t event_cb, void *user_data)
{
    COS_CHECK_PTR_RETURN(rp && rp->radio_item_container);
    lv_obj_add_event_cb(rp->radio_item_container, event_cb, LV_EVENT_VALUE_CHANGED, user_data);
}

static const cos_activity_lifecycle_t radio_page_lifecycle = {
    .on_enter = NULL,
    .on_destroy = NULL,
    .on_pause = NULL,
    .on_resume = NULL,
};

cos_radio_page_t *cos_radio_page_create(const char *title)
{
    cos_radio_page_t *rp = cos_malloc_zeroed(sizeof(cos_radio_page_t));
    COS_CHECK_PTR_RETURN_VAL(rp, NULL);

    rp->item_number = 0;
    rp->selected_index = 0;

    cos_activity_t *a = cos_activity_create(&radio_page_lifecycle);
    COS_CHECK_PTR_RETURN_VAL(a, NULL);
    cos_activity_set_title(a, title);
    cos_activity_set_app_header_visible(a, true);
    /* 恢复 header 时钟(Settings 系列页面将其隐藏) */
    cos_app_header_set_clock_visible(true);
    lv_obj_t *view = cos_activity_get_view(a);

    lv_obj_t *list = cos_list_create(view);

    rp->subtitle_label = cos_list_add_title(list, title);

    lv_obj_t *con = lv_obj_create(list);
    lv_obj_remove_style_all(con);
    lv_obj_set_size(con, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(con, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(con, 0, 0);
    lv_obj_set_style_pad_all(con, 0, 0);
    lv_obj_set_style_clip_corner(con, true, 0);
    lv_obj_set_flex_flow(con, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(con, 1, 0);
    lv_obj_set_style_pad_top(con, 0, 0);
    lv_obj_set_style_pad_bottom(con, 0, 0);

    rp->radio_item_container = con;
    rp->comment_label = cos_list_add_comment(list, "");
    rp->activity = a;

    cos_activity_set_user_data(a, rp);
    return rp;
}

void cos_radio_page_show(cos_radio_page_t *rp)
{
    COS_CHECK_PTR_RETURN(rp);
    cos_activity_enter(rp->activity);
}
