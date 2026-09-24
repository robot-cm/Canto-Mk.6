/**
 * @file cos_msg_list.c
 * @brief Drop-down message list
 */

#include "cos_msg_list.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_lang.h"
#include "cos_anim.h"
#define COS_LOG_DISABLE
#define COS_LOG_TAG "MessageList"
#include "cos_log.h"
#include "cos_image.h"
#include "cos_config.h"
#include "cos_theme.h"
#include "cos_slide_widget.h"
#include "cos_event.h"
#include "cos_port.h"
#include "cos_anim.h"
#include "cos_mem.h"
#include "cos_crown.h"
#include "cos_panel.h"
#include "cos_chrome_manager.h"
#include "cos_overlay_layer.h"
/* Macros and Definitions -------------------------------------*/
#define _DEBUG_LAYOUT 0

#define _MSG_LIST_ITEM_MARGIN_BOTTOM 25

#define _ICON_WIDTH 64
#define _ICON_HEIGHT _ICON_WIDTH

#define _ICON_LARGE_WIDTH 80
#define _ICON_LARGE_HEIGHT _ICON_LARGE_WIDTH

#define _ITEM_CLICK_THRESHOLD 3
// Swipe-to-delete related macros
#define _SWIPE_THRESHOLD (COS_DISPLAY_WIDTH / 2) // Swipe distance threshold for deletion
#define _SWIPE_ANIM_DURATION 200 // Swipe animation duration
#define _DETAIL_ANIM_DURATION 250

typedef struct
{
    cos_panel_t *panel;
    cos_msg_list_item_t *item;
    lv_obj_t *item_container;
} detail_page_data_t;

/* Variables --------------------------------------------------*/
static detail_page_data_t *_detail_data = NULL;
static cos_msg_list_t *message_list_instance = NULL;
static void _msg_list_overlay_pull_back(void);
static void _msg_list_overlay_hide(void);
static void _msg_list_overlay_on_focus(void);
static bool _msg_list_overlay_is_open(void);
static lv_obj_t *_msg_list_overlay_get_scrollable(void);
static lv_obj_t *_msg_list_overlay_get_foreground_obj(void);
static const cos_chrome_overlay_t _msg_list_overlay = {
    .pull_back = _msg_list_overlay_pull_back,
    .hide = _msg_list_overlay_hide,
    .on_focus = _msg_list_overlay_on_focus,
    .is_open = _msg_list_overlay_is_open,
    .get_scrollable = _msg_list_overlay_get_scrollable,
    .get_foreground_obj = _msg_list_overlay_get_foreground_obj,
    .name = "msg_list",
};
/* Function Implementations -----------------------------------*/

static void _msg_list_item_clicked_cb(lv_event_t *e);

/************************** Delete Item Callback **************************/

