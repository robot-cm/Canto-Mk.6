/**
 * @file cos_settings.c
 * @brief Settings page
 */

#include "cos_settings.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COS_LOG_TAG "Settings"
#include <inttypes.h>
#include "cos_log.h"
#include "lvgl.h"
#include "cJSON.h"
#include "cos_image.h"
#include "cos_msg_list.h"
#include "cos_lang.h"
#include "cos_basic_widgets.h"
#include "cos_event.h"
#include "cos_test.h"
#include "cos_version.h"
#include "cos_port.h"
#include "cos_swipe_panel.h"
#include "cos_app.h"
#include "cos_app_list.h" /* cos_sys_app_* / cos_app_launch_immediately() */
#include "cos_watchface.h"
#include "cos_theme.h"
#include "cos_pkg_mgr.h"
#include "cos_service_sensor.h"
#include "cos_config.h"
#include "cos_icon.h"
#include "cos_service_display.h"
#include "cos_service_audio.h"
#include "cos_toast.h"
#include "cos_service_storage.h"
#include "cos_service_pm.h"
#include "cos_app_header.h"
#include "cos_radio_page.h"
#include "cos_lang.h"
#include "cos_mem.h"
#include "cos_font.h"
#include "cos_std_widgets.h"
#include "cos_service_haptic.h"
#include "cos_service_display.h"
#include "cos_activity.h"
#include "cos_input_page.h"
#include "cos_net_wifi.h"
#include "cos_net_bt.h"
#include "cos_net_vpn.h"
#include "cos_service_permission.h"
#include "cos_sha256.h"
#include "cos_service_lock.h"
#include "cos_mem.h"
#include "cos_panel.h"

/* Macros and Definitions -------------------------------------*/
#define _BRIGHTNESS_SMOOTH_DURATION 200
#define _SETTINGS_PASSCODE_MAGIC 0x53504D47U

/* Settings 页彩色图标(webp 转换的 ARGB8888 静态图, 见 resources/images/icon) */
extern const lv_image_dsc_t cos_icon_wifi;
extern const lv_image_dsc_t cos_icon_bluetooth;
extern const lv_image_dsc_t cos_icon_wireguard;
extern const lv_image_dsc_t cos_icon_pwd;
extern const lv_image_dsc_t cos_icon_settings_apps;

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

/************************** General Functions **************************/

void cos_settings_slient_mode_on(void)
{
    cos_service_audio_set_mute(true);
}

void cos_settings_slient_mode_off(void)
{
    cos_service_audio_set_mute(false);
}

/************************** Helper Functions **************************/

lv_obj_t *_auto_get_config_switch_create(lv_obj_t *list, const char *txt, const char *config_key, bool default_val)
{
    lv_obj_t *sw = cos_list_add_switch(list, txt);
    lv_obj_set_state(sw, LV_STATE_CHECKED, cos_config_get_bool(config_key, default_val));
    return sw;
}

static cos_activity_t *_create_activity_with_header(lang_string_id_t id, lv_obj_t **out_view)
{
    cos_activity_t *a = cos_activity_create(NULL);
    COS_CHECK_PTR_RETURN_VAL(a, NULL);
    lv_obj_t *view = cos_activity_get_view(a);
    COS_CHECK_PTR_RETURN_VAL(view, NULL);
    cos_activity_set_title_id(a, id);
    cos_activity_set_app_header_visible(a, true);
    /* 右上角时间已在状态栏显示,Settings 系列页面不再重复显示 */
    cos_app_header_set_clock_visible(false);
    if (out_view)
    {
        *out_view = view;
    }
    return a;
}

/**
 * @brief Generic LV_EVENT_DELETE handler that frees user_data (char* allocated with cos_strdup)
 */
static void _free_user_data_on_delete_cb(lv_event_t *e)
{
    void *user_data = lv_event_get_user_data(e);
    if (user_data)
    {
        cos_free(user_data);
    }
}

/************************** Bluetooth **************************/
static void _bluetooth_enable_switch_cb(lv_event_t *e)
{
    lv_obj_t *bt_sw = lv_event_get_target(e);
    COS_CHECK_PTR_RETURN(bt_sw);
    if (lv_obj_has_state(bt_sw, LV_STATE_CHECKED))
    {
        cos_bluetooth_enable();
        cos_config_set_bool(COS_CONFIG_KEY_BLUETOOTH_BOOL, true);
    }
    else
    {
        cos_bluetooth_disable();
        cos_config_set_bool(COS_CONFIG_KEY_BLUETOOTH_BOOL, false);
    }
}

/* 蓝牙页容器句柄(单实例,页面销毁时清空) */
static lv_obj_t *s_bt_dev_container = NULL;
static lv_obj_t *s_bt_paired_container = NULL;

static void _bt_view_delete_cb(lv_event_t *e)
{
    (void)e;
    s_bt_dev_container = NULL;
    s_bt_paired_container = NULL;
}

/* BT 设备行: [蓝牙图标][名称] ─── [RSSI][Paired] */
static lv_obj_t *_bt_device_row_create(lv_obj_t *parent, const cos_bt_device_t *dev)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_color(row, COS_THEME_SECONDARY_COLOR, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, COS_LIST_OBJ_RADIUS, 0);
    lv_obj_set_style_margin_bottom(row, 8, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    /* 左: 蓝色圆底蓝牙图标 */
    lv_obj_t *round = lv_obj_create(row);
    lv_obj_set_size(round, 32, 32);
    lv_obj_set_style_bg_color(round, COS_COLOR_BLUE, 0);
    lv_obj_set_style_radius(round, 8, 0);
    lv_obj_set_style_border_width(round, 0, 0);
    lv_obj_set_style_pad_all(round, 0, 0);
    lv_obj_align(round, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *icon = lv_label_create(round);
    lv_label_set_text(icon, RI_BLUETOOTH_FILL);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_center(icon);

    /* 中: 设备名(过长滚动) */
    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, dev->name);
    lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    cos_label_set_font_size(name, COS_FONT_SIZE_SMALL);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 56, 0);
    lv_obj_set_width(name, 110);

    /* 右: RSSI + 已配对标记 */
    lv_obj_t *info = lv_label_create(row);
    char buf[40];
    if (dev->paired)
    {
        snprintf(buf, sizeof(buf), "%ddBm %s", dev->rssi, "Paired");
    }
    else
    {
        snprintf(buf, sizeof(buf), "%ddBm", dev->rssi);
    }
    lv_label_set_text(info, buf);
    lv_obj_set_style_text_color(info, lv_color_hex(0x9AA0A6), 0);
    cos_label_set_font_size(info, COS_FONT_SIZE_SMALL);
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, -12, 0);
    return row;
}

/* 设备行点击回调(定义在下方,此处前置声明供 _bt_scan_fill 使用) */
static void _bt_device_clicked_cb(lv_event_t *e);

/* 扫描结果填入设备容器(show_toast 控制是否提示) */
static void _bt_scan_fill(lv_obj_t *dev_container, bool show_toast)
{
    cos_bt_device_t devs[COS_NET_BT_SCAN_MAX];
    uint32_t count = 0;
    lv_obj_clean(dev_container);
    if (cos_net_bt_scan(devs, COS_NET_BT_SCAN_MAX, &count) != COS_OK || count == 0)
    {
        if (show_toast)
        {
            cos_toast_show(NULL, "No devices found");
        }
        return;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        char *addr_copy = cos_strdup(devs[i].addr);
        if (!addr_copy)
        {
            continue;
        }
        lv_obj_t *row = _bt_device_row_create(dev_container, &devs[i]);
        lv_obj_add_event_cb(row, _bt_device_clicked_cb, LV_EVENT_CLICKED, addr_copy);
        lv_obj_add_event_cb(row, _free_user_data_on_delete_cb, LV_EVENT_DELETE, NULL);
    }
    if (show_toast)
    {
        cos_toast_show(NULL, "Scanned");
    }
}

/* 已配对列表 */
static void _bt_paired_fill(lv_obj_t *pair_container)
{
    cos_bt_device_t devs[COS_NET_BT_SCAN_MAX];
    uint32_t count = 0;
    lv_obj_clean(pair_container);
    if (cos_net_bt_get_paired(devs, COS_NET_BT_SCAN_MAX, &count) != COS_OK || count == 0)
    {
        lv_obj_t *empty = lv_label_create(pair_container);
        lv_label_set_text(empty, "None");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9AA0A6), 0);
        cos_label_set_font_size(empty, COS_FONT_SIZE_SMALL);
        return;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        lv_obj_t *row = lv_button_create(pair_container);
        lv_obj_set_size(row, lv_pct(100), 44);
        lv_obj_set_style_bg_color(row, COS_THEME_SECONDARY_COLOR, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, COS_LIST_OBJ_RADIUS, 0);
        lv_obj_set_style_margin_bottom(row, 6, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_t *lbl = lv_label_create(row);
        char buf[COS_NET_BT_NAME_MAX + COS_NET_BT_ADDR_MAX + 2];
        snprintf(buf, sizeof(buf), "%s  %s", devs[i].name, devs[i].addr);
        lv_label_set_text(lbl, buf);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        cos_label_set_font_size(lbl, COS_FONT_SIZE_SMALL);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
    }
}

