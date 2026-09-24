/**
 * @file cos_service_pm.c
 * @brief Power Manager
 */

#include "cos_service_pm.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#define COS_LOG_TAG "PowerManager"
#include "cos_log.h"
#include "cos_service_config.h"
#include "cos_event.h"
#include "cos_dev_power.h"
#include "cos_config.h"
#include "cos_touch.h"
#include "cos_dispatcher.h"
#include "cos_dfw.h"
#include "cos_service_power_save.h" /* cos_power_save_is_active + 熄屏策略宏 */
/* Macros and Definitions -------------------------------------*/
/* 睡眠定时器:超时后自动熄屏(进 AOD/SLEEP)。
 * 曾为调试置 1 禁用以保持屏幕常亮,现在已不需要——正式启用。 */
#define DEBUG_DISABLE_TIMER 0 /**< [Debug] Whether to disable the timer */
/* 熄屏默认超时 10s(电池优化:无操作 10s 后关背光并进入硬件 Light Sleep)。
 * 可被 COS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER 覆盖(Settings -> Screen timeout)。 */
#define _DEFAULT_TIMEOUT_SEC 10

/* ---- 触摸手势:手掌覆盖(长按静止)熄屏 / 双击亮屏 ----
 * CHSC6X 触摸芯片只有单点坐标、无手势寄存器,故:
 *   - "手掌覆盖"近似为"按住 ≥ PM_PALM_HOLD_MS 且位移 ≤ PM_PALM_SLOP_PX"
 *     (手掌整个贴上时触摸点持续存在、几乎不动,与手指长按数据无法区分);
 *   - "双击"为纯软件检测:非亮屏状态下 400ms 内两次 PRESSED。
 * 注意:与 watchface 的 400ms 长按(打开表盘列表)共存——手掌覆盖 0.4s 会
 * 先弹出表盘列表,2.5s 后才熄屏;若不想看到列表弹出,可改用下滑手势熄屏。 */
#define PM_PALM_HOLD_MS  (2500)
#define PM_PALM_SLOP_PX  (15)
#define PM_DOUBLE_TAP_MS (400)

/* Variables --------------------------------------------------*/
static lv_timer_t *t; /**< Sleep timer, triggers sleep mode after timeout */
static cos_pm_state_t pm_state = COS_PM_DISPLAY_ON;
static bool aod_mode = true;
static lv_obj_t *_ds_mask = NULL;  /**< Deep-sleep black full-screen mask (simulator) */
static lv_timer_t *_ds_timer = NULL; /**< Auto-wake timer for deep sleep */
/* 手势跟踪状态 */
static uint32_t s_press_tick_ms = 0; /**< 本次按压开始时刻(lv_tick_get) */
static lv_point_t s_press_pt = {0, 0}; /**< 本次按压起点坐标 */
static bool s_pressing = false;       /**< 手指当前是否按着 */
static bool s_tap_armed = false;      /**< 已记下第一次点击,等待第二次(双击) */
static uint32_t s_tap_first_ms = 0;   /**< 第一次点击时刻 */
/* Function Implementations -----------------------------------*/
void cos_pm_reset_timer(void);

/* ---- 黑屏 mask:熄屏(SLEEP)与关机/待机深睡(DEEP_SLEEP)共用 ----
 * 真机背光已灭(+面板 0x28),mask 只是模拟器的熄屏视觉与真机防残影兜底;
 * 两种场景共用同一对象,故日志按 ctx 区分,避免普通熄屏被误读成进深睡。 */
static void _ds_mask_create(const char *ctx)
{
    if (_ds_mask)
        return;
    lv_obj_t *top = lv_layer_top();
    _ds_mask = lv_obj_create(top);
    lv_obj_set_size(_ds_mask, COS_DISPLAY_WIDTH, COS_DISPLAY_HEIGHT);
    lv_obj_set_pos(_ds_mask, 0, 0);
    lv_obj_set_style_bg_color(_ds_mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_ds_mask, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_ds_mask, 0, 0);
    lv_obj_set_style_border_width(_ds_mask, 0, 0);
    lv_obj_move_foreground(_ds_mask);
    COS_LOG_I("%s: screen masked (black)", ctx ? ctx : "Black mask");
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
    cos_pm_wake_up();
}

