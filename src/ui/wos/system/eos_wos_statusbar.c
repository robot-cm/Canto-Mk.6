/**
 * @file eos_wos_statusbar.c
 * @brief WOS top info bar — circular-screen top strip, 15s rotating info.
 *
 * Lives on the statusbar overlay layer, above the app page and notifications.
 * A single small centered label rotates every 15 seconds between:
 *   1. "HH:MM 87%"      — time + battery percent
 *   2. "8/25 Tue 87%"   — date + weekday abbr + battery percent
 * The label refreshes every second from the SAME time API as the watchface
 * (eos_time_get), so the strip never shows a stale time.
 * Battery percent is always shown ("--%" when no battery device reports).
 * Transparent background: never covers watchface / app content (the layout
 * already reserves WOS_STATUSBAR_H at the top).
 */
#include "eos_wos_statusbar.h"

#include "eos_wos_theme.h"
#include "eos_overlay_layer.h"
#include "eos_log.h"
#include "eos_service_time.h"
#include "eos_service_battery.h"
#include <stdio.h>
#include <string.h>

#define EOS_LOG_TAG "WosStatus"

/* 时间每秒刷新(与表盘同一 eos_time_get 源,保持同步) */
#define WOS_STATUS_REFRESH_MS (1000)
/* 每 15s 在 "时间" 与 "日期" 两种信息之间轮播 */
#define WOS_STATUS_ROTATE_MS (15 * 1000)

static lv_obj_t *s_label = NULL;
static lv_timer_t *s_timer = NULL;
static uint8_t s_mode = 0; /* 0: 时间+电量  1: 日期+星期+电量 */
static uint32_t s_elapsed_ms = 0;

/* day_of_week: 1=Mon .. 7=Sun (PCF8563 惯例, eos_service_time 保证) */
static const char *const _wday_abbr[8] = {
    "", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
};

static void _render(void)
{
    if (!s_label || !lv_obj_is_valid(s_label))
        return;

    eos_datetime_t dt = eos_time_get();
    int8_t batt = eos_battery_get_percent(); /* -1 = no battery device */
    char buf[24];

    /* 电量必须始终显示;无电池设备时显示 "--%" */
    if (s_mode == 0)
    {
        if (batt >= 0)
            snprintf(buf, sizeof(buf), "%02d:%02d %d%%", dt.hour, dt.min, (int)batt);
        else
            snprintf(buf, sizeof(buf), "%02d:%02d --%%", dt.hour, dt.min);
    }
    else
    {
        const char *wday = (dt.day_of_week >= 1 && dt.day_of_week <= 7)
                               ? _wday_abbr[dt.day_of_week] : "";
        if (batt >= 0)
            snprintf(buf, sizeof(buf), "%d/%d %s %d%%", dt.month, dt.day, wday, (int)batt);
        else
            snprintf(buf, sizeof(buf), "%d/%d %s --%%", dt.month, dt.day, wday);
    }

    lv_label_set_text(s_label, buf);
}

/* 每秒刷新时间(与表盘同步);累计到 15s 轮换一次信息模式 */
static void _tick_cb(lv_timer_t *tm)
{
    (void)tm;
    s_elapsed_ms += WOS_STATUS_REFRESH_MS;
    if (s_elapsed_ms >= WOS_STATUS_ROTATE_MS)
    {
        s_elapsed_ms = 0;
        s_mode = (s_mode + 1) & 1;
    }
    _render();
}

void wos_statusbar_init(void)
{
    if (s_label)
        return;

    lv_obj_t *layer = eos_overlay_get_statusbar_layer();
    if (!layer)
        return;

    s_label = lv_label_create(layer);
    lv_obj_set_width(s_label, lv_pct(100));
    /* 更小字号,给时间+电量留出空间 */
    wos_style_text_primary(s_label, WOS_FONT_XS);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(s_label, LV_OPA_TRANSP, 0);
    /* 顶部居中,稍微下移避开圆屏最顶部被裁切的几行像素 */
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, 6);

    _render();

    s_timer = lv_timer_create(_tick_cb, WOS_STATUS_REFRESH_MS, NULL);
    lv_timer_set_repeat_count(s_timer, -1);

    EOS_LOG_I("wos: top info bar initialized (1s refresh, 15s rotate: time+batt <-> date+wday+batt)");
}