/* 扫描按钮 → 同步扫描(服务层 mock 即时返回)并填充 */
static void _bt_scan_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (!cos_net_bt_is_enabled())
    {
        cos_toast_show(NULL, "Enable Bluetooth first");
        return;
    }
    if (s_bt_dev_container && lv_obj_is_valid(s_bt_dev_container))
    {
        _bt_scan_fill(s_bt_dev_container, true);
    }
}

/* 设备行点击 → 配对(mock 返回 ok) + 刷新列表 */
static void _bt_device_clicked_cb(lv_event_t *e)
{
    const char *addr = (const char *)lv_event_get_user_data(e);
    if (!addr || !addr[0])
    {
        return;
    }
    if (!cos_net_bt_is_enabled())
    {
        cos_toast_show(NULL, "Enable Bluetooth first");
        return;
    }
    cos_result_t r = cos_net_bt_connect(addr);
    if (r == COS_OK)
    {
        cos_toast_show(NULL, "Paired: OK");
    }
    else
    {
        cos_toast_show(NULL, "Pair failed");
    }
    if (s_bt_dev_container && lv_obj_is_valid(s_bt_dev_container))
    {
        _bt_scan_fill(s_bt_dev_container, false);
    }
    if (s_bt_paired_container && lv_obj_is_valid(s_bt_paired_container))
    {
        _bt_paired_fill(s_bt_paired_container);
    }
}

static void _settings_view_bluetooth(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    cos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_BLUETOOTH, &view);
    COS_CHECK_PTR_RETURN(a && view);
    lv_obj_t *list = cos_list_create(view);

    lv_obj_t *bt_sw = _auto_get_config_switch_create(list,
                                                     cos_lang_get_text(STR_ID_SETTINGS_BLUETOOTH_ENABLE),
                                                     COS_CONFIG_KEY_BLUETOOTH_BOOL,
                                                     false);
    lv_obj_add_event_cb(bt_sw, _bluetooth_enable_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    /* 扫描按钮 + 设备列表 */
    lv_obj_t *scan_btn = cos_list_add_entry_button(list, "Scan devices");
    lv_obj_t *dev_container = cos_list_add_container(list);
    lv_obj_set_layout(dev_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dev_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dev_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(scan_btn, _bt_scan_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* 已配对列表 */
    cos_list_add_title(list, "Paired");
    lv_obj_t *pair_container = cos_list_add_container(list);
    lv_obj_set_layout(pair_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pair_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pair_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    cos_list_add_comment(list, "Saved to /sdcard/history/bt");

    /* 记录容器句柄,页面销毁时清空 */
    s_bt_dev_container = dev_container;
    s_bt_paired_container = pair_container;
    lv_obj_add_event_cb(view, _bt_view_delete_cb, LV_EVENT_DELETE, NULL);

    /* 打开页面时展示已配对设备 */
    _bt_paired_fill(pair_container);

    cos_activity_enter(a);
}

/************************** Wi-Fi Settings **************************/

typedef struct
{
    char ssid[COS_NET_WIFI_SSID_MAX + 1];
} wifi_connect_ctx_t;

static void _wifi_enable_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    COS_LOG_D("wifi switch VALUE_CHANGED: target=%p checked=%d", sw, lv_obj_has_state(sw, LV_STATE_CHECKED));
    cos_net_wifi_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* 键盘输密码 → 连接(异步:esp_wifi_connect 阻塞 15s,必须在后台 worker 执行) */
static void _wifi_pwd_input_cb(const char *text, cos_input_result_t result, void *user_data)
{
    wifi_connect_ctx_t *ctx = (wifi_connect_ctx_t *)user_data;
    if (result == COS_INPUT_RESULT_OK && text && text[0] && ctx)
    {
        COS_LOG_I("WiFi connect: ssid=%s", ctx->ssid);
        cos_result_t r = cos_net_wifi_connect_async(ctx->ssid, text);
        if (r == COS_OK)
        {
            cos_toast_show(NULL, "Connecting...");
        }
        else
        {
            cos_toast_show(NULL, "Failed");
        }
    }
    if (ctx)
    {
        cos_free(ctx);
    }
}

/* AP 行点击 → 弹键盘输密码 */
static void _wifi_ap_clicked_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    if (!ssid || !ssid[0])
    {
        return;
    }
    wifi_connect_ctx_t *ctx = (wifi_connect_ctx_t *)cos_malloc(sizeof(*ctx));
    if (!ctx)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->ssid, sizeof(ctx->ssid), "%s", ssid);
    cos_input_page_open_with_callback(NULL, _wifi_pwd_input_cb, ctx);
}

/* AP 行: [WiFi图标][SSID] ─── [🔒][dBm], 64px 紧凑高度便于显示 ≥3 个 AP */
static lv_obj_t *_wifi_ap_row_create(lv_obj_t *parent, const cos_wifi_ap_t *ap, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, lv_pct(100), 64);
    lv_obj_set_style_bg_color(row, COS_THEME_SECONDARY_COLOR, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, COS_LIST_OBJ_RADIUS, 0);
    lv_obj_set_style_margin_bottom(row, 8, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    /* 左: 蓝色圆底 WiFi 图标 */
    lv_obj_t *round = lv_obj_create(row);
    lv_obj_set_size(round, 36, 36);
    lv_obj_set_style_bg_color(round, COS_COLOR_BLUE, 0);
    lv_obj_set_style_radius(round, 8, 0);
    lv_obj_set_style_border_width(round, 0, 0);
    lv_obj_set_style_pad_all(round, 0, 0);
    lv_obj_align(round, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *icon = lv_label_create(round);
    lv_label_set_text(icon, RI_WIFI_FILL);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_center(icon);

    /* 中: SSID (22px 等宽字体, 清晰显示) */
    lv_obj_t *ssid = lv_label_create(row);
    lv_label_set_text(ssid, ap->ssid);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(ssid, lv_color_white(), 0);
    cos_label_set_font_size(ssid, COS_FONT_SIZE_SMALL);
    lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 62, 0);
    lv_obj_set_width(ssid, 95);

    /* 右: 加密锁(开放网络无锁) + 信号强度 */
    lv_obj_t *info = lv_label_create(row);
    const char *lock = (ap->auth == COS_WIFI_AUTH_OPEN) ? "" : RI_LOCK_LINE;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d dBm", lock, ap->rssi);
    lv_label_set_text(info, buf);
    lv_obj_set_style_text_color(info, lv_color_hex(0x9AA0A6), 0);
    cos_label_set_font_size(info, COS_FONT_SIZE_SMALL);
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, -12, 0);

    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, ud);
    return row;
}

/* 从缓存填充 AP 容器(UI 线程调用,不碰 esp_wifi) */
static void _wifi_scan_fill(lv_obj_t *ap_container)
{
    cos_wifi_ap_t aps[COS_NET_WIFI_SCAN_MAX];
    uint32_t count = 0;
    if (cos_net_wifi_scan_take(aps, COS_NET_WIFI_SCAN_MAX, &count) != COS_OK || count == 0)
    {
        cos_toast_show(NULL, "No Wi-Fi found");
        return;
    }

    lv_obj_clean(ap_container);   /* 清旧 AP 行，重建 */
    for (uint32_t i = 0; i < count; i++)
    {
        char *ssid_copy = cos_strdup(aps[i].ssid);
        if (!ssid_copy)
            continue;
        lv_obj_t *row = _wifi_ap_row_create(ap_container, &aps[i], _wifi_ap_clicked_cb, ssid_copy);
        lv_obj_add_event_cb(row, _free_user_data_on_delete_cb, LV_EVENT_DELETE, NULL);
    }
    cos_toast_show(NULL, "Scanned");
}

/* 扫描轮询:后台 worker 完成(esp_wifi_scan 不阻塞 UI)后取结果填充 */
typedef struct
{
    lv_obj_t *ap_container;
} wifi_scan_poll_ctx_t;

static void _wifi_scan_poll_timer_cb(lv_timer_t *t)
{
    wifi_scan_poll_ctx_t *ctx = (wifi_scan_poll_ctx_t *)lv_timer_get_user_data(t);
    if (!ctx)
    {
        lv_timer_del(t);
        return;
    }
    if (cos_net_wifi_scan_busy())
    {
        return; /* 继续轮询 */
    }
    lv_timer_del(t);
    lv_obj_t *container = ctx->ap_container;
    cos_free(ctx);
    if (container && lv_obj_is_valid(container))
    {
        _wifi_scan_fill(container);
    }
}