#if COS_COMPILE_MODE == DEBUG
static char *_print_state(cos_pm_state_t state)
{
    switch (state)
    {
        case COS_PM_DISPLAY_ON:
            return "COS_PM_DISPLAY_ON";
        case COS_PM_DISPLAY_AOD:
            return "COS_PM_DISPLAY_AOD";
        case COS_PM_SLEEP:
            return "COS_PM_SLEEP";
        default:
            return "";
    }
}
#endif /* COS_COMPILE_MODE */
static void _pm_set_state(cos_pm_state_t state)
{
#if COS_COMPILE_MODE == DEBUG
    COS_LOG_I("State: %s -> %s", _print_state(pm_state), _print_state(state));
#else
    COS_LOG_I("State: %d -> %d", pm_state, state);
#endif /* COS_COMPILE_MODE */
    pm_state = state;

    cos_dev_power_t *dev = cos_dev_power_get_instance();
    if (dev->ops == NULL || dev->ops->set_power == NULL)
    {
        COS_LOG_E("Power device OPS not available");
        return;
    }

    switch (state)
    {
        case COS_PM_DISPLAY_ON:
            _ds_mask_destroy();
            cos_event_post(COS_EVENT_SYSTEM_DISPLAY_ON, NULL, NULL);
            dev->ops->set_power(DEV_POWER_STATE_ON);
            /* 唤醒强制整屏重绘:熄屏期间黑 mask 帧残留在面板 GRAM,若唤醒后
             * 恰好无对象 invalidate,LVGL 不会自动重绘 → DISPON+背光后看到
             * 的是黑帧(表现为"双击亮了但画面全黑/像没唤醒")。显式 invalidate
             * 全屏并立即同步刷一帧,保证背光亮起时 GRAM 已是当前 UI。 */
            lv_obj_invalidate(lv_screen_active());
            lv_obj_invalidate(lv_layer_top());
            lv_refr_now(NULL);
            COS_LOG_I("Wake: full redraw forced");
            if (t)
                lv_timer_resume(t);
            break;
        case COS_PM_SLEEP:
            if (t)
                lv_timer_pause(t);
            cos_event_post(COS_EVENT_SYSTEM_SLEEP, NULL, NULL);
#if COS_DFW_ENABLE
            cos_dfw_sync();
#endif /* COS_DFW_ENABLE */
            dev->ops->set_power(DEV_POWER_STATE_SLEEP);
            /* 模拟器无背光概念,黑屏 mask 提供熄屏视觉;真机背光已灭,
             * 静态黑屏渲染无额外开销。 */
            _ds_mask_create("Light sleep (screen off)");
            break;
        case COS_PM_DISPLAY_AOD:
            if (t)
                lv_timer_pause(t);
            cos_event_post(COS_EVENT_SYSTEM_DISPLAY_AOD, NULL, NULL);
            dev->ops->set_power(DEV_POWER_STATE_AOD);
            break;
        case COS_PM_DEEP_SLEEP:
            if (t)
                lv_timer_pause(t);
            cos_event_post(COS_EVENT_SYSTEM_SLEEP, NULL, NULL);
            dev->ops->set_power(DEV_POWER_STATE_SLEEP);
            break;
        default:
            COS_LOG_E("Unknown state");
            return;
    }
    cos_pm_reset_timer();
}

void cos_pm_set_aod_mode(bool enable)
{
    aod_mode = enable;
    cos_config_set_bool(COS_CONFIG_KEY_AOD_MODE_BOOL, enable);
}

cos_pm_state_t cos_pm_get_state(void)
{
    return pm_state;
}

void cos_pm_request_sleep(void)
{
    if (aod_mode)
    {
        _pm_set_state(COS_PM_DISPLAY_AOD);
    }
    else
    {
        _pm_set_state(COS_PM_SLEEP);
    }
}