static void _del_item_cb(cos_msg_list_t *list)
{
    COS_CHECK_PTR_RETURN(list);
    bool has_items = false;
    uint32_t child_count = lv_obj_get_child_count(list->list);
    for (uint32_t i = 0; i < child_count; i++)
    {
        lv_obj_t *child = lv_obj_get_child(list->list, i);
        if (child == list->clear_all_btn)
        {
            continue;
        }
        if (lv_obj_get_user_data(child) != NULL)
        {
            has_items = true;
            break;
        }
    }

    if (!has_items)
    {
        if (list->no_msg_label)
        {
            lv_obj_remove_flag(list->no_msg_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (list->clear_all_btn)
        {
            lv_obj_add_flag(list->clear_all_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/************************** Swipe-to-Delete Related Callbacks **************************/

static void _slide_widget_reached_threashold_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    cos_msg_list_t *list = (cos_msg_list_t *)lv_event_get_user_data(e);
    cos_msg_list_item_t *item = (cos_msg_list_item_t *)lv_obj_get_user_data(obj);
    if (item)
    {
        cos_msg_list_item_delete(item);
        _del_item_cb(list);
    }
}

/************************** Detail Page Related Callbacks **************************/

static void _set_item_opa_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (obj && lv_obj_is_valid(obj))
    {
        lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
    }
}

static void _delete_anim_end_cb(cos_anim_t *a)
{
    if (!_detail_data)
        return;

    cos_msg_list_item_t *item = _detail_data->item;

    if (_detail_data->panel)
    {
        cos_panel_delete(_detail_data->panel);
    }

    if (item && !item->is_deleted)
    {
        cos_msg_list_t *msg_list = item->msg_list;
        cos_msg_list_item_delete(item);
        if (msg_list)
        {
            _del_item_cb(msg_list);
        }
    }

    cos_free(_detail_data);
    _detail_data = NULL;
}

static void _dismiss_anim_end_cb(cos_anim_t *a)
{
    if (!_detail_data)
        return;

    if (_detail_data->panel)
    {
        cos_panel_delete(_detail_data->panel);
    }

    cos_free(_detail_data);
    _detail_data = NULL;
}

static void _dismiss_btn_click_cb(lv_event_t *e)
{
    if (!_detail_data)
        return;

    lv_obj_t *item_cont = _detail_data->item_container;
    if (item_cont)
    {
        lv_anim_t item_anim;
        lv_anim_init(&item_anim);
        lv_anim_set_var(&item_anim, item_cont);
        lv_anim_set_values(&item_anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_exec_cb(&item_anim, _set_item_opa_cb);
        lv_anim_set_path_cb(&item_anim, lv_anim_path_ease_in_out);
        lv_anim_set_duration(&item_anim, _DETAIL_ANIM_DURATION);
        lv_anim_start(&item_anim);
    }

    cos_anim_t *scale_anim =
        cos_anim_transform_scale_create(_detail_data->panel->container, 256, 0, _DETAIL_ANIM_DURATION, false);
    if (scale_anim)
    {
        cos_anim_add_cb(scale_anim, _dismiss_anim_end_cb, NULL);
        cos_anim_start(scale_anim);
    }
    else
    {
        if (item_cont)
        {
            lv_obj_set_style_opa(item_cont, LV_OPA_COVER, 0);
        }
        cos_panel_delete(_detail_data->panel);
        cos_free(_detail_data);
        _detail_data = NULL;
    }
}

static void _mark_as_read_btn_click_cb(lv_event_t *e)
{
    if (!_detail_data)
        return;

    cos_anim_t *fade_anim =
        cos_anim_fade_create(_detail_data->panel->container, LV_OPA_COVER, LV_OPA_TRANSP, _DETAIL_ANIM_DURATION, false);
    if (fade_anim)
    {
        cos_anim_add_cb(fade_anim, _delete_anim_end_cb, NULL);
        cos_anim_start(fade_anim);
    }
    else
    {
        cos_msg_list_item_t *item = _detail_data->item;
        cos_msg_list_t *msg_list = item ? item->msg_list : NULL;
        cos_panel_delete(_detail_data->panel);
        if (item && !item->is_deleted)
        {
            cos_msg_list_item_delete(item);
            if (msg_list)
            {
                _del_item_cb(msg_list);
            }
        }
        cos_free(_detail_data);
        _detail_data = NULL;
    }
}

static void _msg_list_item_clicked_cb(lv_event_t *e)
{
    cos_slide_widget_t *sw = (cos_slide_widget_t *)lv_event_get_param(e);
    if (abs(cos_slide_widget_get_displacement(sw)) > _ITEM_CLICK_THRESHOLD)
        return;

    lv_obj_t *item_container = lv_event_get_current_target(e);
    cos_msg_list_item_t *item = (cos_msg_list_item_t *)lv_obj_get_user_data(item_container);
    COS_CHECK_PTR_RETURN(item && !item->is_deleted && item->container && !_detail_data);

    _detail_data = cos_malloc_zeroed(sizeof(detail_page_data_t));
    COS_CHECK_PTR_RETURN(_detail_data);
    _detail_data->item = item;
    _detail_data->item_container = item->container;

    const void *icon_src = item->icon ? lv_image_get_src(item->icon) : NULL;

    cos_panel_cfg_t cfg = {
        .icon_bg_color = COS_COLOR_BLACK,
        .icon_type = icon_src ? COS_PANEL_ICON_TYPE_IMAGE : COS_PANEL_ICON_TYPE_NONE,
        .icon_src = icon_src,
        .title_text = lv_label_get_text(item->title_label),
        .message_text = item->msg_str,
        .confirm_btn_id = STR_ID_APP_FLASH_LIGHT_DISMISS,
        .confirm_cb = _dismiss_btn_click_cb,
        .cancel_btn_id = STR_ID_MSG_LIST_ITEM_MARK_AS_READ,
        .cancel_cb = _mark_as_read_btn_click_cb,
    };

    cos_panel_t *panel = cos_panel_create(lv_layer_top(), &cfg);
    if (!panel)
    {
        cos_free(_detail_data);
        _detail_data = NULL;
        return;
    }

    _detail_data->panel = panel;

    lv_obj_set_style_bg_color(panel->container, COS_COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(panel->container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel->container, COS_DISPLAY_RADIUS, 0);

    if (panel->icon)
    {
        lv_obj_set_size(panel->icon, _ICON_LARGE_WIDTH, _ICON_LARGE_HEIGHT);
        lv_obj_t *icon_img = lv_obj_get_child(panel->icon, 0);
        if (icon_img)
        {
            cos_img_set_size(icon_img, _ICON_LARGE_WIDTH, _ICON_LARGE_HEIGHT);
        }
    }

    lv_obj_set_style_opa_layered(panel->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_scale(panel->container, 0, 0);

    lv_area_t item_coords;
    lv_obj_get_coords(item->container, &item_coords);
    int32_t item_center_x = (item_coords.x1 + item_coords.x2) / 2;
    int32_t item_center_y = (item_coords.y1 + item_coords.y2) / 2;
    lv_obj_set_style_transform_pivot_x(panel->container, item_center_x, 0);
    lv_obj_set_style_transform_pivot_y(panel->container, item_center_y, 0);

    {
        lv_anim_t item_anim;
        lv_anim_init(&item_anim);
        lv_anim_set_var(&item_anim, item->container);
        lv_anim_set_values(&item_anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_exec_cb(&item_anim, _set_item_opa_cb);
        lv_anim_set_path_cb(&item_anim, lv_anim_path_ease_in_out);
        lv_anim_set_duration(&item_anim, _DETAIL_ANIM_DURATION);
        lv_anim_start(&item_anim);
    }

    cos_anim_t *scale_anim = cos_anim_transform_scale_create(panel->container, 0, 256, _DETAIL_ANIM_DURATION, false);
    if (scale_anim)
    {
        cos_anim_start(scale_anim);
    }

    cos_anim_t *fade_anim =
        cos_anim_fade_create(panel->container, LV_OPA_TRANSP, LV_OPA_COVER, _DETAIL_ANIM_DURATION, false);
    if (fade_anim)
    {
        cos_anim_start(fade_anim);
    }
}

/************************** Public Functions**************************/

cos_msg_list_item_t *cos_msg_list_item_create(cos_msg_list_t *list)
{
    cos_msg_list_item_t *item = cos_malloc_zeroed(sizeof(cos_msg_list_item_t));
    COS_CHECK_PTR_RETURN_VAL(list, NULL);
    COS_CHECK_PTR_RETURN_VAL_FREE(item, NULL, item);

    item->msg_list = list;
    item->is_deleted = false;
    // Create container
    item->container = lv_button_create(list->list);
    COS_CHECK_PTR_RETURN_VAL_FREE(item->container, NULL, item);
    lv_obj_set_size(item->container, lv_pct(100), LV_SIZE_CONTENT);
    // Vertical layout for item
    lv_obj_set_flex_flow(item->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(item->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(item->container, COS_COLOR_DARK_GREY_2, 0);
    lv_obj_set_style_border_width(item->container, 0, 0);
    lv_obj_set_style_margin_bottom(item->container, _MSG_LIST_ITEM_MARGIN_BOTTOM, 0);
    lv_obj_set_style_pad_all(item->container, 20, 0);
    lv_obj_set_style_align(item->container, LV_ALIGN_CENTER, 0);
    lv_obj_set_style_radius(item->container, 30, 0);
    lv_obj_set_user_data(item->container, item);
    lv_obj_set_style_translate_x(item->container, 0, 0);
    lv_obj_set_style_shadow_width(item->container, 0, 0);
    lv_obj_remove_flag(item->container, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_update_layout(item->container);
    cos_slide_widget_t *sw = cos_slide_widget_create_with_touch(item->container,
                                                                item->container,
                                                                COS_SLIDE_DIR_HOR,
                                                                COS_DISPLAY_WIDTH,
                                                                COS_THRESHOLD_40);
    cos_slide_widget_set_bidirectional(sw, true);
    cos_slide_widget_add_event_cb_reached_threshold(sw, _slide_widget_reached_threashold_cb, list);
    cos_slide_widget_add_event_cb_reverted(sw, _msg_list_item_clicked_cb, NULL);
    // First row of item layout
    item->row1 = lv_obj_create(item->container);
    lv_obj_set_size(item->row1, lv_pct(100), 64);
    lv_obj_set_style_bg_opa(item->row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item->row1, 0, 0);
    lv_obj_set_style_pad_all(item->row1, 0, 0);
    lv_obj_remove_flag(item->row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(item->row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item->row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);
#if _DEBUG_LAYOUT
    lv_obj_set_style_bg_opa(item->row1, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(item->row1, COS_COLOR_RED, 0);
#endif
    // Icon area
    item->icon = lv_image_create(item->row1);
    lv_obj_set_size(item->icon, _ICON_WIDTH, _ICON_HEIGHT);
    lv_obj_set_style_bg_opa(item->icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item->icon, 0, 0);
    lv_obj_set_style_margin_all(item->icon, 0, 0);
    lv_obj_remove_flag(item->icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(item->icon, COS_IMG_APP);
    cos_img_set_size(item->icon, _ICON_WIDTH, _ICON_HEIGHT);

    // Title label
    item->title_label = lv_label_create(item->row1);
    lv_label_set_long_mode(item->title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(item->title_label, 1);

    // Time label
    item->time_label = lv_label_create(item->row1);
    lv_obj_set_style_text_align(item->time_label, LV_TEXT_ALIGN_RIGHT, 0);

    // Message content
    item->msg_label = lv_label_create(item->container);
    lv_obj_set_width(item->msg_label, lv_pct(100));
    lv_label_set_long_mode(item->msg_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_margin_ver(item->msg_label, 16, 0);
    lv_obj_align_to(item->msg_label, item->row1, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Set event bubble to avoid affecting container's Clicked event
    lv_obj_add_flag(item->row1, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(item->icon, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(item->title_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(item->time_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(item->msg_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Update UI state when adding new message
    if (list->no_msg_label)
    {
        lv_obj_add_flag(list->no_msg_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (list->clear_all_btn)
    {
        lv_obj_remove_flag(list->clear_all_btn, LV_OBJ_FLAG_HIDDEN);
    }

    return item;
}

void cos_msg_list_item_delete(cos_msg_list_item_t *item)
{
    COS_CHECK_PTR_RETURN(item);

    // Clear container user data
    if (item->container)
    {
        lv_obj_set_user_data(item->container, NULL);
    }

    // Free message string
    if (item->msg_str)
    {
        cos_free(item->msg_str);
        item->msg_str = NULL;
    }

    // Delete container directly (LVGL will automatically delete child objects)
    if (item->container)
    {
        lv_obj_delete_async(item->container);
        item->container = NULL;
    }

    // Free struct
    cos_free(item);
}

void cos_msg_list_item_set_msg(cos_msg_list_item_t *item, const char *msg)
{
    COS_CHECK_PTR_RETURN(item);

    // Free old message
    if (item->msg_str)
    {
        cos_free(item->msg_str);
        item->msg_str = NULL;
    }

    if (!msg || strlen(msg) == 0)
    {
        lv_label_set_text(item->msg_label, "");
    }
    else
    {
        // Allocate new memory and copy
        item->msg_str = cos_malloc(strlen(msg) + 1);
        if (item->msg_str)
        {
            strcpy(item->msg_str, msg);
            lv_label_set_text(item->msg_label, item->msg_str);
        }
        else
        {
            lv_label_set_text(item->msg_label, "");
        }
    }
}

void cos_msg_list_item_set_title(cos_msg_list_item_t *item, const char *title)
{
    COS_CHECK_PTR_RETURN(item);
    lv_label_set_text(item->title_label, title ? title : "");
}

void cos_msg_list_item_set_time(cos_msg_list_item_t *item, const char *time)
{
    COS_CHECK_PTR_RETURN(item);
    lv_label_set_text(item->time_label, time ? time : "");
}

void cos_msg_list_item_icon_set_src(cos_msg_list_item_t *item, const char *src)
{
    COS_CHECK_PTR_RETURN(item && src);
    lv_image_set_src(item->icon, src);
    cos_img_set_size(item->icon, _ICON_WIDTH, _ICON_HEIGHT);
}

void cos_msg_list_clear_all(cos_msg_list_t *msg_list)
{
    COS_CHECK_PTR_RETURN(msg_list && msg_list->list);

    // Get all child objects
    uint32_t child_count = lv_obj_get_child_count(msg_list->list);
    lv_obj_t **children = cos_malloc(sizeof(lv_obj_t *) * child_count);

    // Save child object pointers (avoid traversal issues during dynamic deletion)
    for (uint32_t i = 0; i < child_count; i++)
    {
        children[i] = lv_obj_get_child(msg_list->list, i);
    }

    // Delete child objects
    for (uint32_t i = 0; i < child_count; i++)
    {
        lv_obj_t *child = children[i];
        if (child == msg_list->clear_all_btn)
        {
            continue;
        }
        cos_msg_list_item_t *item = (cos_msg_list_item_t *)lv_obj_get_user_data(child);
        if (item)
        {
            cos_msg_list_item_delete(item);
        }
        else
        {
            lv_obj_delete_async(child);
        }
    }

    cos_free(children);
}

/************************** Clear Button Related Callbacks **************************/

/**
 * @brief Callback for message animation end
 */
static void _msg_list_item_anim_end_cb(lv_anim_t *a)
{
    cos_msg_list_t *list = (cos_msg_list_t *)lv_anim_get_user_data(a);
    COS_CHECK_PTR_RETURN(list);
    // Decrease animation count
    if (list->animating_count > 0)
    {
        list->animating_count--;
    }

    // Check if all animations are completed
    if (list->animating_count == 0)
    {
        // Directly clear all contents
        cos_msg_list_clear_all(list);
        cos_swipe_panel_pull_back(list->swipe_panel);
        _del_item_cb(list);
    }
}

/**
 * @brief Trigger message deletion animation
 * @param list Target message list
 */
static void _trigger_msg_anims(cos_msg_list_t *list)
{
    COS_CHECK_PTR_RETURN(list);
    static uint8_t anim_index = 0; // Static variable to track current animation index
    lv_obj_t *parent = list->list;
    uint8_t child_count = lv_obj_get_child_count(parent);

    // Process only one message item animation at a time
    for (uint8_t i = anim_index; i < child_count && list->animating_count < 2; i++)
    {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        cos_msg_list_item_t *item = (cos_msg_list_item_t *)lv_obj_get_user_data(child);

        // Skip the clear button itself
        if (child == list->clear_all_btn)
        {
            anim_index++;
            continue;
        }

        if (item)
        {
            // Create animation
            cos_lite_anim_fade_layered_start(item->container, LV_OPA_COVER, LV_OPA_TRANSP, 300, 0, NULL, NULL);
            cos_lite_anim_move_hor_start(item->container,
                                         lv_obj_get_x(item->container),
                                         COS_DISPLAY_WIDTH,
                                         300,
                                         0,
                                         _msg_list_item_anim_end_cb,
                                         list);

            list->animating_count++;
            anim_index++;
        }
    }

    anim_index = 0;
}

/**
 * @brief Callback for clear all messages button
 */
static void _msg_list_clear_all_btn_cb(lv_event_t *e)
{
    cos_msg_list_t *list = (cos_msg_list_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(list);
    // Hide no-message label
    if (list->no_msg_label)
    {
        lv_obj_add_flag(list->no_msg_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Reset animation count
    list->animating_count = 0;

    // Trigger animations
    _trigger_msg_anims(list);
}

/************************** List Related Functions **************************/

static void _msg_list_deleted_cb(lv_event_t *e)
{
    cos_msg_list_t *list = (cos_msg_list_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(list);

    // Clear all message items
    cos_msg_list_clear_all(list);

    // Delete no message label
    if (list->no_msg_label)
    {
        lv_obj_delete_async(list->no_msg_label);
        list->no_msg_label = NULL;
    }

    // Delete swipe panel (will automatically delete all internal objects)
    if (list->swipe_panel)
    {
        cos_swipe_panel_delete(list->swipe_panel);
        list->swipe_panel = NULL;
    }

    // Free list struct
    cos_free(list);
}

static void _slide_widget_reached_threshold_cb(lv_event_t *e)
{
    if (lv_obj_get_y(message_list_instance->swipe_panel->swipe_obj) >= 0)
        cos_crown_encoder_set_target_obj(message_list_instance->list);
}

static void _msg_list_opened_cb(lv_event_t *e)
{
    COS_LOG_I("Message list opened");
    cos_chrome_manager_notify_overlay_opened(&_msg_list_overlay);
}

static void _msg_list_closed_cb(lv_event_t *e)
{
    COS_LOG_I("Message list closed");
    cos_chrome_manager_notify_overlay_closed(&_msg_list_overlay);
}

static void _msg_list_overlay_pull_back(void)
{
    cos_msg_list_close_detail();
    cos_msg_list_t *msg_list = cos_msg_list_get_instance();
    if (msg_list && msg_list->swipe_panel)
    {
        cos_swipe_panel_pull_back(msg_list->swipe_panel);
    }
}

static void _msg_list_overlay_hide(void)
{
    cos_msg_list_close_detail();
    cos_msg_list_hide();
}

static void _msg_list_overlay_on_focus(void)
{
    COS_LOG_D("Msg list focused");
}

static bool _msg_list_overlay_is_open(void)
{
    cos_msg_list_t *msg_list = cos_msg_list_get_instance();
    if (msg_list && msg_list->swipe_panel && msg_list->swipe_panel->sw)
    {
        return cos_slide_widget_get_state(msg_list->swipe_panel->sw) == COS_SLIDE_WIDGET_STATE_OPEN;
    }
    return false;
}

static lv_obj_t *_msg_list_overlay_get_scrollable(void)
{
    cos_msg_list_t *msg_list = cos_msg_list_get_instance();
    if (msg_list && msg_list->list)
    {
        return msg_list->list;
    }
    return NULL;
}

static lv_obj_t *_msg_list_overlay_get_foreground_obj(void)
{
    cos_msg_list_t *msg_list = cos_msg_list_get_instance();
    if (msg_list && msg_list->swipe_panel && msg_list->swipe_panel->sw)
    {
        return cos_slide_widget_get_touch_obj(msg_list->swipe_panel->sw);
    }
    return NULL;
}

cos_msg_list_t *cos_msg_list_create(lv_obj_t *parent)
{
    COS_CHECK_PTR_RETURN_VAL(parent, NULL);
    cos_msg_list_t *msg_list = cos_malloc_zeroed(sizeof(cos_msg_list_t));
    COS_CHECK_PTR_RETURN_VAL_FREE(msg_list, NULL, msg_list);
    _detail_data = NULL;

    msg_list->swipe_panel = cos_swipe_panel_create(parent);
    COS_CHECK_PTR_RETURN_VAL_FREE(msg_list->swipe_panel, NULL, msg_list);
    cos_swipe_panel_set_dir(msg_list->swipe_panel, COS_SWIPE_DIR_DOWN);
    cos_crown_encoder_register_slide_widget(msg_list->swipe_panel->sw);
    cos_slide_widget_add_event_cb_reached_threshold(msg_list->swipe_panel->sw,
                                                    _slide_widget_reached_threshold_cb,
                                                    NULL);
    cos_slide_widget_add_event_cb_opened(msg_list->swipe_panel->sw, _msg_list_opened_cb, NULL);
    cos_slide_widget_add_event_cb_closed(msg_list->swipe_panel->sw, _msg_list_closed_cb, NULL);

    msg_list->list = lv_list_create(msg_list->swipe_panel->swipe_obj);
    COS_CHECK_PTR_RETURN_VAL_FREE(msg_list->list, NULL, msg_list);
    lv_obj_set_size(msg_list->list, LV_PCT(100), LV_PCT(88));
    lv_obj_center(msg_list->list);
    lv_obj_set_style_bg_opa(msg_list->list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(msg_list->list, 0, 0);
    lv_obj_set_style_pad_all(msg_list->list, 30, 0);
    lv_obj_align(msg_list->list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_user_data(msg_list->list, msg_list);
    lv_obj_set_scroll_dir(msg_list->list, LV_DIR_VER);
    lv_obj_add_event_cb(msg_list->list, _msg_list_deleted_cb, LV_EVENT_DELETE, msg_list);
    // Create clear all button
    msg_list->clear_all_btn = lv_button_create(msg_list->list);
    lv_obj_set_size(msg_list->clear_all_btn, lv_pct(100), 80);
    lv_obj_remove_flag(msg_list->clear_all_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(msg_list->clear_all_btn, COS_COLOR_DARK_GREY_2, 0);
    lv_obj_set_style_border_width(msg_list->clear_all_btn, 0, 0);
    lv_obj_set_style_margin_bottom(msg_list->clear_all_btn, _MSG_LIST_ITEM_MARGIN_BOTTOM, 0);
    lv_obj_set_style_pad_all(msg_list->clear_all_btn, 0, 0);
    lv_obj_set_style_align(msg_list->clear_all_btn, LV_ALIGN_CENTER, 0);
    lv_obj_set_style_radius(msg_list->clear_all_btn, 50, 0);
    lv_obj_set_style_shadow_width(msg_list->clear_all_btn, 0, 0);

    // Hide clear button initially
    lv_obj_add_flag(msg_list->clear_all_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *clear_all_label = lv_label_create(msg_list->clear_all_btn);
    cos_label_set_text_id(clear_all_label, STR_ID_MSG_LIST_CLEAR_ALL);

    lv_obj_center(clear_all_label);

    lv_obj_add_event_cb(msg_list->clear_all_btn, _msg_list_clear_all_btn_cb, LV_EVENT_CLICKED, msg_list);

    // Create no message label
    msg_list->no_msg_label = lv_label_create(msg_list->swipe_panel->swipe_obj);
    cos_label_set_text_id(msg_list->no_msg_label, STR_ID_MSG_LIST_NO_MSG);

    lv_obj_center(msg_list->no_msg_label);

    return msg_list;
}

void cos_msg_list_close_detail(void)
{
    if (!_detail_data)
        return;

    if (_detail_data->item_container)
    {
        lv_obj_set_style_opa(_detail_data->item_container, LV_OPA_COVER, 0);
    }

    if (_detail_data->panel)
    {
        cos_panel_delete(_detail_data->panel);
    }

    cos_free(_detail_data);
    _detail_data = NULL;
}

void cos_msg_list_hide(void)
{
    COS_CHECK_PTR_RETURN(message_list_instance);
    cos_msg_list_close_detail();
    cos_swipe_panel_hide(message_list_instance->swipe_panel);
    cos_crown_encoder_activate_current_overlay_scrollable();
}

void cos_msg_list_show(void)
{
    COS_CHECK_PTR_RETURN(message_list_instance);
    cos_swipe_panel_show(message_list_instance->swipe_panel);
    cos_crown_encoder_activate_current_overlay_scrollable();
}

cos_msg_list_t *cos_msg_list_get_instance(void)
{
    return message_list_instance;
}

void cos_msg_list_init(void)
{
    message_list_instance = cos_msg_list_create(cos_overlay_get_overlay_layer());
}

const cos_chrome_overlay_t *cos_msg_list_get_overlay_descriptor(void)
{
    return &_msg_list_overlay;
}