/* 扫描附近 Wi-Fi(异步)→ 填入 AP 容器 */
static void _wifi_scan_clicked_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *list = lv_obj_get_parent(btn);   /* entry_button 直接挂 list */
    lv_obj_t *ap_container = (lv_obj_t *)lv_event_get_user_data(e);
    if (!list || !ap_container)
    {
        return;
    }

    if (!cos_net_wifi_is_enabled())
    {
        cos_toast_show(NULL, "Enable Wi-Fi first");
        return;
    }

    if (cos_net_wifi_scan_async() != COS_OK)
    {
        cos_toast_show(NULL, cos_net_wifi_scan_busy() ? "Scanning..." : "Scan failed");
        return;
    }

    cos_toast_show(NULL, "Scanning...");
    wifi_scan_poll_ctx_t *ctx = (wifi_scan_poll_ctx_t *)cos_malloc(sizeof(*ctx));
    if (!ctx)
    {
        return;
    }
    ctx->ap_container = ap_container;
    lv_timer_t *t = lv_timer_create(_wifi_scan_poll_timer_cb, 150, ctx);
    if (!t)
    {
        cos_free(ctx);
    }
}

static void _settings_view_wifi(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    cos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_WIFI, &view);
    COS_CHECK_PTR_RETURN(a && view);
    lv_obj_t *list = cos_list_create(view);

    /* 开关 */
    lv_obj_t *sw = cos_list_add_switch(list, cos_lang_get_text(STR_ID_SETTINGS_WIFI));
    lv_obj_set_state(sw, LV_STATE_CHECKED, cos_net_wifi_is_enabled());
    lv_obj_add_event_cb(sw, _wifi_enable_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    /* 扫描按钮 */
    lv_obj_t *scan_btn = cos_list_add_entry_button(list, cos_lang_get_text(STR_ID_SETTINGS_WIFI_SCAN));
    lv_obj_t *ap_container = cos_list_add_container(list);
    /* AP 行竖排: 容器默认无布局, 多个子对象会重叠在 (0,0) 导致只显示一个 AP */
    lv_obj_set_layout(ap_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ap_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ap_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(scan_btn, _wifi_scan_clicked_cb, LV_EVENT_CLICKED, ap_container);

    /* 当前连接状态 */
    cos_wifi_state_t st = cos_net_wifi_state();
    const char *connected = cos_net_wifi_connected_ssid();
    char status[64];
    if (st == COS_WIFI_CONNECTED && connected && connected[0])
    {
        snprintf(status, sizeof(status), "%s: %s", cos_lang_get_text(STR_ID_SETTINGS_WIFI_CONNECTED), connected);
    }
    else
    {
        snprintf(status, sizeof(status), "%s", cos_lang_get_text(STR_ID_SETTINGS_WIFI_DISCONNECTED));
    }
    cos_list_add_comment(list, status);
    cos_list_add_comment(list, "Saved: /sdcard/history/wifi");

    cos_activity_enter(a);
}

/************************** VPN (WireGuard via microlink) **************************/

static void _vpn_enable_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (cos_net_vpn_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED)) != COS_OK)
    {
        cos_toast_show(NULL, "VPN failed to start");
        lv_obj_set_state(sw, LV_STATE_CHECKED, false);
    }
}

/* 输入完成回调：which=0 auth key / 1 device name */
static void _vpn_input_cb(const char *text, cos_input_result_t result, void *user_data)
{
    if (result != COS_INPUT_RESULT_OK || !text)
    {
        return;
    }
    switch ((int)(intptr_t)user_data)
    {
        case 0: cos_net_vpn_set_auth_key(text); break;
        case 1: cos_net_vpn_set_device_name(text); break;
        default: break;
    }
}

static void _vpn_row_clicked_cb(lv_event_t *e)
{
    cos_input_page_open_with_callback(NULL, _vpn_input_cb, (void *)(intptr_t)lv_event_get_user_data(e));
}

static void _settings_view_vpn(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    cos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_SOCKS5, &view);
    COS_CHECK_PTR_RETURN(a && view);
    cos_activity_set_title(a, "VPN");
    lv_obj_t *list = cos_list_create(view);

    char *auth_key = cos_net_vpn_get_auth_key();
    char *device_name = cos_net_vpn_get_device_name();

    lv_obj_t *sw = cos_list_add_switch(list, "VPN (WireGuard)");
    lv_obj_set_state(sw, LV_STATE_CHECKED, cos_net_vpn_is_enabled());
    lv_obj_add_event_cb(sw, _vpn_enable_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    cos_list_add_comment(list, "Tailscale/WireGuard via microlink");

    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    char auth_text[64];
    snprintf(auth_text, sizeof(auth_text), "%s: %s", "Auth key",
             (auth_key && auth_key[0]) ? "configured" : "-");
    lv_obj_t *auth_row = cos_list_add_entry_button(list, auth_text);
    lv_obj_add_event_cb(auth_row, _vpn_row_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    char name_text[128];
    snprintf(name_text, sizeof(name_text), "%s: %s", "Device name",
             (device_name && device_name[0]) ? device_name : "-");
    lv_obj_t *name_row = cos_list_add_entry_button(list, name_text);
    lv_obj_add_event_cb(name_row, _vpn_row_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    cos_free(auth_key);
    cos_free(device_name);

    char status[48];
    snprintf(status, sizeof(status), "Status: %s", cos_net_vpn_state_str());
    cos_list_add_comment(list, status);

    cos_activity_enter(a);
}

/************************** Display Settings **************************/

static void _brightness_slider_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    cos_display_set_brightness((uint8_t)lv_slider_get_value(sl), COS_DISPLAY_DURATION_OFF, false);
}

static void _brightness_slider_released_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(sl);
    cos_display_set_brightness((uint8_t)val, COS_DISPLAY_DURATION_OFF, false);
    cos_config_set_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, val);
}

static void _brightness_slider_minus_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(slider);
    int32_t min = lv_slider_get_min_value(slider);
    int32_t val = lv_slider_get_value(slider);
    if (val == min)
        return;
    val -= 5;
    lv_slider_set_value(slider, val, LV_ANIM_ON);
    cos_config_set_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, val);
    cos_display_set_brightness((uint8_t)val, _BRIGHTNESS_SMOOTH_DURATION, false);
}

static void _brightness_slider_plus_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(slider);
    int32_t max = lv_slider_get_max_value(slider);
    int32_t val = lv_slider_get_value(slider);
    if (val == max)
        return;
    val += 5;
    lv_slider_set_value(slider, val, LV_ANIM_ON);
    cos_config_set_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, val);
    cos_display_set_brightness((uint8_t)val, _BRIGHTNESS_SMOOTH_DURATION, false);
}

static void _aod_mode_switch_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    COS_CHECK_PTR_RETURN(obj);
    if (lv_obj_has_state(obj, LV_STATE_CHECKED))
    {
        cos_pm_set_aod_mode(true);
    }
    else
    {
        cos_pm_set_aod_mode(false);
    }
}

/* 熄屏时间选项: 5s / 10s / 15s / 30s / 45s / 60s / 300s (10s 为电池优化默认) */
static const uint32_t _sleep_timeout_options[] = {5, 10, 15, 30, 45, 60, 300};
#define _SLEEP_TIMEOUT_OPTIONS_COUNT (sizeof(_sleep_timeout_options) / sizeof(_sleep_timeout_options[0]))

static void _wake_duration_radio_list_selection_changed_cb(lv_event_t *e)
{
    uint32_t index = (uint32_t)lv_event_get_param(e);
    if (index >= _SLEEP_TIMEOUT_OPTIONS_COUNT)
    {
        return;
    }
    uint32_t wake_duration = _sleep_timeout_options[index];
    cos_pm_set_sleep_timeout(wake_duration);
    cos_config_set_number(COS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, wake_duration);
}

static void _wake_duration_entry_button_clicked_cb(lv_event_t *e)
{
    cos_radio_page_t *rp = cos_radio_page_create("Screen timeout");
    char str[32];
    uint32_t item_indexes[_SLEEP_TIMEOUT_OPTIONS_COUNT];
    for (uint32_t i = 0; i < _SLEEP_TIMEOUT_OPTIONS_COUNT; i++)
    {
        snprintf(str, sizeof(str), "%us", (unsigned int)_sleep_timeout_options[i]);
        item_indexes[i] = cos_radio_page_add_item(rp, str);
    }
    cos_radio_page_add_event_cb(rp, _wake_duration_radio_list_selection_changed_cb, NULL);
    cos_radio_page_set_comment(rp, "Auto off after no touch");
    uint32_t timeout = cos_config_get_number(COS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, 10);
    uint32_t checked_index = 1; /* default 10s */
    for (uint32_t i = 0; i < _SLEEP_TIMEOUT_OPTIONS_COUNT; i++)
    {
        if (_sleep_timeout_options[i] == timeout)
        {
            checked_index = i;
            break;
        }
    }
    cos_radio_page_check(rp, item_indexes[checked_index]);
    cos_radio_page_show(rp);
}