static void _sleep_timer_cb(lv_timer_t *t)
{
    cos_pm_request_sleep();
    if (t)
    {
        lv_timer_pause(t);
        lv_timer_reset(t);
    }
}

void cos_pm_set_sleep_timeout(uint32_t sec)
{
    if (t)
    {
        lv_timer_set_period(t, sec * 1000);
        lv_timer_reset(t);
    }
}

void cos_pm_wake_up(void)
{
    _pm_set_state(COS_PM_DISPLAY_ON);
}

void cos_pm_deep_sleep_request(uint32_t duration_sec)
{
    COS_LOG_I("Deep sleep requested (duration=%u s)", (unsigned)duration_sec);
    if (_ds_timer)
    {
        lv_timer_delete(_ds_timer);
        _ds_timer = NULL;
    }
    _ds_mask_create("Deep sleep");
    _pm_set_state(COS_PM_DEEP_SLEEP);
    if (duration_sec > 0)
    {
        _ds_timer = lv_timer_create(_ds_wake_timer_cb, duration_sec * 1000, NULL);
        lv_timer_set_repeat_count(_ds_timer, 1);
        COS_LOG_I("Deep sleep: auto-wake in %u s", (unsigned)duration_sec);
    }
}

/* 进入深睡关机公共主体(不返回)。调用前必须经 set_poweroff_params 明确
 * 本次关机模式,否则可能带上次关机(RTC_DATA_ATTR 跨深睡保留)的模式残留。 */
static void _pm_power_off_enter(void)
{
    if (_ds_timer)
    {
        lv_timer_delete(_ds_timer);
        _ds_timer = NULL;
    }
    _ds_mask_create("Deep sleep");
    _pm_set_state(COS_PM_DEEP_SLEEP);
    /* 真机(ESP32-S3): 板级 set_power(DEV_POWER_STATE_OFF) 进入硬件深睡
     * (深睡 + RTC 定时器唤醒轮询 CHSC6X 触摸,长按开机),
     * esp_deep_sleep_start() 不返回,芯片重新启动后从 app_main 继续。
     * 模拟器: 无电源硬件,黑屏 mask 已由 _ds_mask_create() 提供。 */
    cos_dev_power_t *dev = cos_dev_power_get_instance();
    if (dev->ops && dev->ops->set_power)
        dev->ops->set_power(DEV_POWER_STATE_OFF);
}

/* 触摸模式关机(长按开机)。必须显式把板级模式重置为触摸:
 * 定时关机若中途被强制复位(RTC_DATA_ATTR 跨深睡保留 timed=1),
 * 残留会让本次"触摸关机"错误地走定时路径 —— 睡满后自动开机,
 * 用户看到"关机后没有真正待机,自己重启"。 */
void cos_pm_power_off(void)
{
    COS_LOG_I("Power off requested (touch mode, hold to boot)");
    cos_dev_power_t *dev = cos_dev_power_get_instance();
    if (dev->ops && dev->ops->set_poweroff_params)
        dev->ops->set_poweroff_params(false, 0);
    _pm_power_off_enter();
}

/* 定时关机:先通知板级"定时模式"(触摸无效,到点自动开机),再走关机流程。
 * 板级 set_poweroff_params 判空,模拟器/旧实现不受影响。 */
void cos_pm_power_off_timed(uint32_t wake_after_s)
{
    COS_LOG_I("Power off requested (timed, wake after %u s)", (unsigned)wake_after_s);
    cos_dev_power_t *dev = cos_dev_power_get_instance();
    if (dev->ops && dev->ops->set_poweroff_params)
        dev->ops->set_poweroff_params(true, wake_after_s);
    _pm_power_off_enter();
}

void cos_pm_reset_timer(void)
{
    COS_LOG_I("Sleep timer reset");
    if (t)
        lv_timer_reset(t);
}

