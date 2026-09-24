/**
 * @file cos_wos_statusbar.c
 * @brief WOS top info bar — circular-screen top strip.
 *
 * Lives on the statusbar overlay layer, above the app page and notifications.
 * ONE centered label rotates every 10 s between:
 *   1. "HH:MM 87%"  — time + battery percent (20 s visibility cycle)
 *   2. "8/25 Tue"   — date + weekday abbr
 *
 * WHY a single centered label, no side placement:
 * the strip (y=6..28) is the narrowest band of the circular 240x240 screen —
 * at y=17 the circle still leaves only ~124 px across, and at y=6 only ~75 px.
 * The text renders with the 22 px Tiny-TTF font_small (cos_label_set_font_size
 * maps any size < COS_FONT_CFG_SMALL_SIZE to the 22 px font), i.e. ~12 px per
 * ASCII char. A single "8/25 Tue 87%" row (~140 px) is therefore clipped at
 * both edges, and a dedicated top-right battery label would fall entirely
 * outside the circle. Keeping every page <= 8 chars (~96 px) centered is the
 * only layout that never touches the curved edges while keeping the 22 px font.
 *
 * The label refreshes every second from the SAME time API as the watchface
 * (cos_time_get), so the strip never shows a stale time.
 * Transparent background: never covers watchface / app content (the layout
 * already reserves WOS_STATUSBAR_H at the top).
 */
#include "cos_wos_statusbar.h"

#include "cos_wos_theme.h"
#include "cos_overlay_layer.h"
#include "cos_log.h"
#include "cos_service_time.h"
#include "cos_service_battery.h"
#include <stdio.h>
#include <string.h>

#define COS_LOG_TAG "WosStatus"

/* 时间每秒刷新(与表盘同一 cos_time_get 源,保持同步) */
#define WOS_STATUS_REFRESH_MS (1000)
/* 每 10s 轮播一次;两页 → 电量/日期各 20s 见一次 */
#define WOS_STATUS_ROTATE_MS (10 * 1000)

static lv_obj_t *s_label = NULL; /* 顶部居中单 label */
static lv_timer_t *s_timer = NULL;
static uint8_t s_mode = 0; /* 0: 时间+电量  1: 日期+星期 */
static uint32_t s_elapsed_ms = 0;

/* day_of_week: 1=Mon .. 7=Sun (PCF8563 惯例, cos_service_time 保证) */
static const char *const _wday_abbr[8] = {
    "", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
};

static void _render(void)
{
    if (!s_label || !lv_obj_is_valid(s_label))
        return;

    cos_datetime_t dt = cos_time_get();
    char buf[24];

    if (s_mode == 0)
    {
        /* 时间页带电量(20s 一轮回),文本 8 字符 ~96px,圆内安全 */
        int8_t batt = cos_battery_get_percent(); /* -1 = no battery device */
        if (batt >= 0)
            snprintf(buf, sizeof(buf), "%02d:%02d %d%%", dt.hour, dt.min, (int)batt);
        else
            snprintf(buf, sizeof(buf), "%02d:%02d --%%", dt.hour, dt.min);

        /* 电池状态着色: 充电→绿, <20%→红, 其余→白 */
        lv_color_t c = WOS_COLOR_TEXT_PRIMARY;
        if (batt >= 0 && cos_battery_is_charging())
            c = lv_color_hex(0x66BB6A);
        else if (batt >= 0 && batt < 20)
            c = lv_color_hex(0xEF5350);
        lv_obj_set_style_text_color(s_label, c, 0);
    }
    else
    {
        const char *wday = (dt.day_of_week >= 1 && dt.day_of_week <= 7)
                               ? _wday_abbr[dt.day_of_week] : "";
        snprintf(buf, sizeof(buf), "%d/%d %s", dt.month, dt.day, wday);
        lv_obj_set_style_text_color(s_label, WOS_COLOR_TEXT_PRIMARY, 0);
    }
    lv_label_set_text(s_label, buf);
}

/* 每秒刷新时间(与表盘同步);累计到 10s 轮换一次 时间/日期 模式 */
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

    lv_obj_t *layer = cos_overlay_get_statusbar_layer();
    if (!layer)
        return;

    /* 顶部居中单 label;对齐 TOP_MID 稍下移,避开圆屏最顶部被裁切的几行像素 */
    s_label = lv_label_create(layer);
    lv_obj_set_width(s_label, lv_pct(100));
    wos_style_text_primary(s_label, 14);   /* 12→14 再加大 2 档(仍落 jbm_13 12..15 带);8 字符 ~104px,圆内 y17 界 124px 安全 */
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(s_label, LV_OPA_TRANSP, 0);
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 0, 6);

    _render();

    s_timer = lv_timer_create(_tick_cb, WOS_STATUS_REFRESH_MS, NULL);
    lv_timer_set_repeat_count(s_timer, -1);

    COS_LOG_I("wos: top info bar initialized (1s refresh, 10s rotate: time+batt <-> date)");
}