static void _settings_view_display(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    cos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_DISPLAY, &view);
    COS_CHECK_PTR_RETURN(a && view);

    lv_obj_t *list = cos_list_create(view);

    cos_list_slider_t *ls = cos_list_add_slider(list, cos_lang_get_text(STR_ID_SETTINGS_DISPLAY_BRIGHTNESS));
    lv_label_set_text(ls->minus_label, RI_SUN_LINE);
    lv_label_set_text(ls->plus_label, RI_SUN_FILL);
    cos_list_slider_set_minus_label_scale(ls, 200);
    lv_slider_set_value(ls->slider, cos_config_get_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, 50), LV_ANIM_ON);
    lv_slider_set_range(ls->slider, COS_DISPLAY_BRIGHTNESS_MIN, COS_DISPLAY_BRIGHTNESS_MAX);
    lv_obj_add_event_cb(ls->slider, _brightness_slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ls->slider, _brightness_slider_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(ls->minus_btn, _brightness_slider_minus_cb, LV_EVENT_CLICKED, ls->slider);
    lv_obj_add_event_cb(ls->plus_btn, _brightness_slider_plus_cb, LV_EVENT_CLICKED, ls->slider);

    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    lv_obj_t *sw = _auto_get_config_switch_create(list,
                                                  cos_lang_get_text(STR_ID_SETTINGS_DISPLAY_AOD),
                                                  COS_CONFIG_KEY_AOD_MODE_BOOL,
                                                  false);
    lv_obj_add_event_cb(sw, _aod_mode_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    cos_list_add_comment(list, cos_lang_get_text(STR_ID_SETTINGS_DISPLAY_AOD_COMMENT));
    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    cos_list_add_title(list, "Screen");

    lv_obj_t *wd_btn = cos_list_add_entry_button(list, "Screen timeout");
    lv_obj_add_event_cb(wd_btn, _wake_duration_entry_button_clicked_cb, LV_EVENT_CLICKED, NULL);
    cos_activity_enter(a);
}
/************************** Notification **************************/
static void _settings_view_notification(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    cos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_NOTIFICATION, &view);
    COS_CHECK_PTR_RETURN(a && view);
    lv_obj_t *list = cos_list_create(view);
    LV_UNUSED(list);
    // TODO: Notification settings
    cos_activity_enter(a);
}

/************************** App List **************************/

typedef struct
{
    const char *app_id;
    const char *app_name;
    char title[128];
} _uninstall_ctx_t;

/**
 * @brief Uninstall confirmation panel callback (actual uninstall action)
 */
static void _uninstall_confirm_cb(lv_event_t *e)
{
    _uninstall_ctx_t *ctx = (_uninstall_ctx_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx);
    cos_app_uninstall(ctx->app_id);
    cos_free((void *)ctx->app_name);
    cos_free(ctx);
    cos_activity_back();
}

/**
 * @brief Uninstall button callback - shows confirmation panel
 */
static void _uninstall_btn_cb(lv_event_t *e)
{
    _uninstall_ctx_t *ctx = (_uninstall_ctx_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx);

    cos_activity_t *current = cos_activity_get_current();
    COS_CHECK_PTR_RETURN(current);

    snprintf(ctx->title,
             sizeof(ctx->title),
             "%s \"%s\"",
             cos_lang_get_text(STR_ID_SETTINGS_APPS_UNINSTALL),
             ctx->app_name ? ctx->app_name : ctx->app_id);

    cos_panel_cfg_t cfg = {
        .icon_bg_color = COS_THEME_DANGEROS_COLOR,
        .icon_type = COS_PANEL_ICON_TYPE_SYMBOL,
        .icon_src = RI_ALERT_FILL,
        .title_text = ctx->title,
        .message_id = STR_ID_SETTINGS_APPS_UNINSTALL_MSG,
        .confirm_btn_id = STR_ID_SETTINGS_APPS_UNINSTALL,
        .confirm_cb = NULL,
        .cancel_btn_id = STR_ID_CANCEL,
        .cancel_cb = NULL,
    };

    cos_panel_t *panel = cos_panel_create_on_activity(current, &cfg);
    if (panel)
    {
        lv_obj_add_event_cb(panel->confirm_btn, _uninstall_confirm_cb, LV_EVENT_CLICKED, ctx);
    }
}

/**
 * @brief Clear data confirmation panel callback (actual clear action)
 */
static void _clear_data_confirm_cb(lv_event_t *e)
{
    typedef struct
    {
        const char *app_id;
        lv_obj_t *orig_btn;
    } _clear_data_ctx_t;
    _clear_data_ctx_t *ctx = (_clear_data_ctx_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx);

    char data_path[COS_FS_PATH_MAX];
    snprintf(data_path, sizeof(data_path), COS_APP_DATA_DIR "%s", ctx->app_id);
    if (cos_storage_rm_recursive(data_path) != COS_OK)
    {
        COS_LOG_E("Remove data failed");
        cos_free(ctx);
        return;
    }
    if (ctx->orig_btn)
    {
        lv_obj_add_state(ctx->orig_btn, LV_STATE_DISABLED);
    }
    cos_toast_show_char_icon(RI_CHECKBOX_CIRCLE_FILL,
                             COS_COLOR_GREEN,
                             cos_lang_get_text(STR_ID_SETTINGS_APPS_CLEAR_DATA_SUCCESS));
    cos_free(ctx);
}

/**
 * @brief Clear data button callback - shows confirmation panel
 */
static void _clear_data_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *app_id = (const char *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(app_id);

    cos_activity_t *current = cos_activity_get_current();
    COS_CHECK_PTR_RETURN(current);

    typedef struct
    {
        const char *app_id;
        lv_obj_t *orig_btn;
    } _clear_data_ctx_t;
    _clear_data_ctx_t *ctx = cos_malloc(sizeof(_clear_data_ctx_t));
    COS_CHECK_PTR_RETURN(ctx);
    ctx->app_id = app_id;
    ctx->orig_btn = btn;

    cos_panel_cfg_t cfg = {
        .icon_bg_color = COS_THEME_DANGEROS_COLOR,
        .icon_type = COS_PANEL_ICON_TYPE_SYMBOL,
        .icon_src = RI_ALERT_FILL,
        .title_id = STR_ID_SETTINGS_APPS_CLEAR_DATA,
        .message_text = app_id,
        .confirm_btn_id = STR_ID_SETTINGS_APPS_CLEAR_DATA,
        .confirm_cb = NULL,
        .cancel_btn_id = STR_ID_CANCEL,
        .cancel_cb = NULL,
    };

    cos_panel_t *panel = cos_panel_create_on_activity(current, &cfg);
    if (panel)
    {
        lv_obj_add_event_cb(panel->confirm_btn, _clear_data_confirm_cb, LV_EVENT_CLICKED, ctx);
    }
}

/* ---- Forward declarations for permission radio page ---- */
typedef struct
{
    const char *app_id;
    cos_perm_category_t category;
    lv_obj_t *state_label; /* reference to status label for refresh */
} _perm_radio_ctx_t;

static void _perm_row_click_cb(lv_event_t *e);
static void _perm_state_radio_cb(lv_event_t *e);

/**
 * @brief Permission entry button clicked - opens permission list page for this app
 */