/* 手指按下:记录按压起点;非亮屏状态下检测双击亮屏 */
static void _indev_pressed_cb(lv_event_t *e)
{
    COS_LOG_I("Timer paused");

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev)
        lv_indev_get_point(indev, &s_press_pt);
    s_press_tick_ms = lv_tick_get();
    s_pressing = true;

    /* Any touch wakes the device from deep sleep. */
    if (pm_state == COS_PM_DEEP_SLEEP)
    {
        cos_pm_wake_up();
        return;
    }

    /* AOD/SLEEP 状态:双击任意处亮屏(两次 PRESSED 间隔 < 400ms)。 */
    if (pm_state != COS_PM_DISPLAY_ON)
    {
        uint32_t now = lv_tick_get();
        if (s_tap_armed && (now - s_tap_first_ms) < PM_DOUBLE_TAP_MS)
        {
            s_tap_armed = false;
            COS_LOG_I("Double tap -> wake up");
            cos_pm_wake_up();
        }
        else
        {
            s_tap_first_ms = now;
            s_tap_armed = true;
        }
        return;
    }

    if (t)
    {
        lv_timer_pause(t);
        lv_timer_reset(t);
    }
}

/* 手指按住中:亮屏状态下,按住 ≥ 2.5s 且几乎不动 → 判定"手掌覆盖",熄屏 */
static void _indev_pressing_cb(lv_event_t *e)
{
    if (pm_state != COS_PM_DISPLAY_ON || !s_pressing)
        return;

    if ((lv_tick_get() - s_press_tick_ms) < PM_PALM_HOLD_MS)
        return;

    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev)
        return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (LV_ABS(p.x - s_press_pt.x) > PM_PALM_SLOP_PX ||
        LV_ABS(p.y - s_press_pt.y) > PM_PALM_SLOP_PX)
        return;

    s_pressing = false; /* 防重复触发 */
    COS_LOG_I("Palm-cover detected (stationary hold %u ms) -> sleep",
              (unsigned)PM_PALM_HOLD_MS);
    cos_pm_request_sleep();
}

static void _indev_released_cb(lv_event_t *e)
{
    COS_LOG_I("Timer resumed");
    s_pressing = false;
    if (t && pm_state == COS_PM_DISPLAY_ON)
    {
        lv_timer_resume(t);
        lv_timer_reset(t);
    }
}

void cos_service_pm_init(void)
{
    COS_LOG_I("Power manager init");
    aod_mode = cos_config_get_bool(COS_CONFIG_KEY_AOD_MODE_BOOL, false);
    uint32_t timer_period_sec = cos_config_get_number(COS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, _DEFAULT_TIMEOUT_SEC);
    /* 省电模式激活(重启后保持)时强制 5s 熄屏:cos_service_power_save_init()
     * 先于本服务运行(cos_core.c),它设置的运行时超时当时 t 尚未创建会落空,
     * 因此在此按省电状态对齐初始熄屏超时。 */
    if (cos_power_save_is_active())
    {
        timer_period_sec = COS_POWER_SAVE_SLEEP_TIMEOUT_SEC;
    }
#if DEBUG_DISABLE_TIMER
    t = NULL;
#else
    t = lv_timer_create(_sleep_timer_cb, timer_period_sec * 1000, NULL);
    lv_timer_set_repeat_count(t, -1); // Must be infinite, otherwise timer will be deleted
#endif /* DEBUG_DISABLE_TIMER */
    /* Touch indev may be absent on boards without a registered POINTER
     * input device (e.g. ESP32 phase B touch stub). Registering event cbs on
     * NULL triggers LVGL LV_ASSERT_NULL -> while(1) hang + task watchdog. */
    lv_indev_t *touch = cos_touch_get_indev();
    if (touch)
    {
        lv_indev_add_event_cb(touch, _indev_pressed_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(touch, _indev_pressing_cb, LV_EVENT_PRESSING, NULL);
        lv_indev_add_event_cb(touch, _indev_released_cb, LV_EVENT_RELEASED, NULL);
    }
    else
    {
        COS_LOG_W("No touch indev registered; PM touch wake/pause disabled");
    }
}
