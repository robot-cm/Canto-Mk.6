/**
 * @file eos_service_pm.c
 * @brief Power Manager
 */

#include "eos_service_pm.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#define EOS_LOG_TAG "PowerManager"
#include "eos_log.h"
#include "eos_service_config.h"
#include "eos_event.h"
#include "eos_dev_power.h"
#include "eos_config.h"
#include "eos_touch.h"
#include "eos_dispatcher.h"
#include "eos_dfw.h"
/* Macros and Definitions -------------------------------------*/
#define DEBUG_DISABLE_TIMER 1 /**< [Debug] Whether to disable the timer */
#define _DEFAULT_TIMEOUT_SEC 15
/* Variables --------------------------------------------------*/
static lv_timer_t *t; /**< Sleep timer, triggers sleep mode after timeout */
static eos_pm_state_t pm_state = EOS_PM_DISPLAY_ON;
static bool aod_mode = true;
static lv_obj_t *_ds_mask = NULL;  /**< Deep-sleep black full-screen mask (simulator) */
static lv_timer_t *_ds_timer = NULL; /**< Auto-wake timer for deep sleep */
/* Function Implementations -----------------------------------*/
void eos_pm_reset_timer(void);

/* ---- Deep-sleep black-screen mask (simulator only) ---- */
static void _ds_mask_create(void)
{
    if (_ds_mask)
        return;
    lv_obj_t *top = lv_layer_top();
    _ds_mask = lv_obj_create(top);
    lv_obj_set_size(_ds_mask, EOS_DISPLAY_WIDTH, EOS_DISPLAY_HEIGHT);
    lv_obj_set_pos(_ds_mask, 0, 0);
    lv_obj_set_style_bg_color(_ds_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_ds_mask, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_ds_mask, 0, 0);
    lv_obj_set_style_border_width(_ds_mask, 0, 0);
    lv_obj_move_foreground(_ds_mask);
    EOS_LOG_I("Deep sleep: screen masked (black)");
}

static void _ds_mask_destroy(void)
{
    if (_ds_mask)
    {
        lv_obj_delete(_ds_mask);
        _ds_mask = NULL;
    }
}

static void _ds_wake_timer_cb(lv_timer_t *tm)
{
    lv_timer_delete(tm);
    if (_ds_timer == tm)
        _ds_timer = NULL;
    eos_pm_wake_up();
}

#if EOS_COMPILE_MODE == DEBUG
static char *_print_state(eos_pm_state_t state)
{
    switch (state)
    {
        case EOS_PM_DISPLAY_ON:
            return "EOS_PM_DISPLAY_ON";
        case EOS_PM_DISPLAY_AOD:
            return "EOS_PM_DISPLAY_AOD";
        case EOS_PM_SLEEP:
            return "EOS_PM_SLEEP";
        default:
            return "";
    }
}
#endif /* EOS_COMPILE_MODE */
static void _pm_set_state(eos_pm_state_t state)
{
#if EOS_COMPILE_MODE == DEBUG
    EOS_LOG_I("State: %s -> %s", _print_state(pm_state), _print_state(state));
#else
    EOS_LOG_I("State: %d -> %d", pm_state, state);
#endif /* EOS_COMPILE_MODE */
    pm_state = state;

    eos_dev_power_t *dev = eos_dev_power_get_instance();
    if (dev->ops == NULL || dev->ops->set_power == NULL)
    {
        EOS_LOG_E("Power device OPS not available");
        return;
    }

    switch (state)
    {
        case EOS_PM_DISPLAY_ON:
            _ds_mask_destroy();
            eos_event_post(EOS_EVENT_SYSTEM_DISPLAY_ON, NULL, NULL);
            dev->ops->set_power(DEV_POWER_STATE_ON);
            if (t)
                lv_timer_resume(t);
            break;
        case EOS_PM_SLEEP:
            lv_timer_pause(t);
            eos_event_post(EOS_EVENT_SYSTEM_SLEEP, NULL, NULL);
#if EOS_DFW_ENABLE
            eos_dfw_sync();
#endif /* EOS_DFW_ENABLE */
            dev->ops->set_power(DEV_POWER_STATE_SLEEP);
            break;
        case EOS_PM_DISPLAY_AOD:
            lv_timer_pause(t);
            eos_event_post(EOS_EVENT_SYSTEM_DISPLAY_AOD, NULL, NULL);
            dev->ops->set_power(DEV_POWER_STATE_AOD);
            break;
        case EOS_PM_DEEP_SLEEP:
            if (t)
                lv_timer_pause(t);
            eos_event_post(EOS_EVENT_SYSTEM_SLEEP, NULL, NULL);
            dev->ops->set_power(DEV_POWER_STATE_SLEEP);
            break;
        default:
            EOS_LOG_E("Unknown state");
            return;
    }
    eos_pm_reset_timer();
}