static void _settings_perm_entry_clicked_cb(lv_event_t *e)
{
    const char *app_id = (const char *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(app_id);

    /* Read manifest to get permissions list */
    char manifest_path[COS_FS_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), COS_APP_INSTALLED_DIR "%s/" COS_APP_MANIFEST_FILE_NAME, app_id);
    script_pkg_t pkg = {0};
    if (script_engine_get_manifest(manifest_path, &pkg) != COS_OK)
    {
        COS_LOG_E("Read manifest failed: %s", manifest_path);
        return;
    }

    cos_activity_t *a = cos_activity_create(NULL);
    COS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = cos_activity_get_view(a);
    COS_CHECK_PTR_RETURN(view);

    cos_activity_set_title_id(a, STR_ID_PERM_TITLE);
    cos_activity_set_app_header_visible(a, true);
    cos_app_header_set_clock_visible(false);

    lv_obj_t *list = cos_list_create(view);

    if (pkg.permission_count == 0)
    {
        cos_list_add_comment(list, cos_lang_get_text(STR_ID_PERM_NO_PERMISSIONS));
    }
    else
    {
        for (uint8_t i = 0; i < pkg.permission_count; i++)
        {
            if (!pkg.permissions[i])
                continue;

            cos_perm_category_t cat = cos_permission_name_to_category(pkg.permissions[i]);
            if (cat >= COS_PERM_CATEGORY_COUNT)
                continue;

            const char *perm_display_name = cos_permission_category_name(cat);
            cos_perm_state_t current_state = cos_permission_get(app_id, cat);
            const char *state_label = cos_lang_get_text(cos_permission_state_label_id(current_state));

            /* Create entry button: top=name(white), bottom=status(grey) */
            lv_obj_t *perm_btn = cos_list_add_entry_button(list, perm_display_name ? perm_display_name : "");

            /* Switch to vertical two-line layout */
            lv_obj_set_flex_flow(perm_btn, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(perm_btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

            /* Permission name: white text (top line), left-aligned */
            lv_obj_t *name_label = lv_obj_get_child(perm_btn, 0);
            if (name_label)
            {
                lv_obj_set_style_text_color(name_label, COS_COLOR_WHITE, 0);
                lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_LEFT, 0);
                lv_obj_set_flex_grow(name_label, 0);
                /* Label must fill button width for text-align-left to be visible */
                lv_obj_set_width(name_label, lv_pct(100));
            }

            /* Replace the right-side arrow label with permission state text (bottom line) */
            lv_obj_t *state_label_obj = lv_obj_get_child(perm_btn, 1);
            if (state_label_obj && state_label)
            {
                lv_label_set_text(state_label_obj, state_label);
                /* Permission status: grey text, left-aligned */
                lv_obj_set_style_text_color(state_label_obj, COS_COLOR_GREY, 0);
                lv_obj_set_style_text_align(state_label_obj, LV_TEXT_ALIGN_LEFT, 0);
                lv_obj_set_width(state_label_obj, lv_pct(100));
            }

            /* Allocate context for radio page callback */
            _perm_radio_ctx_t *radio_ctx = cos_malloc(sizeof(_perm_radio_ctx_t));
            if (radio_ctx)
            {
                radio_ctx->app_id = app_id;
                radio_ctx->category = cat;
                radio_ctx->state_label = state_label_obj;
                lv_obj_add_event_cb(perm_btn, _perm_row_click_cb, LV_EVENT_CLICKED, radio_ctx);
            }
        }
    }

    cos_activity_enter(a);
    cos_pkg_free(&pkg);
}

/**
 * @brief Permission state radio selection callback
 */
static void _perm_state_radio_cb(lv_event_t *e)
{
    _perm_radio_ctx_t *ctx = (_perm_radio_ctx_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx);

    uint32_t selected = (uint32_t)(uintptr_t)lv_event_get_param(e);
    if (selected <= COS_PERM_STATE_ALLOW_ALWAYS)
    {
        cos_permission_set(ctx->app_id, ctx->category, (cos_perm_state_t)selected);
        COS_LOG_D("Permission state changed: app=%s cat=%d state=%lu", ctx->app_id, ctx->category, selected);

        /* Refresh status label text on the list page */
        if (ctx->state_label && lv_obj_is_valid(ctx->state_label))
        {
            const char *new_state_label = cos_lang_get_text(cos_permission_state_label_id((cos_perm_state_t)selected));
            lv_label_set_text(ctx->state_label, new_state_label ? new_state_label : "");
        }

        cos_activity_back();
    }
}

/**
 * @brief Permission row click callback - opens radio page for state selection
 */
static void _perm_row_click_cb(lv_event_t *e)
{
    _perm_radio_ctx_t *ctx = (_perm_radio_ctx_t *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(ctx);

    cos_perm_state_t current_state = cos_permission_get(ctx->app_id, ctx->category);
    const char *perm_name = cos_permission_category_name(ctx->category);

    cos_radio_page_t *rp = cos_radio_page_create(perm_name ? perm_name : "");
    cos_radio_page_add_item(rp, cos_lang_get_text(STR_ID_PERM_STATE_DENIED));
    cos_radio_page_add_item(rp, cos_lang_get_text(STR_ID_PERM_ALLOW_ONCE));
    cos_radio_page_add_item(rp, cos_lang_get_text(STR_ID_PERM_ALLOW_FOREGROUND));
    cos_radio_page_add_item(rp, cos_lang_get_text(STR_ID_PERM_STATE_ALLOW_ALWAYS));
    cos_radio_page_add_event_cb(rp, _perm_state_radio_cb, ctx);
    cos_radio_page_check(rp, (uint32_t)current_state);
    cos_radio_page_show(rp);
}

/**
 * @brief App list callback, opens app details
 * @param e
 */
static void _settings_app_list_btn_cb(lv_event_t *e)
{
    const char *app_id = (const char *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(app_id);

    // Get app manifest
    char manifest_path[COS_FS_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), COS_APP_INSTALLED_DIR "%s/" COS_APP_MANIFEST_FILE_NAME, app_id);
    script_pkg_t pkg = {0};
    if (script_engine_get_manifest(manifest_path, &pkg) != COS_OK)
    {
        COS_LOG_E("Read manifest failed: %s", manifest_path);
        return;
    }
    COS_LOG_D("App Info:\n"
              "id=%s | name=%s | version=%s |\n"
              "author:%s | description:%s",
              pkg.id,
              pkg.name,
              pkg.version,
              pkg.version,
              pkg.description);
    char script_path[COS_FS_PATH_MAX];
    snprintf(script_path, sizeof(script_path), COS_APP_INSTALLED_DIR "%s/" COS_APP_SCRIPT_ENTRY_FILE_NAME, app_id);
    if (!cos_storage_is_file(script_path))
    {
        COS_LOG_E("Can't find script: %s", script_path);
        cos_pkg_free(&pkg);
        return;
    }

    // Create new activity for app details
    cos_activity_t *a = cos_activity_create(NULL);
    COS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = cos_activity_get_view(a);
    COS_CHECK_PTR_RETURN(view);
    cos_activity_set_title(a, pkg.name);
    cos_activity_set_app_header_visible(a, true);
    cos_app_header_set_clock_visible(false);

    lv_obj_t *list = cos_list_create(view);

    lv_obj_t *container = cos_list_add_container(list);
    lv_obj_set_size(container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    char icon_path[COS_FS_PATH_MAX];
    snprintf(icon_path, sizeof(icon_path), COS_APP_INSTALLED_DIR "%s/" COS_APP_ICON_FILE_NAME, app_id);
    if (!cos_storage_is_file(icon_path))
    {
        memcpy(icon_path, COS_IMG_APP, sizeof(COS_IMG_APP));
    }
    cos_row_create(container, NULL, pkg.name, icon_path, 100, 100);

    cos_row_create(container, cos_lang_get_text(STR_ID_SETTINGS_APPS_APPID), app_id, NULL, 0, 0);
    cos_row_create(container, cos_lang_get_text(STR_ID_SETTINGS_APPS_AUTHOR), pkg.author, NULL, 0, 0);
    cos_row_create(container, cos_lang_get_text(STR_ID_SETTINGS_APPS_VERSION), pkg.version, NULL, 0, 0);

    char sdk_buf[32];
    snprintf(sdk_buf, sizeof(sdk_buf), "Min: %d, Target: %d", pkg.min_api_level, pkg.target_api_level);
    cos_row_create(container, cos_lang_get_text(STR_ID_SETTINGS_APPS_SDK), sdk_buf, NULL, 0, 0);

    if (strcmp(pkg.description, "") != 0)
    {
        lv_obj_t *inner_container =
            cos_list_add_title_container(list, cos_lang_get_text(STR_ID_SETTINGS_APPS_DESCRIPTON));
        lv_obj_set_size(inner_container, lv_pct(100), LV_SIZE_CONTENT);

        lv_obj_t *desc_label = lv_label_create(inner_container);
        lv_label_set_text(desc_label, pkg.description);
        lv_obj_set_width(desc_label, lv_pct(100));
        lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    }

    /* ---- Permission Entry ---- */
    if (pkg.permission_count > 0)
    {
        cos_list_add_title(list, cos_lang_get_text(STR_ID_PERM_TITLE));
        lv_obj_t *perm_entry_btn = cos_list_add_button(list, NULL, cos_lang_get_text(STR_ID_PERM_TITLE));
        /* Store app_id for the callback - must persist until activity is destroyed */
        char *stored_app_id = cos_strdup(app_id);
        lv_obj_add_event_cb(perm_entry_btn, _settings_perm_entry_clicked_cb, LV_EVENT_CLICKED, stored_app_id);
        /* Free stored_app_id when button is deleted */
        lv_obj_add_event_cb(perm_entry_btn, _free_user_data_on_delete_cb, LV_EVENT_DELETE, stored_app_id);
    }

    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    lv_obj_t *clear_data_btn = cos_button_create_ex(list,
                                                    COS_THEME_SECONDARY_COLOR,
                                                    cos_lang_get_text(STR_ID_SETTINGS_APPS_CLEAR_DATA),
                                                    COS_THEME_DANGEROS_COLOR,
                                                    _clear_data_btn_cb,
                                                    (void *)(uintptr_t)app_id);

    cos_list_add_placeholder(list, 20);

    char data_path[COS_FS_PATH_MAX];
    snprintf(data_path, sizeof(data_path), COS_APP_DATA_DIR "%s", app_id);
    if (!cos_storage_is_dir(data_path))
    {
        lv_obj_add_state(clear_data_btn, LV_STATE_DISABLED);
    }

    _uninstall_ctx_t *uninstall_ctx = cos_malloc(sizeof(_uninstall_ctx_t));
    if (uninstall_ctx)
    {
        uninstall_ctx->app_id = app_id;
        uninstall_ctx->app_name = cos_strdup(pkg.name);
    }

    lv_obj_t *uninstall_btn = cos_button_create_ex(list,
                                                   COS_THEME_SECONDARY_COLOR,
                                                   cos_lang_get_text(STR_ID_SETTINGS_APPS_UNINSTALL),
                                                   COS_THEME_DANGEROS_COLOR,
                                                   _uninstall_btn_cb,
                                                   uninstall_ctx);

    (void)uninstall_btn;
    cos_activity_enter(a);
    cos_pkg_free(&pkg);
}

/* Launch a built-in system app (sys.*) directly from the App list. */
static void _settings_sys_app_btn_cb(lv_event_t *e)
{
    const char *app_id = (const char *)lv_event_get_user_data(e);
    COS_CHECK_PTR_RETURN(app_id);
    if (cos_app_launch_immediately(app_id) != COS_OK)
    {
        COS_LOG_W("Failed to launch system app '%s'", app_id);
    }
}

static void _app_btn_create(lv_obj_t *parent, const char *app_id)
{
    COS_CHECK_PTR_RETURN(parent && app_id);

    /* Built-in system apps have no manifest / icon.bin on disk: show the
     * built-in icon + a friendly name and launch them directly on tap. */
    for (int si = 0; si < COS_SYS_APP_LAST; si++)
    {
        if (strcmp(app_id, cos_sys_app_id_list[si]) == 0)
        {
            const char *sys_name = "App";
            if (strcmp(app_id, "sys.settings") == 0)
            {
                sys_name = "Settings";
            }
            else if (strcmp(app_id, "sys.flash_light") == 0)
            {
                sys_name = "Flash Light";
            }
            lv_obj_t *sys_btn = cos_list_add_button(parent, cos_sys_app_icon_list[si], sys_name);
            if (sys_btn)
            {
                lv_obj_add_event_cb(sys_btn, _settings_sys_app_btn_cb, LV_EVENT_CLICKED,
                                    (void *)cos_sys_app_id_list[si]);
            }
            return;
        }
    }

    char icon_path[COS_FS_PATH_MAX];
    snprintf(icon_path, sizeof(icon_path), COS_APP_INSTALLED_DIR "%s/" COS_APP_ICON_FILE_NAME, app_id);
    if (!cos_storage_is_file(icon_path))
    {
        memcpy(icon_path, COS_IMG_APP, sizeof(COS_IMG_APP));
    }

    /* Resolve the display name from the manifest; fall back to the app id so
     * the row always renders (e.g. unpack failure / missing manifest). */
    char manifest_path[COS_FS_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), COS_APP_INSTALLED_DIR "%s/" COS_APP_MANIFEST_FILE_NAME, app_id);
    script_pkg_t pkg = {0};
    const char *display_name = app_id;
    if (script_engine_get_manifest(manifest_path, &pkg) == COS_OK)
    {
        display_name = pkg.name ? pkg.name : app_id;
    }
    else
    {
        COS_LOG_W("Manifest missing, showing app id: %s", app_id);
    }

    /* Keep our own copy of app_id: the plugin manager list may be freed or
     * rebuilt (uninstall / rescan) while this button is still alive. */
    char *stored_id = cos_strdup(app_id);
    if (!stored_id)
    {
        cos_pkg_free(&pkg);
        return;
    }

    lv_obj_t *btn = cos_list_add_button(parent, icon_path, display_name);
    if (btn)
    {
        lv_obj_add_event_cb(btn, _settings_app_list_btn_cb, LV_EVENT_CLICKED, stored_id);
        lv_obj_add_event_cb(btn, _free_user_data_on_delete_cb, LV_EVENT_DELETE, stored_id);
        cos_app_obj_auto_delete(btn, stored_id);
    }
    else
    {
        cos_free(stored_id);
    }
    cos_pkg_free(&pkg);
}

static void _app_installed_cb(cos_event_t *e)
{
    lv_obj_t *parent = cos_event_get_user_data(e);
    const char *installed_app_id = cos_event_get_param(e);
    COS_CHECK_PTR_RETURN(parent && installed_app_id);
    _app_btn_create(parent, installed_app_id);
}

/**
 * @brief App list in system settings
 */
static void _settings_view_apps_on_destroy(cos_activity_t *a)
{
    lv_obj_t *view = cos_activity_get_view(a);
    if (view)
    {
        lv_obj_t *app_list = lv_obj_get_child(view, 0);
        if (app_list)
        {
            cos_event_unsubscribe_with_obj(COS_EVENT_APP_INSTALLED, _app_installed_cb, app_list);
        }
    }
}

static void _settings_view_apps(lv_event_t *e)
{
    // Create a new activity to draw the app list
    static const cos_activity_lifecycle_t lifecycle = {.on_destroy = _settings_view_apps_on_destroy,
                                                       .on_enter = NULL,
                                                       .on_pause = NULL,
                                                       .on_resume = NULL};

    cos_activity_t *a = cos_activity_create(&lifecycle);
    COS_CHECK_PTR_RETURN(a);

    lv_obj_t *view = cos_activity_get_view(a);
    COS_CHECK_PTR_RETURN(view);

    cos_activity_set_title_id(a, STR_ID_SETTINGS_APPS);
    cos_activity_set_app_header_visible(a, true);
    cos_app_header_set_clock_visible(false);

    lv_obj_t *app_list = cos_list_create(view);
    cos_event_subscribe_ex(COS_EVENT_APP_INSTALLED, _app_installed_cb, NULL, app_list);

    size_t app_list_size = cos_app_get_installed();
    if (app_list_size == 0)
    {
        cos_list_add_comment(app_list, cos_lang_get_text(STR_ID_SETTINGS_APPS_EMPTY));
    }
    for (size_t i = 0; i < app_list_size; i++)
    {
        _app_btn_create(app_list, cos_app_list_get_id(i));
    }

    cos_activity_enter(a);
}

/************************** Password Settings **************************/

/* 密码最长长度（input page 上限 511，这里留余量） */
#define _PASSWORD_INPUT_MAX_LEN (255u)

typedef enum
{
    _PASSWORD_STAGE_VERIFY_OLD = 0, /* 修改/关闭流程：先验证旧密码 */
    _PASSWORD_STAGE_NEW,            /* 输入新密码 */
    _PASSWORD_STAGE_CONFIRM,        /* 再次输入新密码确认 */
} _password_stage_t;

typedef enum
{
    _PASSWORD_FLOW_CREATE = 0, /* 创建密码 */
    _PASSWORD_FLOW_CHANGE,     /* 修改密码 */
    _PASSWORD_FLOW_DISABLE,    /* 关闭密码 */
} _password_flow_t;

typedef struct _password_flow_state_t
{
    uint32_t magic;
    _password_stage_t stage;
    bool is_disabling;              /* true = 关闭密码流程 */
    uint8_t old_attempts;           /* 旧密码连续错误次数 */
    char step1_digits[_PASSWORD_INPUT_MAX_LEN + 1];
} _password_flow_state_t;

/* References to password sub-page widgets that need dynamic updates */
typedef struct _password_subpage_refs_t
{
    lv_obj_t *enable_sw;
    lv_obj_t *change_btn;
    lv_obj_t *list;
} _password_subpage_refs_t;

static void _password_subpage_refresh(_password_subpage_refs_t *refs);

/* Forward declarations for password input page flow */
static void _password_enable_sw_cb(lv_event_t *e);
static void _password_change_clicked_cb(lv_event_t *e);
static void _settings_view_password(lv_event_t *e);

/* ---- Password Input Page Helper ---- */

static bool _password_matches_hash(const char *digits)
{
    uint8_t hash[COS_SHA256_DIGEST_SIZE];
    cos_sha256((const uint8_t *)digits, strlen(digits), hash);
    char entered_hex[COS_SHA256_HEX_STR_SIZE];
    cos_sha256_to_hex(hash, entered_hex, sizeof(entered_hex));

    char *stored_hash = cos_config_get_string(COS_CONFIG_KEY_PASSWORD_HASH_STR, "");
    bool match = (stored_hash && strcmp(entered_hex, stored_hash) == 0);
    cos_free(stored_hash);
    return match;
}

static void _password_close_cb(const char *text, cos_input_result_t result, void *user_data);
static void _password_open_next_async(void *user_data);

/* ---- Password Settings Page ---- */

/**
 * @brief Save password hash to config
 */
static void _save_password_hash(const char *digits)
{
    uint8_t hash[COS_SHA256_DIGEST_SIZE];
    cos_sha256((const uint8_t *)digits, strlen(digits), hash);

    char hex[COS_SHA256_HEX_STR_SIZE];
    cos_sha256_to_hex(hash, hex, sizeof(hex));

    cos_config_set_string(COS_CONFIG_KEY_PASSWORD_HASH_STR, hex);
    cos_config_set_bool(COS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, true);
    COS_LOG_I("Password saved");
}

static void _password_close_cb(const char *text, cos_input_result_t result, void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (!state)
        return;

    if (result == COS_INPUT_RESULT_CANCEL)
    {
        /* 确认步取消 → 回到新密码输入；其他步骤取消 → 退出整个流程 */
        if (state->stage == _PASSWORD_STAGE_CONFIRM)
        {
            state->stage = _PASSWORD_STAGE_NEW;
            lv_async_call(_password_open_next_async, state);
        }
        else
        {
            cos_free(state);
        }
        return;
    }

    const char *digits = text ? text : "";

    switch (state->stage)
    {
    case _PASSWORD_STAGE_VERIFY_OLD:
        if (_password_matches_hash(digits))
        {
            if (state->is_disabling)
            {
                /* 旧密码正确 → 关闭密码 */
                cos_config_set_bool(COS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, false);
                cos_config_set_string(COS_CONFIG_KEY_PASSWORD_HASH_STR, "");
                cos_toast_show_char_icon(RI_CHECKBOX_CIRCLE_FILL, COS_COLOR_GREEN, "Password disabled");
                cos_free(state);
            }
            else
            {
                /* 修改流程：进入新密码输入 */
                state->stage = _PASSWORD_STAGE_NEW;
                lv_async_call(_password_open_next_async, state);
            }
        }
        else
        {
            state->old_attempts++;
            cos_toast_show_char_icon(RI_ERROR_WARNING_FILL,
                                     COS_COLOR_RED,
                                     cos_lang_get_text(STR_ID_LOCK_SCREEN_WRONG_PASSCODE));
            if (state->old_attempts >= 3)
            {
                cos_free(state);
                return;
            }
            /* 重新输入旧密码 */
            lv_async_call(_password_open_next_async, state);
        }
        break;

    case _PASSWORD_STAGE_NEW:
    case _PASSWORD_STAGE_CONFIRM:
        if (digits[0] == '\0')
        {
            cos_toast_show_char_icon(RI_ERROR_WARNING_FILL, COS_COLOR_RED, "Password cannot be empty");
            state->stage = _PASSWORD_STAGE_NEW;
            lv_async_call(_password_open_next_async, state);
            break;
        }
        if (state->stage == _PASSWORD_STAGE_NEW)
        {
            strncpy(state->step1_digits, digits, _PASSWORD_INPUT_MAX_LEN);
            state->step1_digits[_PASSWORD_INPUT_MAX_LEN] = '\0';
            state->stage = _PASSWORD_STAGE_CONFIRM;
            lv_async_call(_password_open_next_async, state);
        }
        else /* _PASSWORD_STAGE_CONFIRM */
        {
            if (strcmp(state->step1_digits, digits) == 0)
            {
                _save_password_hash(digits);
                cos_toast_show_char_icon(RI_CHECKBOX_CIRCLE_FILL, COS_COLOR_GREEN, "Password enabled");
                cos_free(state);
            }
            else
            {
                cos_toast_show_char_icon(RI_ERROR_WARNING_FILL,
                                         COS_COLOR_RED,
                                         cos_lang_get_text(STR_ID_SETTINGS_PASSWORD_MISMATCH));
                state->stage = _PASSWORD_STAGE_NEW;
                lv_async_call(_password_open_next_async, state);
            }
        }
        break;

    default:
        cos_free(state);
        break;
    }
}

static void _password_open_next_async(void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (!state)
        return;
    cos_input_page_open_with_callback(NULL, _password_close_cb, state);
}

static void _password_start_create(void)
{
    _password_flow_state_t *state = (_password_flow_state_t *)cos_malloc_zeroed(sizeof(_password_flow_state_t));
    if (!state)
        return;
    state->magic = _SETTINGS_PASSCODE_MAGIC;
    state->stage = _PASSWORD_STAGE_NEW;
    cos_input_page_open_with_callback(NULL, _password_close_cb, state);
}

/* ---- Password Change / Disable Flow ---- */

static void _password_start_change(void)
{
    _password_flow_state_t *state = (_password_flow_state_t *)cos_malloc_zeroed(sizeof(_password_flow_state_t));
    if (!state)
        return;
    state->magic = _SETTINGS_PASSCODE_MAGIC;
    state->stage = _PASSWORD_STAGE_VERIFY_OLD;
    cos_input_page_open_with_callback(NULL, _password_close_cb, state);
}

static void _password_start_disable(void)
{
    _password_flow_state_t *state = (_password_flow_state_t *)cos_malloc_zeroed(sizeof(_password_flow_state_t));
    if (!state)
        return;
    state->magic = _SETTINGS_PASSCODE_MAGIC;
    state->stage = _PASSWORD_STAGE_VERIFY_OLD;
    state->is_disabling = true;
    cos_input_page_open_with_callback(NULL, _password_close_cb, state);
}

static void _password_start_flow_async(void *user_data)
{
    uint8_t flow = (uint8_t)(uintptr_t)user_data;
    if (flow == _PASSWORD_FLOW_CHANGE)
    {
        _password_start_change();
    }
    else if (flow == _PASSWORD_FLOW_DISABLE)
    {
        _password_start_disable();
    }
    else
    {
        _password_start_create();
    }
}

/* ---- Password Settings Sub-Page ---- */

static void _password_subpage_on_destroy(cos_activity_t *activity)
{
    _password_subpage_refs_t *refs = (_password_subpage_refs_t *)cos_activity_get_user_data(activity);
    if (refs)
        cos_free(refs);
}

static void _password_subpage_refresh(_password_subpage_refs_t *refs)
{
    if (!refs)
        return;
    bool is_enabled = cos_config_get_bool(COS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, false);
    char *hash = cos_config_get_string(COS_CONFIG_KEY_PASSWORD_HASH_STR, "");
    bool has_hash = (hash && strlen(hash) > 0);
    cos_free(hash);

    /* Update Enable switch */
    if (refs->enable_sw && lv_obj_is_valid(refs->enable_sw))
    {
        if (is_enabled && has_hash)
        {
            lv_obj_add_state(refs->enable_sw, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(refs->enable_sw, LV_STATE_CHECKED);
        }
    }

    /* Enable/disable Change Password button (disabled when no password set) */
    if (refs->change_btn && lv_obj_is_valid(refs->change_btn))
    {
        if (is_enabled && has_hash)
        {
            lv_obj_remove_state(refs->change_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(refs->change_btn, LV_STATE_DISABLED);
        }
    }
}

static void _password_subpage_on_resume(cos_activity_t *activity)
{
    _password_subpage_refs_t *refs = (_password_subpage_refs_t *)cos_activity_get_user_data(activity);
    _password_subpage_refresh(refs);
}

static void _password_enable_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    (void)sw;
    bool is_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (is_enabled)
    {
        /* 开启密码：输入新密码 + 确认 */
        lv_async_call(_password_start_flow_async, (void *)(uintptr_t)_PASSWORD_FLOW_CREATE);
    }
    else
    {
        /* 关闭密码：必须先验证旧密码；验证通过后由 on_resume 刷新开关状态 */
        lv_async_call(_password_start_flow_async, (void *)(uintptr_t)_PASSWORD_FLOW_DISABLE);
    }
}

static void _password_change_clicked_cb(lv_event_t *e)
{
    (void)e;
    /* 修改密码：验证旧密码 → 新密码 → 确认新密码 */
    lv_async_call(_password_start_flow_async, (void *)(uintptr_t)_PASSWORD_FLOW_CHANGE);
}

static void _settings_view_password(lv_event_t *e)
{
    (void)e;
    static const cos_activity_lifecycle_t lifecycle = {
        .on_destroy = _password_subpage_on_destroy,
        .on_enter = _password_subpage_on_resume,
        .on_resume = _password_subpage_on_resume,
    };

    cos_activity_t *a = cos_activity_create(&lifecycle);
    COS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = cos_activity_get_view(a);
    COS_CHECK_PTR_RETURN(view);
    cos_activity_set_title_id(a, STR_ID_SETTINGS_PASSWORD);
    cos_activity_set_app_header_visible(a, true);
    cos_app_header_set_clock_visible(false);

    _password_subpage_refs_t *refs = (_password_subpage_refs_t *)cos_malloc_zeroed(sizeof(_password_subpage_refs_t));
    cos_activity_set_user_data(a, refs);

    lv_obj_t *list = cos_list_create(view);
    refs->list = list;

    bool is_enabled = cos_config_get_bool(COS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, false);
    char *hash = cos_config_get_string(COS_CONFIG_KEY_PASSWORD_HASH_STR, "");
    bool has_hash = (hash && strlen(hash) > 0);

    /* Enable Password switch */
    refs->enable_sw = _auto_get_config_switch_create(list,
                                                     cos_lang_get_text(STR_ID_SETTINGS_PASSWORD_ENABLE),
                                                     COS_CONFIG_KEY_PASSWORD_ENABLED_BOOL,
                                                     false);
    lv_obj_add_event_cb(refs->enable_sw, _password_enable_sw_cb, LV_EVENT_VALUE_CHANGED, refs);
    /* Tag the switch container so list transition animation targets this row */
    lv_obj_t *enable_container = lv_obj_get_parent(refs->enable_sw);
    if (enable_container)
        lv_obj_add_flag(enable_container, LV_OBJ_FLAG_USER_1);

    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    /* Change Password button (disabled when no password) */
    refs->change_btn = cos_list_add_entry_button_str_id(list, STR_ID_SETTINGS_PASSWORD_CHANGE);
    lv_obj_add_event_cb(refs->change_btn, _password_change_clicked_cb, LV_EVENT_CLICKED, NULL);
    if (!is_enabled || !has_hash)
    {
        lv_obj_add_state(refs->change_btn, LV_STATE_DISABLED);
    }
    cos_list_add_placeholder(list, COS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    cos_free(hash);
    cos_activity_enter(a);
}

/************************** General Settings **************************/

static void _device_name_input_closed_cb(const char *text, cos_input_result_t result, void *user_data)
{
    if (result != COS_INPUT_RESULT_OK)
    {
        return;
    }

    lv_obj_t *label = (lv_obj_t *)user_data;
    const char *new_name = text ? text : "";

    if (cos_config_set_string(COS_CONFIG_KEY_DEVICE_NAME_STR, new_name) == COS_OK)
    {
        if (label && lv_obj_is_valid(label))
        {
            lv_label_set_text(label, new_name);
        }
    }
    else
    {
        COS_LOG_E("Failed to save device name to config");
    }
}

static void _device_name_click_cb(lv_event_t *e)
{
    lv_obj_t *label = lv_event_get_target(e);
    COS_CHECK_PTR_RETURN(label);

    cos_result_t result = cos_input_page_open_with_callback(label, _device_name_input_closed_cb, label);

    if (result != COS_OK)
    {
        COS_LOG_E("Failed to open input page for device name edit");
    }
}

static void _settings_view_device_info(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    cos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_GENERAL_DEVICE_INFO, &view);
    COS_CHECK_PTR_RETURN(a && view);
    cos_activity_set_title_color(a, COS_THEME_LOGO_PRIMARY_COLOR);

    lv_obj_t *list = cos_list_create(view);
    lv_obj_set_style_pad_row(list, 0, 0);

    cos_list_add_placeholder(list, 20);
    lv_obj_t *logo = lv_image_create(list);
    lv_image_set_src(logo, COS_IMG_LOGO);
    cos_list_add_placeholder(list, 30);

    char *device_name = cos_config_get_string(COS_CONFIG_KEY_DEVICE_NAME_STR, COS_CONFIG_DEFAULT_DEVICE_NAME);

    lv_obj_t *title_label = cos_list_add_title(list, cos_lang_get_text(STR_ID_SETTINGS_GENERAL_DEVICE_NAME));
    cos_label_set_font_size(title_label, COS_FONT_SIZE_LARGE);

    lv_obj_t *device_name_label = cos_list_add_comment(list, device_name);
    lv_obj_set_style_text_color(device_name_label, COS_COLOR_WHITE, 0);
    lv_obj_add_flag(device_name_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(device_name_label, _device_name_click_cb, LV_EVENT_CLICKED, NULL);

    if (device_name)
        cos_free(device_name);
    cos_list_add_placeholder(list, 20);

    cos_std_title_comment_create(list, cos_lang_get_text(STR_ID_SETTINGS_GENERAL_COS_VER), CANTOMK6_OS_VERSION_FULL);
    cos_list_add_placeholder(list, 20);

    cos_std_title_comment_create(list,
                                 cos_lang_get_text(STR_ID_SETTINGS_GENERAL_MARKETING_NAME),
                                 CANTOMK6_WATCH_MARKETING_NAME);
    cos_list_add_placeholder(list, 20);

    cos_std_title_comment_create(list,
                                 cos_lang_get_text(STR_ID_SETTINGS_GENERAL_MODEL_NUMBER),
                                 CANTOMK6_WATCH_MODEL_NUMBER);
    cos_list_add_placeholder(list, 20);

    char install_number_str[32];
    snprintf(install_number_str, sizeof(install_number_str), "%" PRIu32, cos_app_get_installed());
    cos_std_title_comment_create(list, cos_lang_get_text(STR_ID_SETTINGS_APPS), install_number_str);
    cos_list_add_placeholder(list, 20);

    cos_std_title_comment_create(list,
                                 cos_lang_get_text(STR_ID_SETTINGS_GENERAL_OPEN_SOURCE),
                                 cos_lang_get_text(STR_ID_SETTINGS_GENERAL_OPEN_SOURCE_CONTENT));

#if LV_USE_QRCODE
    lv_obj_t *qr = lv_qrcode_create(list);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, COS_COLOR_BLACK);
    lv_qrcode_set_light_color(qr, COS_COLOR_WHITE);
    lv_obj_set_style_margin_all(qr, 20, 0);
    const char *repo_data = "https://github.com/CantoMk6/CantoMk6";
    lv_qrcode_update(qr, repo_data, strlen(repo_data));
    lv_obj_set_style_border_color(qr, COS_COLOR_WHITE, 0);
    lv_obj_set_style_border_width(qr, 8, 0);
#endif /* LV_USE_QRCODE */

    cos_std_title_comment_create(list,
                                 cos_lang_get_text(STR_ID_SETTINGS_GENERAL_LEGAL_INFO),
                                 cos_lang_get_text(STR_ID_SETTINGS_GENERAL_LEGAL_INFO_CONTENT));
#if LV_USE_QRCODE
    qr = lv_qrcode_create(list);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, COS_COLOR_BLACK);
    lv_qrcode_set_light_color(qr, COS_COLOR_WHITE);
    lv_obj_set_style_margin_all(qr, 20, 0);
    const char *legal_data = "https://www.apache.org/licenses/LICENSE-2.0";
    lv_qrcode_update(qr, legal_data, strlen(legal_data));
    lv_obj_set_style_border_color(qr, COS_COLOR_WHITE, 0);
    lv_obj_set_style_border_width(qr, 8, 0);
#endif /* LV_USE_QRCODE */

    cos_activity_enter(a);
}

static void _settings_view_general(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    cos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_GENERAL, &view);
    COS_CHECK_PTR_RETURN(a && view);

    lv_obj_t *list = cos_list_create(view);

    lv_obj_t *btn;
    // Device info (system is English-only, no language picker)
    btn = cos_list_add_entry_button_str_id(list, STR_ID_SETTINGS_GENERAL_DEVICE_INFO);
    lv_obj_add_event_cb(btn, _settings_view_device_info, LV_EVENT_CLICKED, NULL);
    cos_activity_enter(a);
}

/************************** System Settings Program Entry **************************/
static const cos_activity_lifecycle_t _settings_lifecycle = {
    .on_enter = NULL,
    .on_destroy = NULL,
};
void cos_settings_enter(void)
{
    cos_activity_t *a = cos_activity_create(&_settings_lifecycle);
    COS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = cos_activity_get_view(a);
    COS_CHECK_PTR_RETURN(view);
    cos_activity_set_title_id(a, STR_ID_SETTINGS);
    cos_activity_set_app_header_visible(a, true);
    cos_app_header_set_clock_visible(false);

    lv_obj_t *settings_list = cos_list_create(view);

    lv_obj_t *btn;
    // Wi-Fi settings
    btn = cos_list_add_round_icon_button_str_id(settings_list, COS_COLOR_BLUE, &cos_icon_wifi, STR_ID_SETTINGS_WIFI);
    lv_obj_add_event_cb(btn, _settings_view_wifi, LV_EVENT_CLICKED, NULL);
    // VPN (WireGuard via microlink) settings
    btn = cos_list_add_round_icon_button(settings_list, COS_COLOR_GREEN, &cos_icon_wireguard, "VPN");
    lv_obj_add_event_cb(btn, _settings_view_vpn, LV_EVENT_CLICKED, NULL);
    // Bluetooth settings
    btn = cos_list_add_round_icon_button_str_id(settings_list,
                                                COS_COLOR_BLUE,
                                                &cos_icon_bluetooth,
                                                STR_ID_SETTINGS_BLUETOOTH);
    lv_obj_add_event_cb(btn, _settings_view_bluetooth, LV_EVENT_CLICKED, NULL);
    // Display settings
    btn = cos_list_add_round_icon_button_str_id(settings_list, COS_COLOR_ORANGE, RI_SUN_FILL, STR_ID_SETTINGS_DISPLAY);
    lv_obj_add_event_cb(btn, _settings_view_display, LV_EVENT_CLICKED, NULL);
    // Notification settings
    btn = cos_list_add_round_icon_button_str_id(settings_list,
                                                COS_COLOR_RED,
                                                RI_NOTIFICATION_2_FILL,
                                                STR_ID_SETTINGS_NOTIFICATION);
    lv_obj_add_event_cb(btn, _settings_view_notification, LV_EVENT_CLICKED, NULL);
    // Password settings (名字统一缩写为 PWD)
    btn = cos_list_add_round_icon_button(settings_list, COS_COLOR_TEAL_BLUE, &cos_icon_pwd, "PWD");
    lv_obj_add_event_cb(btn, _settings_view_password, LV_EVENT_CLICKED, NULL);
    // App list (resources/images/icon/settings.apps.webp)
    btn = cos_list_add_round_icon_button(settings_list,
                                         COS_COLOR_GREY,
                                         &cos_icon_settings_apps,
                                         cos_lang_get_text(STR_ID_SETTINGS_APPS));
    lv_obj_add_event_cb(btn, _settings_view_apps, LV_EVENT_CLICKED, NULL);
    // General settings
    btn = cos_list_add_round_icon_button_str_id(settings_list, COS_COLOR_GREY, RI_TOOLS_FILL, STR_ID_SETTINGS_GENERAL);
    lv_obj_add_event_cb(btn, _settings_view_general, LV_EVENT_CLICKED, NULL);

    cos_activity_enter(a);
}
