/**
 * @file cos_service_display.c
 * @brief Display service
 */

#include "cos_service_display.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#include "cos_config.h"
#include "cos_service_config.h"
#include "cos_dev_display.h"

/* Macros and Definitions -------------------------------------*/

/* LVGL 刷新周期默认值(须与生效中的 lv_conf.h LV_DEF_REFR_PERIOD 一致,
 * 即 port/esp32s3/main/lv_conf.h 的 20ms) */
#define _REFRESH_PERIOD_DEFAULT_MS 20

/* Variables --------------------------------------------------*/
static uint8_t _saved_brightness = 50;
static uint8_t _current_brightness = 50;
static bool _in_temporary_mode = false;

/* Function Implementations -----------------------------------*/

static void _set_brightness_direct(uint8_t brightness)
{
    cos_dev_display_t *dev = cos_dev_display_get_instance();
    if (dev->ops && dev->ops->set_brightness)
    {
        dev->ops->set_brightness(brightness);
        _current_brightness = brightness;
    }
}

static void _brightness_anim_cb(void *var, int32_t v)
{
    LV_UNUSED(var);
    _set_brightness_direct((uint8_t)v);
}

void cos_display_set_brightness(uint8_t brightness, cos_display_duration_t duration_ms, bool is_temporary)
{
    if (is_temporary && !_in_temporary_mode)
    {
        _saved_brightness = _current_brightness;
        _in_temporary_mode = true;
    }
    else if (!is_temporary && _in_temporary_mode)
    {
        _in_temporary_mode = false;
        _saved_brightness = brightness;
    }
    else if (!is_temporary)
    {
        _saved_brightness = brightness;
    }

    if (duration_ms > 0 && _current_brightness != brightness)
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, NULL);
        lv_anim_set_values(&a, _current_brightness, brightness);
        lv_anim_set_time(&a, duration_ms);
        lv_anim_set_exec_cb(&a, _brightness_anim_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
    else
    {
        _set_brightness_direct(brightness);
    }
}

uint8_t cos_display_get_brightness(void)
{
    return _current_brightness;
}

void cos_display_refresh_period_set(uint32_t period_ms)
{
    /* 0 = 复位到系统默认刷新周期(须与生效中的 lv_conf.h LV_DEF_REFR_PERIOD 一致) */
    if (period_ms == 0)
    {
        period_ms = _REFRESH_PERIOD_DEFAULT_MS;
    }
    lv_display_t *disp = lv_display_get_default();
    lv_timer_t *refr = disp ? lv_display_get_refr_timer(disp) : NULL;
    if (!refr)
    {
        return;
    }
    lv_timer_set_period(refr, period_ms);
}

void cos_display_power_on(void)
{
    cos_dev_display_t *dev = cos_dev_display_get_instance();
    if (dev->ops && dev->ops->power_on)
    {
        dev->ops->power_on();
    }
}

void cos_display_power_off(void)
{
    cos_dev_display_t *dev = cos_dev_display_get_instance();
    if (dev->ops && dev->ops->power_off)
    {
        dev->ops->power_off();
    }
}

void cos_display_restore(cos_display_duration_t duration_ms)
{
    uint8_t brightness_to_restore = _saved_brightness;
    _in_temporary_mode = false;
    cos_display_set_brightness(brightness_to_restore, duration_ms, false);
}

cos_result_t cos_display_bltest(bool high)
{
    cos_dev_display_t *dev = cos_dev_display_get_instance();
    if (dev->ops && dev->ops->bltest)
    {
        dev->ops->bltest(high);
        return COS_OK;
    }
    return COS_ERR_DEV_OPS_NOT_SUPPORTED;
}