void eos_pm_set_aod_mode(bool enable)
{
    aod_mode = enable;
    eos_config_set_bool(EOS_CONFIG_KEY_AOD_MODE_BOOL, enable);
}

eos_pm_state_t eos_pm_get_state(void)
{
    return pm_state;
}

void eos_pm_request_sleep(void)
{
    if (aod_mode)
    {
        _pm_set_state(EOS_PM_DISPLAY_AOD);
    }
    else
    {
        _pm_set_state(EOS_PM_SLEEP);
    }
}

static void _sleep_timer_cb(lv_timer_t *t)
{
    eos_pm_request_sleep();
    if (t)
    {
        lv_timer_pause(t);
        lv_timer_reset(t);
    }
}

void eos_pm_set_sleep_timeout(uint32_t sec)
{
    if (t)
    {
        lv_timer_set_period(t, sec * 1000);
        lv_timer_reset(t);
    }
}

void eos_pm_wake_up(void)
{
    _pm_set_state(EOS_PM_DISPLAY_ON);
}

void eos_pm_deep_sleep_request(uint32_t duration_sec)
{
    EOS_LOG_I("Deep sleep requested (duration=%u s)", (unsigned)duration_sec);
    if (_ds_timer)
    {
        lv_timer_delete(_ds_timer);
        _ds_timer = NULL;
    }
    _ds_mask_create();
    _pm_set_state(EOS_PM_DEEP_SLEEP);
    if (duration_sec > 0)
    {
        _ds_timer = lv_timer_create(_ds_wake_timer_cb, duration_sec * 1000, NULL);
        lv_timer_set_repeat_count(_ds_timer, 1);
        EOS_LOG_I("Deep sleep: auto-wake in %u s", (unsigned)duration_sec);
    }
}

void eos_pm_reset_timer(void)
{
    EOS_LOG_I("Sleep timer reset");
    if (t)
        lv_timer_reset(t);
}

static void _indev_pressed_cb(lv_event_t *e)
{
    EOS_LOG_I("Timer paused");
    /* Any touch wakes the device from deep sleep. */
    if (pm_state == EOS_PM_DEEP_SLEEP)
    {
        eos_pm_wake_up();
        return;
    }
    if (t && pm_state == EOS_PM_DISPLAY_ON)
    {
        lv_timer_pause(t);
        lv_timer_reset(t);
    }
}

static void _indev_released_cb(lv_event_t *e)
{
    EOS_LOG_I("Timer resumed");
    if (t && pm_state == EOS_PM_DISPLAY_ON)
    {
        lv_timer_resume(t);
        lv_timer_reset(t);
    }
}

void eos_service_pm_init(void)
{
    EOS_LOG_I("Power manager init");
    aod_mode = eos_config_get_bool(EOS_CONFIG_KEY_AOD_MODE_BOOL, false);
    uint32_t timer_period_sec = eos_config_get_number(EOS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, _DEFAULT_TIMEOUT_SEC);
#if DEBUG_DISABLE_TIMER
    t = NULL;
#else
    t = lv_timer_create(_sleep_timer_cb, timer_period_sec * 1000, NULL);
    lv_timer_set_repeat_count(t, -1); // Must be infinite, otherwise timer will be deleted
#endif /* DEBUG_DISABLE_TIMER */
    /* Touch indev may be absent on boards without a registered POINTER
     * input device (e.g. ESP32 phase B touch stub). Registering event cbs on
     * NULL triggers LVGL LV_ASSERT_NULL -> while(1) hang + task watchdog. */
    lv_indev_t *touch = eos_touch_get_indev();
    if (touch)
    {
        lv_indev_add_event_cb(touch, _indev_pressed_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(touch, _indev_released_cb, LV_EVENT_RELEASED, NULL);
    }
    else
    {
        EOS_LOG_W("No touch indev registered; PM touch wake/pause disabled");
    }
}
