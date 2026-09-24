/**
 * @file cos_ww_clock_hand.c
 * @brief Clock hand
 */

#include "cos_ww_clock_hand.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "cos_image.h"
#include "cos_service_time.h"
#include "cos_log.h"
/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

static void clock_hand_second_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *hand = lv_timer_get_user_data(timer);
    if (!hand || !lv_obj_is_valid(hand))
        return;
    cos_datetime_t now = cos_time_get();

    int32_t angle;

    // Second hand: 60s per revolution, millisecond precision
    angle = (now.sec * 1000 + now.ms) * 3600 / 60000;

    lv_image_set_rotation(hand, angle);
}

static void clock_hand_minute_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *hand = lv_timer_get_user_data(timer);
    if (!hand || !lv_obj_is_valid(hand))
        return;
    cos_datetime_t now = cos_time_get();

    int32_t angle;

    // Minute hand: 60min per revolution, second-level smoothing
    angle = (now.min * 60 + now.sec) * 3600 / 3600;

    lv_image_set_rotation(hand, angle);
}

static void clock_hand_hour_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *hand = lv_timer_get_user_data(timer);
    if (!hand || !lv_obj_is_valid(hand))
        return;
    cos_datetime_t now = cos_time_get();

    int32_t angle;

    // Hour hand: 12h per revolution, minute-level smoothing
    angle = ((now.hour % 12) * 60 + now.min) * 3600 / 720;

    lv_image_set_rotation(hand, angle);
}

void cos_clock_hand_place_pivot(lv_obj_t *hand, lv_coord_t target_x, lv_coord_t target_y)
{
    COS_CHECK_PTR_RETURN(hand);

    lv_point_t pivot_pioint;
    lv_image_get_pivot(hand, &pivot_pioint);

    lv_obj_set_pos(hand, target_x - pivot_pioint.x, target_y - pivot_pioint.y);
}

void cos_clock_hand_center(lv_obj_t *hand)
{
    COS_CHECK_PTR_RETURN(hand);

    lv_point_t pivot_pioint;
    lv_image_get_pivot(hand, &pivot_pioint);

    lv_obj_t *parent = lv_obj_get_parent(hand);
    COS_CHECK_PTR_RETURN(parent);

    lv_coord_t parent_w = lv_obj_get_width(parent);
    lv_coord_t parent_h = lv_obj_get_height(parent);

    lv_coord_t x = parent_w / 2 - pivot_pioint.x;
    lv_coord_t y = parent_h / 2 - pivot_pioint.y;

    lv_obj_set_pos(hand, x, y);
}

static void _clock_hand_deleted_cb(lv_event_t *e)
{
    lv_timer_t *t = lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(t);
    lv_timer_delete(t);
}

lv_obj_t *cos_clock_hand_create(lv_obj_t *parent,
                                const char *src,
                                cos_clock_hand_type_t type,
                                lv_coord_t hand_pivot_x,
                                lv_coord_t hand_pivot_y)
{
    COS_CHECK_PTR_RETURN_VAL(parent && src, NULL);

    lv_obj_t *hand = lv_image_create(parent);

    lv_image_set_src(hand, src);

    lv_image_set_pivot(hand, hand_pivot_x, hand_pivot_y);

    lv_timer_t *t = NULL;
    if (type == COS_CLOCK_HAND_SECOND)
    {
        t = lv_timer_create(clock_hand_second_timer_cb, LV_DEF_REFR_PERIOD, hand);
    }
    else if (type == COS_CLOCK_HAND_MINUTE)
    {
        t = lv_timer_create(clock_hand_minute_timer_cb, LV_DEF_REFR_PERIOD, hand);
    }
    else if (type == COS_CLOCK_HAND_HOUR)
    {
        t = lv_timer_create(clock_hand_hour_timer_cb, LV_DEF_REFR_PERIOD, hand);
    }
    else
    {
        COS_LOG_W("Unknown clock hand type");
    }
    if (t)
    {
        lv_obj_add_event_cb(hand, _clock_hand_deleted_cb, LV_EVENT_DELETE, t);
    }

    return hand;
}

static void _clock_hand_second_style_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *hand = lv_timer_get_user_data(timer);
    if (!hand || !lv_obj_is_valid(hand))
        return;
    cos_datetime_t now = cos_time_get();

    int32_t angle = (int32_t)((now.sec * 1000 + now.ms) * 3600 / 60000);

    lv_obj_set_style_transform_rotation(hand, angle, 0);
}

static void _clock_hand_minute_style_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *hand = lv_timer_get_user_data(timer);
    if (!hand || !lv_obj_is_valid(hand))
        return;
    cos_datetime_t now = cos_time_get();

    int32_t angle = (int32_t)((now.min * 60 + now.sec) * 3600 / 3600);

    lv_obj_set_style_transform_rotation(hand, angle, 0);
}

static void _clock_hand_hour_style_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *hand = lv_timer_get_user_data(timer);
    if (!hand || !lv_obj_is_valid(hand))
        return;
    cos_datetime_t now = cos_time_get();

    int32_t angle = (int32_t)(((now.hour % 12) * 60 + now.min) * 3600 / 720);

    lv_obj_set_style_transform_rotation(hand, angle, 0);
}

lv_timer_t *cos_clock_hand_attach(lv_obj_t *hand, cos_clock_hand_type_t type)
{
    COS_CHECK_PTR_RETURN_VAL(hand, NULL);

    lv_timer_t *t = NULL;
    if (type == COS_CLOCK_HAND_SECOND)
    {
        t = lv_timer_create(_clock_hand_second_style_timer_cb, LV_DEF_REFR_PERIOD, hand);
    }
    else if (type == COS_CLOCK_HAND_MINUTE)
    {
        t = lv_timer_create(_clock_hand_minute_style_timer_cb, LV_DEF_REFR_PERIOD, hand);
    }
    else if (type == COS_CLOCK_HAND_HOUR)
    {
        t = lv_timer_create(_clock_hand_hour_style_timer_cb, LV_DEF_REFR_PERIOD, hand);
    }
    else
    {
        COS_LOG_W("cos_clock_hand_attach: unknown hand type %d", (int)type);
        return NULL;
    }

    if (t)
    {
        lv_obj_add_event_cb(hand, _clock_hand_deleted_cb, LV_EVENT_DELETE, t);
    }

    return t;
}

void cos_clock_hand_center_style(lv_obj_t *hand, lv_coord_t pivot_x, lv_coord_t pivot_y)
{
    COS_CHECK_PTR_RETURN(hand);

    lv_obj_t *parent = lv_obj_get_parent(hand);
    COS_CHECK_PTR_RETURN(parent);

    lv_coord_t parent_w = lv_obj_get_width(parent);
    lv_coord_t parent_h = lv_obj_get_height(parent);

    lv_coord_t x = parent_w / 2 - pivot_x;
    lv_coord_t y = parent_h / 2 - pivot_y;

    lv_obj_set_pos(hand, x, y);
}
