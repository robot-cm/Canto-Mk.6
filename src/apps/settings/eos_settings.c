/**
 * @file eos_settings.c
 * @brief Settings page
 */

#include "eos_settings.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EOS_LOG_TAG "Settings"
#include <inttypes.h>
#include "eos_log.h"
#include "lvgl.h"
#include "cJSON.h"
#include "eos_image.h"
#include "eos_msg_list.h"
#include "eos_lang.h"
#include "eos_basic_widgets.h"
#include "eos_event.h"
#include "eos_test.h"
#include "eos_version.h"
#include "eos_port.h"
#include "eos_swipe_panel.h"
#include "eos_app.h"
#include "eos_watchface.h"
#include "eos_theme.h"
#include "eos_pkg_mgr.h"
#include "eos_service_sensor.h"
#include "eos_config.h"
#include "eos_icon.h"
#include "eos_service_display.h"
#include "eos_service_audio.h"
#include "eos_toast.h"
#include "eos_service_storage.h"
#include "eos_service_pm.h"
#include "eos_app_header.h"
#include "eos_radio_page.h"
#include "eos_lang.h"
#include "eos_mem.h"
#include "eos_font.h"
#include "eos_std_widgets.h"
#include "eos_service_haptic.h"
#include "eos_service_display.h"
#include "eos_activity.h"
#include "eos_input_page.h"
#include "eos_net_wifi.h"
#include "eos_net_bt.h"
#include "eos_net_vpn.h"
#include "eos_service_permission.h"
#include "eos_sha256.h"
#include "eos_service_lock.h"
#include "eos_mem.h"
#include "eos_numpad.h"
#include "eos_panel.h"

/* Macros and Definitions -------------------------------------*/
#define _BRIGHTNESS_SMOOTH_DURATION 200
#define _SETTINGS_PASSCODE_MAGIC 0x53504D47U
/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

/************************** General Functions **************************/

void eos_settings_slient_mode_on(void)
{
    eos_service_audio_set_mute(true);
}

void eos_settings_slient_mode_off(void)
{
    eos_service_audio_set_mute(false);
}

/************************** Helper Functions **************************/

lv_obj_t *_auto_get_config_switch_create(lv_obj_t *list, const char *txt, const char *config_key, bool default_val)
{
    lv_obj_t *sw = eos_list_add_switch(list, txt);
    lv_obj_set_state(sw, LV_STATE_CHECKED, eos_config_get_bool(config_key, default_val));
    return sw;
}

static eos_activity_t *_create_activity_with_header(lang_string_id_t id, lv_obj_t **out_view)
{
    eos_activity_t *a = eos_activity_create(NULL);
    EOS_CHECK_PTR_RETURN_VAL(a, NULL);
    lv_obj_t *view = eos_activity_get_view(a);
    EOS_CHECK_PTR_RETURN_VAL(view, NULL);
    eos_activity_set_title_id(a, id);
    eos_activity_set_app_header_visible(a, true);
    if (out_view)
    {
        *out_view = view;
    }
    return a;
}

/**
 * @brief Generic LV_EVENT_DELETE handler that frees user_data (char* allocated with eos_strdup)
 */
static void _free_user_data_on_delete_cb(lv_event_t *e)
{
    void *user_data = lv_event_get_user_data(e);
    if (user_data)
    {
        eos_free(user_data);
    }
}

/************************** Bluetooth **************************/
static void _bluetooth_enable_switch_cb(lv_event_t *e)
{
    lv_obj_t *bt_sw = lv_event_get_target(e);
    EOS_CHECK_PTR_RETURN(bt_sw);
    if (lv_obj_has_state(bt_sw, LV_STATE_CHECKED))
    {
        eos_bluetooth_enable();
        eos_config_set_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, true);
    }
    else
    {
        eos_bluetooth_disable();
        eos_config_set_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, false);
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
static lv_obj_t *_bt_device_row_create(lv_obj_t *parent, const eos_bt_device_t *dev)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_color(row, EOS_THEME_SECONDARY_COLOR, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, EOS_LIST_OBJ_RADIUS, 0);
    lv_obj_set_style_margin_bottom(row, 8, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    /* 左: 蓝色圆底蓝牙图标 */
    lv_obj_t *round = lv_obj_create(row);
    lv_obj_set_size(round, 32, 32);
    lv_obj_set_style_bg_color(round, EOS_COLOR_BLUE, 0);
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
    eos_label_set_font_size(name, EOS_FONT_SIZE_SMALL);
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
    eos_label_set_font_size(info, EOS_FONT_SIZE_SMALL);
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, -12, 0);
    return row;
}

/* 设备行点击回调(定义在下方,此处前置声明供 _bt_scan_fill 使用) */
static void _bt_device_clicked_cb(lv_event_t *e);

/* 扫描结果填入设备容器(show_toast 控制是否提示) */
static void _bt_scan_fill(lv_obj_t *dev_container, bool show_toast)
{
    eos_bt_device_t devs[EOS_NET_BT_SCAN_MAX];
    uint32_t count = 0;
    lv_obj_clean(dev_container);
    if (eos_net_bt_scan(devs, EOS_NET_BT_SCAN_MAX, &count) != EOS_OK || count == 0)
    {
        if (show_toast)
        {
            eos_toast_show(NULL, "No devices found");
        }
        return;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        char *addr_copy = eos_strdup(devs[i].addr);
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
        eos_toast_show(NULL, "Scanned");
    }
}

/* 已配对列表 */
static void _bt_paired_fill(lv_obj_t *pair_container)
{
    eos_bt_device_t devs[EOS_NET_BT_SCAN_MAX];
    uint32_t count = 0;
    lv_obj_clean(pair_container);
    if (eos_net_bt_get_paired(devs, EOS_NET_BT_SCAN_MAX, &count) != EOS_OK || count == 0)
    {
        lv_obj_t *empty = lv_label_create(pair_container);
        lv_label_set_text(empty, "None");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9AA0A6), 0);
        eos_label_set_font_size(empty, EOS_FONT_SIZE_SMALL);
        return;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        lv_obj_t *row = lv_button_create(pair_container);
        lv_obj_set_size(row, lv_pct(100), 44);
        lv_obj_set_style_bg_color(row, EOS_THEME_SECONDARY_COLOR, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, EOS_LIST_OBJ_RADIUS, 0);
        lv_obj_set_style_margin_bottom(row, 6, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_t *lbl = lv_label_create(row);
        char buf[EOS_NET_BT_NAME_MAX + EOS_NET_BT_ADDR_MAX + 2];
        snprintf(buf, sizeof(buf), "%s  %s", devs[i].name, devs[i].addr);
        lv_label_set_text(lbl, buf);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        eos_label_set_font_size(lbl, EOS_FONT_SIZE_SMALL);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
    }
}

/* 扫描按钮 → 同步扫描(服务层 mock 即时返回)并填充 */
static void _bt_scan_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (!eos_net_bt_is_enabled())
    {
        eos_toast_show(NULL, "Enable Bluetooth first");
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
    if (!eos_net_bt_is_enabled())
    {
        eos_toast_show(NULL, "Enable Bluetooth first");
        return;
    }
    eos_result_t r = eos_net_bt_connect(addr);
    if (r == EOS_OK)
    {
        eos_toast_show(NULL, "Paired: OK");
    }
    else
    {
        eos_toast_show(NULL, "Pair failed");
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
    eos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_BLUETOOTH, &view);
    EOS_CHECK_PTR_RETURN(a && view);
    lv_obj_t *list = eos_list_create(view);

    lv_obj_t *bt_sw = _auto_get_config_switch_create(list,
                                                     eos_lang_get_text(STR_ID_SETTINGS_BLUETOOTH_ENABLE),
                                                     EOS_CONFIG_KEY_BLUETOOTH_BOOL,
                                                     false);
    lv_obj_add_event_cb(bt_sw, _bluetooth_enable_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    /* 扫描按钮 + 设备列表 */
    lv_obj_t *scan_btn = eos_list_add_entry_button(list, "Scan devices");
    lv_obj_t *dev_container = eos_list_add_container(list);
    lv_obj_set_layout(dev_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dev_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dev_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(scan_btn, _bt_scan_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* 已配对列表 */
    eos_list_add_title(list, "Paired");
    lv_obj_t *pair_container = eos_list_add_container(list);
    lv_obj_set_layout(pair_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pair_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pair_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    eos_list_add_comment(list, "Saved to /sdcard/history/bt");

    /* 记录容器句柄,页面销毁时清空 */
    s_bt_dev_container = dev_container;
    s_bt_paired_container = pair_container;
    lv_obj_add_event_cb(view, _bt_view_delete_cb, LV_EVENT_DELETE, NULL);

    /* 打开页面时展示已配对设备 */
    _bt_paired_fill(pair_container);

    eos_activity_enter(a);
}

/************************** Wi-Fi Settings **************************/

typedef struct
{
    char ssid[EOS_NET_WIFI_SSID_MAX + 1];
} wifi_connect_ctx_t;

static void _wifi_enable_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    EOS_LOG_D("wifi switch VALUE_CHANGED: target=%p checked=%d", sw, lv_obj_has_state(sw, LV_STATE_CHECKED));
    eos_net_wifi_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* 键盘输密码 → 连接(异步:esp_wifi_connect 阻塞 15s,必须在后台 worker 执行) */
static void _wifi_pwd_input_cb(const char *text, eos_input_result_t result, void *user_data)
{
    wifi_connect_ctx_t *ctx = (wifi_connect_ctx_t *)user_data;
    if (result == EOS_INPUT_RESULT_OK && text && text[0] && ctx)
    {
        EOS_LOG_I("WiFi connect: ssid=%s", ctx->ssid);
        eos_result_t r = eos_net_wifi_connect_async(ctx->ssid, text);
        if (r == EOS_OK)
        {
            eos_toast_show(NULL, "Connecting...");
        }
        else
        {
            eos_toast_show(NULL, "Failed");
        }
    }
    if (ctx)
    {
        eos_free(ctx);
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
    wifi_connect_ctx_t *ctx = (wifi_connect_ctx_t *)eos_malloc(sizeof(*ctx));
    if (!ctx)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->ssid, sizeof(ctx->ssid), "%s", ssid);
    eos_input_page_open_with_callback(NULL, _wifi_pwd_input_cb, ctx);
}

/* AP 行: [WiFi图标][SSID] ─── [🔒][dBm], 64px 紧凑高度便于显示 ≥3 个 AP */
static lv_obj_t *_wifi_ap_row_create(lv_obj_t *parent, const eos_wifi_ap_t *ap, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, lv_pct(100), 64);
    lv_obj_set_style_bg_color(row, EOS_THEME_SECONDARY_COLOR, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, EOS_LIST_OBJ_RADIUS, 0);
    lv_obj_set_style_margin_bottom(row, 8, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    /* 左: 蓝色圆底 WiFi 图标 */
    lv_obj_t *round = lv_obj_create(row);
    lv_obj_set_size(round, 36, 36);
    lv_obj_set_style_bg_color(round, EOS_COLOR_BLUE, 0);
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
    eos_label_set_font_size(ssid, EOS_FONT_SIZE_SMALL);
    lv_obj_align(ssid, LV_ALIGN_LEFT_MID, 62, 0);
    lv_obj_set_width(ssid, 95);

    /* 右: 加密锁(开放网络无锁) + 信号强度 */
    lv_obj_t *info = lv_label_create(row);
    const char *lock = (ap->auth == EOS_WIFI_AUTH_OPEN) ? "" : RI_LOCK_LINE;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d dBm", lock, ap->rssi);
    lv_label_set_text(info, buf);
    lv_obj_set_style_text_color(info, lv_color_hex(0x9AA0A6), 0);
    eos_label_set_font_size(info, EOS_FONT_SIZE_SMALL);
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, -12, 0);

    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, ud);
    return row;
}

/* 从缓存填充 AP 容器(UI 线程调用,不碰 esp_wifi) */
static void _wifi_scan_fill(lv_obj_t *ap_container)
{
    eos_wifi_ap_t aps[EOS_NET_WIFI_SCAN_MAX];
    uint32_t count = 0;
    if (eos_net_wifi_scan_take(aps, EOS_NET_WIFI_SCAN_MAX, &count) != EOS_OK || count == 0)
    {
        eos_toast_show(NULL, "No Wi-Fi found");
        return;
    }

    lv_obj_clean(ap_container);   /* 清旧 AP 行，重建 */
    for (uint32_t i = 0; i < count; i++)
    {
        char *ssid_copy = eos_strdup(aps[i].ssid);
        if (!ssid_copy)
            continue;
        lv_obj_t *row = _wifi_ap_row_create(ap_container, &aps[i], _wifi_ap_clicked_cb, ssid_copy);
        lv_obj_add_event_cb(row, _free_user_data_on_delete_cb, LV_EVENT_DELETE, NULL);
    }
    eos_toast_show(NULL, "Scanned");
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
    if (eos_net_wifi_scan_busy())
    {
        return; /* 继续轮询 */
    }
    lv_timer_del(t);
    lv_obj_t *container = ctx->ap_container;
    eos_free(ctx);
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

    if (!eos_net_wifi_is_enabled())
    {
        eos_toast_show(NULL, "Enable Wi-Fi first");
        return;
    }

    if (eos_net_wifi_scan_async() != EOS_OK)
    {
        eos_toast_show(NULL, eos_net_wifi_scan_busy() ? "Scanning..." : "Scan failed");
        return;
    }

    eos_toast_show(NULL, "Scanning...");
    wifi_scan_poll_ctx_t *ctx = (wifi_scan_poll_ctx_t *)eos_malloc(sizeof(*ctx));
    if (!ctx)
    {
        return;
    }
    ctx->ap_container = ap_container;
    lv_timer_t *t = lv_timer_create(_wifi_scan_poll_timer_cb, 150, ctx);
    if (!t)
    {
        eos_free(ctx);
    }
}

static void _settings_view_wifi(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    eos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_WIFI, &view);
    EOS_CHECK_PTR_RETURN(a && view);
    lv_obj_t *list = eos_list_create(view);

    /* 开关 */
    lv_obj_t *sw = eos_list_add_switch(list, eos_lang_get_text(STR_ID_SETTINGS_WIFI));
    lv_obj_set_state(sw, LV_STATE_CHECKED, eos_net_wifi_is_enabled());
    lv_obj_add_event_cb(sw, _wifi_enable_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    /* 扫描按钮 */
    lv_obj_t *scan_btn = eos_list_add_entry_button(list, eos_lang_get_text(STR_ID_SETTINGS_WIFI_SCAN));
    lv_obj_t *ap_container = eos_list_add_container(list);
    /* AP 行竖排: 容器默认无布局, 多个子对象会重叠在 (0,0) 导致只显示一个 AP */
    lv_obj_set_layout(ap_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ap_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ap_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(scan_btn, _wifi_scan_clicked_cb, LV_EVENT_CLICKED, ap_container);

    /* 当前连接状态 */
    eos_wifi_state_t st = eos_net_wifi_state();
    const char *connected = eos_net_wifi_connected_ssid();
    char status[64];
    if (st == EOS_WIFI_CONNECTED && connected && connected[0])
    {
        snprintf(status, sizeof(status), "%s: %s", eos_lang_get_text(STR_ID_SETTINGS_WIFI_CONNECTED), connected);
    }
    else
    {
        snprintf(status, sizeof(status), "%s", eos_lang_get_text(STR_ID_SETTINGS_WIFI_DISCONNECTED));
    }
    eos_list_add_comment(list, status);
    eos_list_add_comment(list, "Saved: /sdcard/history/wifi");

    eos_activity_enter(a);
}

/************************** VPN (WireGuard via microlink) **************************/

static void _vpn_enable_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (eos_net_vpn_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED)) != EOS_OK)
    {
        eos_toast_show(NULL, "VPN failed to start");
        lv_obj_set_state(sw, LV_STATE_CHECKED, false);
    }
}

/* 输入完成回调：which=0 auth key / 1 device name */
static void _vpn_input_cb(const char *text, eos_input_result_t result, void *user_data)
{
    if (result != EOS_INPUT_RESULT_OK || !text)
    {
        return;
    }
    switch ((int)(intptr_t)user_data)
    {
        case 0: eos_net_vpn_set_auth_key(text); break;
        case 1: eos_net_vpn_set_device_name(text); break;
        default: break;
    }
}

static void _vpn_row_clicked_cb(lv_event_t *e)
{
    eos_input_page_open_with_callback(NULL, _vpn_input_cb, (void *)(intptr_t)lv_event_get_user_data(e));
}

static void _settings_view_vpn(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    eos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_SOCKS5, &view);
    EOS_CHECK_PTR_RETURN(a && view);
    eos_activity_set_title(a, "VPN");
    lv_obj_t *list = eos_list_create(view);

    char *auth_key = eos_net_vpn_get_auth_key();
    char *device_name = eos_net_vpn_get_device_name();

    lv_obj_t *sw = eos_list_add_switch(list, "VPN (WireGuard)");
    lv_obj_set_state(sw, LV_STATE_CHECKED, eos_net_vpn_is_enabled());
    lv_obj_add_event_cb(sw, _vpn_enable_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    eos_list_add_comment(list, "Tailscale/WireGuard via microlink");

    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    char auth_text[64];
    snprintf(auth_text, sizeof(auth_text), "%s: %s", "Auth key",
             (auth_key && auth_key[0]) ? "configured" : "-");
    lv_obj_t *auth_row = eos_list_add_entry_button(list, auth_text);
    lv_obj_add_event_cb(auth_row, _vpn_row_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    char name_text[128];
    snprintf(name_text, sizeof(name_text), "%s: %s", "Device name",
             (device_name && device_name[0]) ? device_name : "-");
    lv_obj_t *name_row = eos_list_add_entry_button(list, name_text);
    lv_obj_add_event_cb(name_row, _vpn_row_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    eos_free(auth_key);
    eos_free(device_name);

    char status[48];
    snprintf(status, sizeof(status), "Status: %s", eos_net_vpn_state_str());
    eos_list_add_comment(list, status);

    eos_activity_enter(a);
}

/************************** Display Settings **************************/

static void _brightness_slider_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    eos_display_set_brightness((uint8_t)lv_slider_get_value(sl), EOS_DISPLAY_DURATION_OFF, false);
}

static void _brightness_slider_released_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(sl);
    eos_display_set_brightness((uint8_t)val, EOS_DISPLAY_DURATION_OFF, false);
    eos_config_set_number(EOS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, val);
}

static void _brightness_slider_minus_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(slider);
    int32_t min = lv_slider_get_min_value(slider);
    int32_t val = lv_slider_get_value(slider);
    if (val == min)
        return;
    val -= 5;
    lv_slider_set_value(slider, val, LV_ANIM_ON);
    eos_config_set_number(EOS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, val);
    eos_display_set_brightness((uint8_t)val, _BRIGHTNESS_SMOOTH_DURATION, false);
}

static void _brightness_slider_plus_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(slider);
    int32_t max = lv_slider_get_max_value(slider);
    int32_t val = lv_slider_get_value(slider);
    if (val == max)
        return;
    val += 5;
    lv_slider_set_value(slider, val, LV_ANIM_ON);
    eos_config_set_number(EOS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, val);
    eos_display_set_brightness((uint8_t)val, _BRIGHTNESS_SMOOTH_DURATION, false);
}

static void _aod_mode_switch_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    EOS_CHECK_PTR_RETURN(obj);
    if (lv_obj_has_state(obj, LV_STATE_CHECKED))
    {
        eos_pm_set_aod_mode(true);
    }
    else
    {
        eos_pm_set_aod_mode(false);
    }
}

/* 熄屏时间选项: 5s / 15s / 30s / 45s / 60s / 300s */
static const uint32_t _sleep_timeout_options[] = {5, 15, 30, 45, 60, 300};
#define _SLEEP_TIMEOUT_OPTIONS_COUNT (sizeof(_sleep_timeout_options) / sizeof(_sleep_timeout_options[0]))

static void _wake_duration_radio_list_selection_changed_cb(lv_event_t *e)
{
    uint32_t index = (uint32_t)lv_event_get_param(e);
    if (index >= _SLEEP_TIMEOUT_OPTIONS_COUNT)
    {
        return;
    }
    uint32_t wake_duration = _sleep_timeout_options[index];
    eos_pm_set_sleep_timeout(wake_duration);
    eos_config_set_number(EOS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, wake_duration);
}

static void _wake_duration_entry_button_clicked_cb(lv_event_t *e)
{
    eos_radio_page_t *rp = eos_radio_page_create("Screen timeout");
    char str[32];
    uint32_t item_indexes[_SLEEP_TIMEOUT_OPTIONS_COUNT];
    for (uint32_t i = 0; i < _SLEEP_TIMEOUT_OPTIONS_COUNT; i++)
    {
        snprintf(str, sizeof(str), "%us", (unsigned int)_sleep_timeout_options[i]);
        item_indexes[i] = eos_radio_page_add_item(rp, str);
    }
    eos_radio_page_add_event_cb(rp, _wake_duration_radio_list_selection_changed_cb, NULL);
    eos_radio_page_set_comment(rp, "Auto off after no touch");
    uint32_t timeout = eos_config_get_number(EOS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, 15);
    uint32_t checked_index = 1; /* default 15s */
    for (uint32_t i = 0; i < _SLEEP_TIMEOUT_OPTIONS_COUNT; i++)
    {
        if (_sleep_timeout_options[i] == timeout)
        {
            checked_index = i;
            break;
        }
    }
    eos_radio_page_check(rp, item_indexes[checked_index]);
    eos_radio_page_show(rp);
}

static void _settings_view_display(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    eos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_DISPLAY, &view);
    EOS_CHECK_PTR_RETURN(a && view);

    lv_obj_t *list = eos_list_create(view);

    eos_list_slider_t *ls = eos_list_add_slider(list, eos_lang_get_text(STR_ID_SETTINGS_DISPLAY_BRIGHTNESS));
    lv_label_set_text(ls->minus_label, RI_SUN_LINE);
    lv_label_set_text(ls->plus_label, RI_SUN_FILL);
    eos_list_slider_set_minus_label_scale(ls, 200);
    lv_slider_set_value(ls->slider, eos_config_get_number(EOS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, 50), LV_ANIM_ON);
    lv_slider_set_range(ls->slider, EOS_DISPLAY_BRIGHTNESS_MIN, EOS_DISPLAY_BRIGHTNESS_MAX);
    lv_obj_add_event_cb(ls->slider, _brightness_slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ls->slider, _brightness_slider_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(ls->minus_btn, _brightness_slider_minus_cb, LV_EVENT_CLICKED, ls->slider);
    lv_obj_add_event_cb(ls->plus_btn, _brightness_slider_plus_cb, LV_EVENT_CLICKED, ls->slider);

    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    lv_obj_t *sw = _auto_get_config_switch_create(list,
                                                  eos_lang_get_text(STR_ID_SETTINGS_DISPLAY_AOD),
                                                  EOS_CONFIG_KEY_AOD_MODE_BOOL,
                                                  false);
    lv_obj_add_event_cb(sw, _aod_mode_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    eos_list_add_comment(list, eos_lang_get_text(STR_ID_SETTINGS_DISPLAY_AOD_COMMENT));
    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    eos_list_add_title(list, "Screen");

    lv_obj_t *wd_btn = eos_list_add_entry_button(list, "Screen timeout");
    lv_obj_add_event_cb(wd_btn, _wake_duration_entry_button_clicked_cb, LV_EVENT_CLICKED, NULL);
    eos_activity_enter(a);
}
/************************** Notification **************************/
static void _settings_view_notification(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    eos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_NOTIFICATION, &view);
    EOS_CHECK_PTR_RETURN(a && view);
    lv_obj_t *list = eos_list_create(view);
    LV_UNUSED(list);
    // TODO: Notification settings
    eos_activity_enter(a);
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
    EOS_CHECK_PTR_RETURN(ctx);
    eos_app_uninstall(ctx->app_id);
    eos_free((void *)ctx->app_name);
    eos_free(ctx);
    eos_activity_back();
}

/**
 * @brief Uninstall button callback - shows confirmation panel
 */
static void _uninstall_btn_cb(lv_event_t *e)
{
    _uninstall_ctx_t *ctx = (_uninstall_ctx_t *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ctx);

    eos_activity_t *current = eos_activity_get_current();
    EOS_CHECK_PTR_RETURN(current);

    snprintf(ctx->title,
             sizeof(ctx->title),
             "%s \"%s\"",
             eos_lang_get_text(STR_ID_SETTINGS_APPS_UNINSTALL),
             ctx->app_name ? ctx->app_name : ctx->app_id);

    eos_panel_cfg_t cfg = {
        .icon_bg_color = EOS_THEME_DANGEROS_COLOR,
        .icon_type = EOS_PANEL_ICON_TYPE_SYMBOL,
        .icon_src = RI_ALERT_FILL,
        .title_text = ctx->title,
        .message_id = STR_ID_SETTINGS_APPS_UNINSTALL_MSG,
        .confirm_btn_id = STR_ID_SETTINGS_APPS_UNINSTALL,
        .confirm_cb = NULL,
        .cancel_btn_id = STR_ID_CANCEL,
        .cancel_cb = NULL,
    };

    eos_panel_t *panel = eos_panel_create_on_activity(current, &cfg);
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
    EOS_CHECK_PTR_RETURN(ctx);

    char data_path[EOS_FS_PATH_MAX];
    snprintf(data_path, sizeof(data_path), EOS_APP_DATA_DIR "%s", ctx->app_id);
    if (eos_storage_rm_recursive(data_path) != EOS_OK)
    {
        EOS_LOG_E("Remove data failed");
        eos_free(ctx);
        return;
    }
    if (ctx->orig_btn)
    {
        lv_obj_add_state(ctx->orig_btn, LV_STATE_DISABLED);
    }
    eos_toast_show_char_icon(RI_CHECKBOX_CIRCLE_FILL,
                             EOS_COLOR_GREEN,
                             eos_lang_get_text(STR_ID_SETTINGS_APPS_CLEAR_DATA_SUCCESS));
    eos_free(ctx);
}

/**
 * @brief Clear data button callback - shows confirmation panel
 */
static void _clear_data_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *app_id = (const char *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(app_id);

    eos_activity_t *current = eos_activity_get_current();
    EOS_CHECK_PTR_RETURN(current);

    typedef struct
    {
        const char *app_id;
        lv_obj_t *orig_btn;
    } _clear_data_ctx_t;
    _clear_data_ctx_t *ctx = eos_malloc(sizeof(_clear_data_ctx_t));
    EOS_CHECK_PTR_RETURN(ctx);
    ctx->app_id = app_id;
    ctx->orig_btn = btn;

    eos_panel_cfg_t cfg = {
        .icon_bg_color = EOS_THEME_DANGEROS_COLOR,
        .icon_type = EOS_PANEL_ICON_TYPE_SYMBOL,
        .icon_src = RI_ALERT_FILL,
        .title_id = STR_ID_SETTINGS_APPS_CLEAR_DATA,
        .message_text = app_id,
        .confirm_btn_id = STR_ID_SETTINGS_APPS_CLEAR_DATA,
        .confirm_cb = NULL,
        .cancel_btn_id = STR_ID_CANCEL,
        .cancel_cb = NULL,
    };

    eos_panel_t *panel = eos_panel_create_on_activity(current, &cfg);
    if (panel)
    {
        lv_obj_add_event_cb(panel->confirm_btn, _clear_data_confirm_cb, LV_EVENT_CLICKED, ctx);
    }
}

/* ---- Forward declarations for permission radio page ---- */
typedef struct
{
    const char *app_id;
    eos_perm_category_t category;
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
    EOS_CHECK_PTR_RETURN(app_id);

    /* Read manifest to get permissions list */
    char manifest_path[EOS_FS_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_MANIFEST_FILE_NAME, app_id);
    script_pkg_t pkg = {0};
    if (script_engine_get_manifest(manifest_path, &pkg) != EOS_OK)
    {
        EOS_LOG_E("Read manifest failed: %s", manifest_path);
        return;
    }

    eos_activity_t *a = eos_activity_create(NULL);
    EOS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = eos_activity_get_view(a);
    EOS_CHECK_PTR_RETURN(view);

    eos_activity_set_title_id(a, STR_ID_PERM_TITLE);
    eos_activity_set_app_header_visible(a, true);

    lv_obj_t *list = eos_list_create(view);

    if (pkg.permission_count == 0)
    {
        eos_list_add_comment(list, eos_lang_get_text(STR_ID_PERM_NO_PERMISSIONS));
    }
    else
    {
        for (uint8_t i = 0; i < pkg.permission_count; i++)
        {
            if (!pkg.permissions[i])
                continue;

            eos_perm_category_t cat = eos_permission_name_to_category(pkg.permissions[i]);
            if (cat >= EOS_PERM_CATEGORY_COUNT)
                continue;

            const char *perm_display_name = eos_permission_category_name(cat);
            eos_perm_state_t current_state = eos_permission_get(app_id, cat);
            const char *state_label = eos_lang_get_text(eos_permission_state_label_id(current_state));

            /* Create entry button: top=name(white), bottom=status(grey) */
            lv_obj_t *perm_btn = eos_list_add_entry_button(list, perm_display_name ? perm_display_name : "");

            /* Switch to vertical two-line layout */
            lv_obj_set_flex_flow(perm_btn, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(perm_btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

            /* Permission name: white text (top line), left-aligned */
            lv_obj_t *name_label = lv_obj_get_child(perm_btn, 0);
            if (name_label)
            {
                lv_obj_set_style_text_color(name_label, EOS_COLOR_WHITE, 0);
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
                lv_obj_set_style_text_color(state_label_obj, EOS_COLOR_GREY, 0);
                lv_obj_set_style_text_align(state_label_obj, LV_TEXT_ALIGN_LEFT, 0);
                lv_obj_set_width(state_label_obj, lv_pct(100));
            }

            /* Allocate context for radio page callback */
            _perm_radio_ctx_t *radio_ctx = eos_malloc(sizeof(_perm_radio_ctx_t));
            if (radio_ctx)
            {
                radio_ctx->app_id = app_id;
                radio_ctx->category = cat;
                radio_ctx->state_label = state_label_obj;
                lv_obj_add_event_cb(perm_btn, _perm_row_click_cb, LV_EVENT_CLICKED, radio_ctx);
            }
        }
    }

    eos_activity_enter(a);
    eos_pkg_free(&pkg);
}

/**
 * @brief Permission state radio selection callback
 */
static void _perm_state_radio_cb(lv_event_t *e)
{
    _perm_radio_ctx_t *ctx = (_perm_radio_ctx_t *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ctx);

    uint32_t selected = (uint32_t)(uintptr_t)lv_event_get_param(e);
    if (selected <= EOS_PERM_STATE_ALLOW_ALWAYS)
    {
        eos_permission_set(ctx->app_id, ctx->category, (eos_perm_state_t)selected);
        EOS_LOG_D("Permission state changed: app=%s cat=%d state=%lu", ctx->app_id, ctx->category, selected);

        /* Refresh status label text on the list page */
        if (ctx->state_label && lv_obj_is_valid(ctx->state_label))
        {
            const char *new_state_label = eos_lang_get_text(eos_permission_state_label_id((eos_perm_state_t)selected));
            lv_label_set_text(ctx->state_label, new_state_label ? new_state_label : "");
        }

        eos_activity_back();
    }
}

/**
 * @brief Permission row click callback - opens radio page for state selection
 */
static void _perm_row_click_cb(lv_event_t *e)
{
    _perm_radio_ctx_t *ctx = (_perm_radio_ctx_t *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(ctx);

    eos_perm_state_t current_state = eos_permission_get(ctx->app_id, ctx->category);
    const char *perm_name = eos_permission_category_name(ctx->category);

    eos_radio_page_t *rp = eos_radio_page_create(perm_name ? perm_name : "");
    eos_radio_page_add_item(rp, eos_lang_get_text(STR_ID_PERM_STATE_DENIED));
    eos_radio_page_add_item(rp, eos_lang_get_text(STR_ID_PERM_ALLOW_ONCE));
    eos_radio_page_add_item(rp, eos_lang_get_text(STR_ID_PERM_ALLOW_FOREGROUND));
    eos_radio_page_add_item(rp, eos_lang_get_text(STR_ID_PERM_STATE_ALLOW_ALWAYS));
    eos_radio_page_add_event_cb(rp, _perm_state_radio_cb, ctx);
    eos_radio_page_check(rp, (uint32_t)current_state);
    eos_radio_page_show(rp);
}

/**
 * @brief App list callback, opens app details
 * @param e
 */
static void _settings_app_list_btn_cb(lv_event_t *e)
{
    const char *app_id = (const char *)lv_event_get_user_data(e);
    EOS_CHECK_PTR_RETURN(app_id);

    // Get app manifest
    char manifest_path[EOS_FS_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_MANIFEST_FILE_NAME, app_id);
    script_pkg_t pkg = {0};
    if (script_engine_get_manifest(manifest_path, &pkg) != EOS_OK)
    {
        EOS_LOG_E("Read manifest failed: %s", manifest_path);
        return;
    }
    EOS_LOG_D("App Info:\n"
              "id=%s | name=%s | version=%s |\n"
              "author:%s | description:%s",
              pkg.id,
              pkg.name,
              pkg.version,
              pkg.version,
              pkg.description);
    char script_path[EOS_FS_PATH_MAX];
    snprintf(script_path, sizeof(script_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_SCRIPT_ENTRY_FILE_NAME, app_id);
    if (!eos_storage_is_file(script_path))
    {
        EOS_LOG_E("Can't find script: %s", script_path);
        eos_pkg_free(&pkg);
        return;
    }

    // Create new activity for app details
    eos_activity_t *a = eos_activity_create(NULL);
    EOS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = eos_activity_get_view(a);
    EOS_CHECK_PTR_RETURN(view);
    eos_activity_set_title(a, pkg.name);
    eos_activity_set_app_header_visible(a, true);

    lv_obj_t *list = eos_list_create(view);

    lv_obj_t *container = eos_list_add_container(list);
    lv_obj_set_size(container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    char icon_path[EOS_FS_PATH_MAX];
    snprintf(icon_path, sizeof(icon_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_ICON_FILE_NAME, app_id);
    if (!eos_storage_is_file(icon_path))
    {
        memcpy(icon_path, EOS_IMG_APP, sizeof(EOS_IMG_APP));
    }
    eos_row_create(container, NULL, pkg.name, icon_path, 100, 100);

    eos_row_create(container, eos_lang_get_text(STR_ID_SETTINGS_APPS_APPID), app_id, NULL, 0, 0);
    eos_row_create(container, eos_lang_get_text(STR_ID_SETTINGS_APPS_AUTHOR), pkg.author, NULL, 0, 0);
    eos_row_create(container, eos_lang_get_text(STR_ID_SETTINGS_APPS_VERSION), pkg.version, NULL, 0, 0);

    char sdk_buf[32];
    snprintf(sdk_buf, sizeof(sdk_buf), "Min: %d, Target: %d", pkg.min_api_level, pkg.target_api_level);
    eos_row_create(container, eos_lang_get_text(STR_ID_SETTINGS_APPS_SDK), sdk_buf, NULL, 0, 0);

    if (strcmp(pkg.description, "") != 0)
    {
        lv_obj_t *inner_container =
            eos_list_add_title_container(list, eos_lang_get_text(STR_ID_SETTINGS_APPS_DESCRIPTON));
        lv_obj_set_size(inner_container, lv_pct(100), LV_SIZE_CONTENT);

        lv_obj_t *desc_label = lv_label_create(inner_container);
        lv_label_set_text(desc_label, pkg.description);
        lv_obj_set_width(desc_label, lv_pct(100));
        lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    }

    /* ---- Permission Entry ---- */
    if (pkg.permission_count > 0)
    {
        eos_list_add_title(list, eos_lang_get_text(STR_ID_PERM_TITLE));
        lv_obj_t *perm_entry_btn = eos_list_add_button(list, NULL, eos_lang_get_text(STR_ID_PERM_TITLE));
        /* Store app_id for the callback - must persist until activity is destroyed */
        char *stored_app_id = eos_strdup(app_id);
        lv_obj_add_event_cb(perm_entry_btn, _settings_perm_entry_clicked_cb, LV_EVENT_CLICKED, stored_app_id);
        /* Free stored_app_id when button is deleted */
        lv_obj_add_event_cb(perm_entry_btn, _free_user_data_on_delete_cb, LV_EVENT_DELETE, stored_app_id);
    }

    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    lv_obj_t *clear_data_btn = eos_button_create_ex(list,
                                                    EOS_THEME_SECONDARY_COLOR,
                                                    eos_lang_get_text(STR_ID_SETTINGS_APPS_CLEAR_DATA),
                                                    EOS_THEME_DANGEROS_COLOR,
                                                    _clear_data_btn_cb,
                                                    (void *)(uintptr_t)app_id);

    eos_list_add_placeholder(list, 20);

    char data_path[EOS_FS_PATH_MAX];
    snprintf(data_path, sizeof(data_path), EOS_APP_DATA_DIR "%s", app_id);
    if (!eos_storage_is_dir(data_path))
    {
        lv_obj_add_state(clear_data_btn, LV_STATE_DISABLED);
    }

    _uninstall_ctx_t *uninstall_ctx = eos_malloc(sizeof(_uninstall_ctx_t));
    if (uninstall_ctx)
    {
        uninstall_ctx->app_id = app_id;
        uninstall_ctx->app_name = eos_strdup(pkg.name);
    }

    lv_obj_t *uninstall_btn = eos_button_create_ex(list,
                                                   EOS_THEME_SECONDARY_COLOR,
                                                   eos_lang_get_text(STR_ID_SETTINGS_APPS_UNINSTALL),
                                                   EOS_THEME_DANGEROS_COLOR,
                                                   _uninstall_btn_cb,
                                                   uninstall_ctx);

    (void)uninstall_btn;
    eos_activity_enter(a);
    eos_pkg_free(&pkg);
}

static void _app_btn_create(lv_obj_t *parent, const char *app_id)
{
    char icon_path[EOS_FS_PATH_MAX];
    snprintf(icon_path, sizeof(icon_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_ICON_FILE_NAME, app_id);
    if (!eos_storage_is_file(icon_path))
    {
        memcpy(icon_path, EOS_IMG_APP, sizeof(EOS_IMG_APP));
    }
    EOS_LOG_D("Icon: %s", icon_path);

    // Get app manifest
    char manifest_path[EOS_FS_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), EOS_APP_INSTALLED_DIR "%s/" EOS_APP_MANIFEST_FILE_NAME, app_id);
    script_pkg_t pkg = {0};
    if (script_engine_get_manifest(manifest_path, &pkg) != EOS_OK)
    {
        EOS_LOG_E("Read manifest failed: %s", manifest_path);
        return;
    }

    EOS_LOG_I("name = %s\n", pkg.name);

    lv_obj_t *btn = eos_list_add_button(parent, icon_path, pkg.name);
    lv_obj_add_event_cb(btn, _settings_app_list_btn_cb, LV_EVENT_CLICKED, (void *)app_id);
    eos_app_obj_auto_delete(btn, app_id);
    eos_pkg_free(&pkg);
}

static void _app_installed_cb(eos_event_t *e)
{
    lv_obj_t *parent = eos_event_get_user_data(e);
    const char *installed_app_id = eos_event_get_param(e);
    EOS_CHECK_PTR_RETURN(parent && installed_app_id);
    _app_btn_create(parent, installed_app_id);
}

/**
 * @brief App list in system settings
 */
static void _settings_view_apps_on_destroy(eos_activity_t *a)
{
    lv_obj_t *view = eos_activity_get_view(a);
    if (view)
    {
        lv_obj_t *app_list = lv_obj_get_child(view, 0);
        if (app_list)
        {
            eos_event_unsubscribe_with_obj(EOS_EVENT_APP_INSTALLED, _app_installed_cb, app_list);
        }
    }
}

static void _settings_view_apps(lv_event_t *e)
{
    // Create a new activity to draw the app list
    static const eos_activity_lifecycle_t lifecycle = {.on_destroy = _settings_view_apps_on_destroy,
                                                       .on_enter = NULL,
                                                       .on_pause = NULL,
                                                       .on_resume = NULL};

    eos_activity_t *a = eos_activity_create(&lifecycle);
    EOS_CHECK_PTR_RETURN(a);

    lv_obj_t *view = eos_activity_get_view(a);
    EOS_CHECK_PTR_RETURN(view);

    eos_activity_set_title_id(a, STR_ID_SETTINGS_APPS);
    eos_activity_set_app_header_visible(a, true);

    lv_obj_t *app_list = eos_list_create(view);
    eos_event_subscribe_ex(EOS_EVENT_APP_INSTALLED, _app_installed_cb, NULL, app_list);

    size_t app_list_size = eos_app_get_installed();
    for (size_t i = 0; i < app_list_size; i++)
    {
        _app_btn_create(app_list, eos_app_list_get_id(i));
    }

    eos_activity_enter(a);
}

/************************** Password Settings **************************/

typedef struct
{
    uint32_t magic;
    eos_activity_t *activity;
    lv_obj_t *title_label;
    eos_numpad_t *numpad;
} _settings_numpad_ctx_t;

typedef struct _password_flow_state_t
{
    char step1_digits[EOS_NUMPAD_MAX_DIGITS + 1];
    uint8_t target_length;
    bool is_changing; /* true = change flow, false = create flow */
    uint8_t old_attempts;
    _settings_numpad_ctx_t *numpad_ctx; /* For reusing the same page */
} _password_flow_state_t;

/* References to password sub-page widgets that need dynamic updates */
typedef struct _password_subpage_refs_t
{
    lv_obj_t *enable_sw;
    lv_obj_t *change_btn;
    lv_obj_t *simple_sw;
    lv_obj_t *list;
} _password_subpage_refs_t;

static void _password_subpage_refresh(_password_subpage_refs_t *refs);

/* Forward declarations for password numpad */
static void _numpad_async_back_cb(void *user_data);
static void _password_create_step1_cb(const char *digits, void *user_data);
static void _password_flow_cancel_step1_cb(void *user_data);
static void _password_enable_sw_cb(lv_event_t *e);
static void _password_simple_sw_cb(lv_event_t *e);
static void _password_change_clicked_cb(lv_event_t *e);
static void _start_password_creation(uint8_t target_length);
static void _start_creation_async_cb(void *user_data);
static void _settings_view_password(lv_event_t *e);

/* ---- Password Numpad Helper ---- */

static void _numpad_async_back_cb(void *user_data)
{
    (void)user_data;
    /* Reset input devices to avoid accessing destroyed widgets */
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL)
    {
        lv_indev_reset(indev, NULL);
    }
    eos_activity_back();
}

static void _settings_numpad_on_destroy(eos_activity_t *activity)
{
    _settings_numpad_ctx_t *ctx = (_settings_numpad_ctx_t *)eos_activity_get_user_data(activity);
    if (ctx)
    {
        eos_numpad_delete(ctx->numpad);
        eos_free(ctx);
        eos_activity_set_user_data(activity, NULL);
    }
}

/**
 * @brief Show a password numpad page.
 * @param title_str Title text (e.g., "Enter New Passcode")
 * @param target_length 4 or 6 digits
 * @param on_complete Called when all digits are entered
 * @param on_cancel Called when user presses cancel (NULL = just go back)
 * @param user_data User data passed to callbacks
 */
static void _show_password_numpad(const char *title_str,
                                  uint8_t target_length,
                                  void (*on_complete)(const char *digits, void *user_data),
                                  void (*on_cancel)(void *user_data),
                                  void *user_data)
{
    static const eos_activity_lifecycle_t lifecycle = {
        .on_destroy = _settings_numpad_on_destroy,
    };

    eos_activity_t *activity = eos_activity_create(&lifecycle);
    if (!activity)
        return;

    eos_activity_set_type(activity, EOS_ACTIVITY_TYPE_APP);
    eos_activity_set_app_header_visible(activity, false);

    _settings_numpad_ctx_t *ctx = (_settings_numpad_ctx_t *)eos_malloc_zeroed(sizeof(_settings_numpad_ctx_t));
    if (!ctx)
    {
        eos_activity_back();
        return;
    }

    ctx->magic = _SETTINGS_PASSCODE_MAGIC;
    ctx->activity = activity;
    eos_activity_set_user_data(activity, ctx);

    /* Connect flow state back to the numpad ctx for page reuse */
    if (user_data)
    {
        ((_password_flow_state_t *)user_data)->numpad_ctx = ctx;
    }

    /* Build UI */
    lv_obj_t *view = eos_activity_get_view(activity);
    if (!view)
    {
        eos_free(ctx);
        eos_activity_back();
        return;
    }

    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(view, lv_color_black(), 0);
    lv_obj_set_style_pad_all(view, 0, 0);
    lv_obj_set_style_border_width(view, 0, 0);
    lv_obj_add_flag(view, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* Flex container for main content */
    lv_obj_t *container = lv_obj_create(view);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Title */
    ctx->title_label = lv_label_create(container);
    lv_label_set_text(ctx->title_label, title_str);
    lv_obj_set_style_text_color(ctx->title_label, EOS_COLOR_WHITE, 0);

    ctx->numpad = eos_numpad_create(container, target_length, true, on_complete, on_cancel, user_data);

    eos_activity_enter(activity);
}

/* ---- Password Settings Page ---- */

/**
 * @brief Save password hash to config
 */
static void _save_password_hash(const char *digits)
{
    uint8_t hash[EOS_SHA256_DIGEST_SIZE];
    eos_sha256((const uint8_t *)digits, strlen(digits), hash);

    char hex[EOS_SHA256_HEX_STR_SIZE];
    eos_sha256_to_hex(hash, hex, sizeof(hex));

    eos_config_set_string(EOS_CONFIG_KEY_PASSWORD_HASH_STR, hex);
    eos_config_set_bool(EOS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, true);
    EOS_LOG_I("Password saved");
}

/**
 * @brief Callback for password creation step 2 (re-enter)
 */
static void _password_create_step2_cb(const char *digits, void *user_data);

/* Async callback data for deferred navigation on success */
typedef struct
{
    _password_flow_state_t *state;
    int back_count;
} _password_finish_data_t;

static void _password_finish_async_cb(void *user_data)
{
    _password_finish_data_t *data = (_password_finish_data_t *)user_data;
    if (!data)
        return;
    if (data->state)
        eos_free(data->state);

    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL)
    {
        lv_indev_reset(indev, NULL);
    }

    for (int i = 0; i < data->back_count; i++)
    {
        eos_activity_back();
    }
    eos_free(data);
}

/* ---- Numpad Page Reconfiguration for Step 2 ---- */

static void _settings_numpad_reconfigure(_settings_numpad_ctx_t *ctx,
                                         const char *new_title,
                                         void (*new_on_complete)(const char *, void *),
                                         void (*new_on_cancel)(void *),
                                         void *new_user_data)
{
    if (!ctx || !ctx->numpad)
        return;

    /* Change title text */
    if (ctx->title_label && lv_obj_is_valid(ctx->title_label))
    {
        lv_label_set_text(ctx->title_label, new_title);
    }

    /* Clear dots */
    eos_numpad_clear(ctx->numpad);

    /* Update callbacks and user data */
    ctx->numpad->on_complete = new_on_complete;
    ctx->numpad->on_cancel = new_on_cancel;
    ctx->numpad->user_data = new_user_data;
}

/* ---- Flow Callbacks ---- */

static void _password_flow_cancel_step1_cb(void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (state)
        eos_free(state);
    lv_async_call(_numpad_async_back_cb, NULL);
}

static void _password_flow_cancel_step2_cb(void *user_data)
{
    /* Go back to step1 (reconfigure current page back to step1) */
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (!state || !state->numpad_ctx)
    {
        lv_async_call(_numpad_async_back_cb, NULL);
        return;
    }
    state->step1_digits[0] = '\0';
    const char *title = eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_ENTER_NEW);
    _settings_numpad_reconfigure(state->numpad_ctx,
                                 title,
                                 _password_create_step1_cb,
                                 _password_flow_cancel_step1_cb,
                                 state);
}

static void _password_create_step1_cb(const char *digits, void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (!state || !state->numpad_ctx)
        return;

    /* Save step 1 digits */
    strncpy(state->step1_digits, digits, EOS_NUMPAD_MAX_DIGITS);
    state->step1_digits[EOS_NUMPAD_MAX_DIGITS] = '\0';

    /* Reuse current page for step 2: just change title and clear dots */
    const char *title = eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_REENTER);
    _settings_numpad_reconfigure(state->numpad_ctx,
                                 title,
                                 _password_create_step2_cb,
                                 _password_flow_cancel_step2_cb,
                                 state);
}

static void _password_create_step2_cb(const char *digits, void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (!state)
        return;

    if (strcmp(digits, state->step1_digits) == 0)
    {
        /* Match! Save password, pop back to password sub-page */
        _save_password_hash(digits);
        eos_toast_show_char_icon(RI_CHECKBOX_CIRCLE_FILL, EOS_COLOR_GREEN, "Password enabled");
        _password_finish_data_t *data = (_password_finish_data_t *)eos_malloc(sizeof(_password_finish_data_t));
        if (data)
        {
            data->state = state;
            /* Creation: pop step1 (current reused page) = 1 level → sub-page
             * Change:   pop step1 + verify-old = 2 levels → sub-page */
            data->back_count = state->is_changing ? 2 : 1;
            lv_async_call(_password_finish_async_cb, data);
        }
        else
        {
            eos_free(state);
        }
    }
    else
    {
        /* Mismatch: clear digits, show toast, go back to step1 on same page */
        eos_toast_show_char_icon(RI_ERROR_WARNING_FILL,
                                 EOS_COLOR_RED,
                                 eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_MISMATCH));
        state->step1_digits[0] = '\0';
        const char *title = eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_ENTER_NEW);
        _settings_numpad_reconfigure(state->numpad_ctx,
                                     title,
                                     _password_create_step1_cb,
                                     _password_flow_cancel_step1_cb,
                                     state);
    }
}

/* Mirrors eos_list_transition_state_t from eos_basic_widgets.c for direct access */
typedef struct
{
    lv_obj_t *list;
    lv_obj_t *button;
    eos_activity_t *activity;
    uint32_t sequence;
} _local_transition_state_t;

/**
 * @brief Ensure list transition state is set so the animation has a target.
 */
static void _ensure_list_transition_state(eos_activity_t *subpage_activity)
{
    if (!subpage_activity)
        return;

    lv_obj_t *view = eos_activity_get_view(subpage_activity);
    if (!view || !lv_obj_is_valid(view))
        return;

    /* Find the list in the sub-page view tree */
    lv_obj_t *list = NULL;
    uint32_t child_cnt = lv_obj_get_child_count(view);
    for (uint32_t i = 0; i < child_cnt; i++)
    {
        lv_obj_t *child = lv_obj_get_child(view, i);
        if (child && lv_obj_check_type(child, &lv_list_class))
        {
            list = child;
            break;
        }
    }
    if (!list)
        return;

    /* Find the switch container (has USER_1) and set it as transition target */
    uint32_t list_child_cnt = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < list_child_cnt; i++)
    {
        lv_obj_t *item = lv_obj_get_child(list, i);
        if (item && lv_obj_has_flag(item, LV_OBJ_FLAG_USER_1))
        {
            _local_transition_state_t *state = (_local_transition_state_t *)lv_obj_get_user_data(list);
            if (state && state->list == list)
            {
                state->button = item;
                state->activity = subpage_activity;
                state->sequence++; /* Ensure non-zero so select picks it up */
            }
            return;
        }
    }
}

static void _start_creation_async_cb(void *user_data)
{
    uint8_t target_length = (uint8_t)(uintptr_t)user_data;
    /* Ensure list transition state is recorded before creating activity */
    _ensure_list_transition_state(eos_activity_get_current());
    _start_password_creation(target_length);
}

static void _start_password_creation(uint8_t target_length)
{
    _password_flow_state_t *state = (_password_flow_state_t *)eos_malloc_zeroed(sizeof(_password_flow_state_t));
    if (!state)
        return;
    state->target_length = target_length;
    const char *title = eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_ENTER_NEW);
    _show_password_numpad(title, target_length, _password_create_step1_cb, _password_flow_cancel_step1_cb, state);
}

/* ---- Password Change Flow ---- */

static void _password_change_cancel_cb(void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (state)
        eos_free(state);
    lv_async_call(_numpad_async_back_cb, NULL);
}

static void _password_change_new_step1_cb(const char *digits, void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (!state || !state->numpad_ctx)
        return;

    strncpy(state->step1_digits, digits, EOS_NUMPAD_MAX_DIGITS);
    state->step1_digits[EOS_NUMPAD_MAX_DIGITS] = '\0';

    /* Reuse current page for re-enter */
    const char *title = eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_REENTER);
    _settings_numpad_reconfigure(state->numpad_ctx,
                                 title,
                                 _password_create_step2_cb,
                                 _password_flow_cancel_step2_cb,
                                 state);
}

static void _password_change_verify_old_cb(const char *digits, void *user_data)
{
    _password_flow_state_t *state = (_password_flow_state_t *)user_data;
    if (!state)
        return;

    uint8_t hash[EOS_SHA256_DIGEST_SIZE];
    eos_sha256((const uint8_t *)digits, strlen(digits), hash);
    char entered_hex[EOS_SHA256_HEX_STR_SIZE];
    eos_sha256_to_hex(hash, entered_hex, sizeof(entered_hex));

    char *stored_hash = eos_config_get_string(EOS_CONFIG_KEY_PASSWORD_HASH_STR, "");
    bool match = (stored_hash && strcmp(entered_hex, stored_hash) == 0);
    eos_free(stored_hash);

    if (match)
    {
        /* Reuse current page for new password entry */
        state->step1_digits[0] = '\0';
        const char *title = eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_ENTER_NEW);
        _settings_numpad_reconfigure(state->numpad_ctx,
                                     title,
                                     _password_change_new_step1_cb,
                                     _password_change_cancel_cb,
                                     state);
    }
    else
    {
        state->old_attempts++;
        eos_toast_show_char_icon(RI_ERROR_WARNING_FILL,
                                 EOS_COLOR_RED,
                                 eos_lang_get_text(STR_ID_LOCK_SCREEN_WRONG_PASSCODE));
        if (state->old_attempts >= 3)
        {
            eos_free(state);
        }
        lv_async_call(_numpad_async_back_cb, NULL);
    }
}

static void _start_password_change(void)
{
    _password_flow_state_t *state = (_password_flow_state_t *)eos_malloc_zeroed(sizeof(_password_flow_state_t));
    if (!state)
        return;
    state->is_changing = true;
    bool simple = eos_config_get_bool(EOS_CONFIG_KEY_PASSWORD_SIMPLE_BOOL, true);
    state->target_length = simple ? 4 : 6;
    const char *title = "Enter Old Passcode";
    _show_password_numpad(title,
                          state->target_length,
                          _password_change_verify_old_cb,
                          _password_change_cancel_cb,
                          state);
}

/* ---- Password Settings Sub-Page ---- */

static void _password_subpage_on_destroy(eos_activity_t *activity)
{
    _password_subpage_refs_t *refs = (_password_subpage_refs_t *)eos_activity_get_user_data(activity);
    if (refs)
        eos_free(refs);
}

static void _password_subpage_refresh(_password_subpage_refs_t *refs)
{
    if (!refs)
        return;
    bool is_enabled = eos_config_get_bool(EOS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, false);
    char *hash = eos_config_get_string(EOS_CONFIG_KEY_PASSWORD_HASH_STR, "");
    bool has_hash = (hash && strlen(hash) > 0);
    eos_free(hash);

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

    /* Enable/disable Simple Password switch (container + switch) */
    if (refs->simple_sw && lv_obj_is_valid(refs->simple_sw))
    {
        lv_obj_t *container = lv_obj_get_parent(refs->simple_sw);
        if (is_enabled && has_hash)
        {
            lv_obj_add_state(refs->simple_sw, LV_STATE_DISABLED);
            if (container && lv_obj_is_valid(container))
            {
                lv_obj_add_state(container, LV_STATE_DISABLED);
            }
        }
        else
        {
            lv_obj_remove_state(refs->simple_sw, LV_STATE_DISABLED);
            if (container && lv_obj_is_valid(container))
            {
                lv_obj_remove_state(container, LV_STATE_DISABLED);
            }
        }
    }
}

static void _password_subpage_on_resume(eos_activity_t *activity)
{
    _password_subpage_refs_t *refs = (_password_subpage_refs_t *)eos_activity_get_user_data(activity);
    _password_subpage_refresh(refs);
}

static void _password_enable_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    _password_subpage_refs_t *refs = (_password_subpage_refs_t *)lv_event_get_user_data(e);
    bool is_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (is_enabled)
    {
        /* Defer creation so it doesn't interfere with list animation */
        bool simple = eos_config_get_bool(EOS_CONFIG_KEY_PASSWORD_SIMPLE_BOOL, true);
        lv_async_call((lv_async_cb_t)_start_creation_async_cb, (void *)(uintptr_t)(simple ? 4 : 6));
    }
    else
    {
        eos_config_set_bool(EOS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, false);
        eos_config_set_string(EOS_CONFIG_KEY_PASSWORD_HASH_STR, "");
        eos_toast_show_char_icon(RI_CHECKBOX_CIRCLE_FILL, EOS_COLOR_GREEN, "Password disabled");
        _password_subpage_refresh(refs);
    }
}

static void _password_simple_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool simple = lv_obj_has_state(sw, LV_STATE_CHECKED);
    eos_config_set_bool(EOS_CONFIG_KEY_PASSWORD_SIMPLE_BOOL, simple);
}

static void _password_change_clicked_cb(lv_event_t *e)
{
    (void)e;
    _start_password_change();
}

static void _settings_view_password(lv_event_t *e)
{
    (void)e;
    static const eos_activity_lifecycle_t lifecycle = {
        .on_destroy = _password_subpage_on_destroy,
        .on_enter = _password_subpage_on_resume,
        .on_resume = _password_subpage_on_resume,
    };

    eos_activity_t *a = eos_activity_create(&lifecycle);
    EOS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = eos_activity_get_view(a);
    EOS_CHECK_PTR_RETURN(view);
    eos_activity_set_title_id(a, STR_ID_SETTINGS_PASSWORD);
    eos_activity_set_app_header_visible(a, true);

    _password_subpage_refs_t *refs = (_password_subpage_refs_t *)eos_malloc_zeroed(sizeof(_password_subpage_refs_t));
    eos_activity_set_user_data(a, refs);

    lv_obj_t *list = eos_list_create(view);
    refs->list = list;

    bool is_enabled = eos_config_get_bool(EOS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, false);
    char *hash = eos_config_get_string(EOS_CONFIG_KEY_PASSWORD_HASH_STR, "");
    bool has_hash = (hash && strlen(hash) > 0);

    /* Enable Password switch */
    refs->enable_sw = _auto_get_config_switch_create(list,
                                                     eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_ENABLE),
                                                     EOS_CONFIG_KEY_PASSWORD_ENABLED_BOOL,
                                                     false);
    lv_obj_add_event_cb(refs->enable_sw, _password_enable_sw_cb, LV_EVENT_VALUE_CHANGED, refs);
    /* Tag the switch container so list transition animation targets this row */
    lv_obj_t *enable_container = lv_obj_get_parent(refs->enable_sw);
    if (enable_container)
        lv_obj_add_flag(enable_container, LV_OBJ_FLAG_USER_1);

    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    /* Change Password button (disabled when no password) */
    refs->change_btn = eos_list_add_entry_button_str_id(list, STR_ID_SETTINGS_PASSWORD_CHANGE);
    lv_obj_add_event_cb(refs->change_btn, _password_change_clicked_cb, LV_EVENT_CLICKED, NULL);
    if (!is_enabled || !has_hash)
    {
        lv_obj_add_state(refs->change_btn, LV_STATE_DISABLED);
    }
    eos_list_add_placeholder(list, EOS_LIST_SECTION_PLACEHOLDER_HEIGHT);

    /* Simple Password switch (disabled when password is set) */
    refs->simple_sw = _auto_get_config_switch_create(list,
                                                     eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_SIMPLE),
                                                     EOS_CONFIG_KEY_PASSWORD_SIMPLE_BOOL,
                                                     true);
    lv_obj_add_event_cb(refs->simple_sw, _password_simple_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);
    eos_list_add_comment(list, eos_lang_get_text(STR_ID_SETTINGS_PASSWORD_SIMPLE_COMMENT));
    if (is_enabled && has_hash)
    {
        lv_obj_add_state(refs->simple_sw, LV_STATE_DISABLED);
        lv_obj_t *simple_container = lv_obj_get_parent(refs->simple_sw);
        if (simple_container)
            lv_obj_add_state(simple_container, LV_STATE_DISABLED);
    }

    eos_free(hash);
    eos_activity_enter(a);
}

/************************** General Settings **************************/

static void _device_name_input_closed_cb(const char *text, eos_input_result_t result, void *user_data)
{
    if (result != EOS_INPUT_RESULT_OK)
    {
        return;
    }

    lv_obj_t *label = (lv_obj_t *)user_data;
    const char *new_name = text ? text : "";

    if (eos_config_set_string(EOS_CONFIG_KEY_DEVICE_NAME_STR, new_name) == EOS_OK)
    {
        if (label && lv_obj_is_valid(label))
        {
            lv_label_set_text(label, new_name);
        }
    }
    else
    {
        EOS_LOG_E("Failed to save device name to config");
    }
}

static void _device_name_click_cb(lv_event_t *e)
{
    lv_obj_t *label = lv_event_get_target(e);
    EOS_CHECK_PTR_RETURN(label);

    eos_result_t result = eos_input_page_open_with_callback(label, _device_name_input_closed_cb, label);

    if (result != EOS_OK)
    {
        EOS_LOG_E("Failed to open input page for device name edit");
    }
}

static void _settings_view_device_info(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    eos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_GENERAL_DEVICE_INFO, &view);
    EOS_CHECK_PTR_RETURN(a && view);
    eos_activity_set_title_color(a, EOS_THEME_LOGO_PRIMARY_COLOR);

    lv_obj_t *list = eos_list_create(view);
    lv_obj_set_style_pad_row(list, 0, 0);

    eos_list_add_placeholder(list, 20);
    lv_obj_t *logo = lv_image_create(list);
    lv_image_set_src(logo, EOS_IMG_LOGO);
    eos_list_add_placeholder(list, 30);

    char *device_name = eos_config_get_string(EOS_CONFIG_KEY_DEVICE_NAME_STR, EOS_CONFIG_DEFAULT_DEVICE_NAME);

    lv_obj_t *title_label = eos_list_add_title(list, eos_lang_get_text(STR_ID_SETTINGS_GENERAL_DEVICE_NAME));
    eos_label_set_font_size(title_label, EOS_FONT_SIZE_LARGE);

    lv_obj_t *device_name_label = eos_list_add_comment(list, device_name);
    lv_obj_set_style_text_color(device_name_label, EOS_COLOR_WHITE, 0);
    lv_obj_add_flag(device_name_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(device_name_label, _device_name_click_cb, LV_EVENT_CLICKED, NULL);

    if (device_name)
        eos_free(device_name);
    eos_list_add_placeholder(list, 20);

    eos_std_title_comment_create(list, eos_lang_get_text(STR_ID_SETTINGS_GENERAL_EOS_VER), ELENIX_OS_VERSION_FULL);
    eos_list_add_placeholder(list, 20);

    eos_std_title_comment_create(list,
                                 eos_lang_get_text(STR_ID_SETTINGS_GENERAL_MARKETING_NAME),
                                 ELENIX_WATCH_MARKETING_NAME);
    eos_list_add_placeholder(list, 20);

    eos_std_title_comment_create(list,
                                 eos_lang_get_text(STR_ID_SETTINGS_GENERAL_MODEL_NUMBER),
                                 ELENIX_WATCH_MODEL_NUMBER);
    eos_list_add_placeholder(list, 20);

    char install_number_str[32];
    snprintf(install_number_str, sizeof(install_number_str), "%" PRIu32, eos_app_get_installed());
    eos_std_title_comment_create(list, eos_lang_get_text(STR_ID_SETTINGS_APPS), install_number_str);
    eos_list_add_placeholder(list, 20);

    eos_std_title_comment_create(list,
                                 eos_lang_get_text(STR_ID_SETTINGS_GENERAL_OPEN_SOURCE),
                                 eos_lang_get_text(STR_ID_SETTINGS_GENERAL_OPEN_SOURCE_CONTENT));

#if LV_USE_QRCODE
    lv_obj_t *qr = lv_qrcode_create(list);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, EOS_COLOR_BLACK);
    lv_qrcode_set_light_color(qr, EOS_COLOR_WHITE);
    lv_obj_set_style_margin_all(qr, 20, 0);
    const char *repo_data = "https://github.com/ElenixOS/ElenixOS";
    lv_qrcode_update(qr, repo_data, strlen(repo_data));
    lv_obj_set_style_border_color(qr, EOS_COLOR_WHITE, 0);
    lv_obj_set_style_border_width(qr, 8, 0);
#endif /* LV_USE_QRCODE */

    eos_std_title_comment_create(list,
                                 eos_lang_get_text(STR_ID_SETTINGS_GENERAL_LEGAL_INFO),
                                 eos_lang_get_text(STR_ID_SETTINGS_GENERAL_LEGAL_INFO_CONTENT));
#if LV_USE_QRCODE
    qr = lv_qrcode_create(list);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, EOS_COLOR_BLACK);
    lv_qrcode_set_light_color(qr, EOS_COLOR_WHITE);
    lv_obj_set_style_margin_all(qr, 20, 0);
    const char *legal_data = "https://www.apache.org/licenses/LICENSE-2.0";
    lv_qrcode_update(qr, legal_data, strlen(legal_data));
    lv_obj_set_style_border_color(qr, EOS_COLOR_WHITE, 0);
    lv_obj_set_style_border_width(qr, 8, 0);
#endif /* LV_USE_QRCODE */

    eos_activity_enter(a);
}

static void _settings_view_general(lv_event_t *e)
{
    lv_obj_t *view = NULL;
    eos_activity_t *a = _create_activity_with_header(STR_ID_SETTINGS_GENERAL, &view);
    EOS_CHECK_PTR_RETURN(a && view);

    lv_obj_t *list = eos_list_create(view);

    lv_obj_t *btn;
    // Device info (system is English-only, no language picker)
    btn = eos_list_add_entry_button_str_id(list, STR_ID_SETTINGS_GENERAL_DEVICE_INFO);
    lv_obj_add_event_cb(btn, _settings_view_device_info, LV_EVENT_CLICKED, NULL);
    eos_activity_enter(a);
}

/************************** System Settings Program Entry **************************/
static const eos_activity_lifecycle_t _settings_lifecycle = {
    .on_enter = NULL,
    .on_destroy = NULL,
};
void eos_settings_enter(void)
{
    eos_activity_t *a = eos_activity_create(&_settings_lifecycle);
    EOS_CHECK_PTR_RETURN(a);
    lv_obj_t *view = eos_activity_get_view(a);
    EOS_CHECK_PTR_RETURN(view);
    eos_activity_set_title_id(a, STR_ID_SETTINGS);
    eos_activity_set_app_header_visible(a, true);

    lv_obj_t *settings_list = eos_list_create(view);

    lv_obj_t *btn;
    // Wi-Fi settings
    btn = eos_list_add_round_icon_button_str_id(settings_list, EOS_COLOR_BLUE, RI_WIFI_FILL, STR_ID_SETTINGS_WIFI);
    lv_obj_add_event_cb(btn, _settings_view_wifi, LV_EVENT_CLICKED, NULL);
    // VPN (WireGuard via microlink) settings
    btn = eos_list_add_round_icon_button(settings_list, EOS_COLOR_GREEN, RI_SHIELD_KEYHOLE_FILL, "VPN");
    lv_obj_add_event_cb(btn, _settings_view_vpn, LV_EVENT_CLICKED, NULL);
    // Bluetooth settings
    btn = eos_list_add_round_icon_button_str_id(settings_list,
                                                EOS_COLOR_BLUE,
                                                RI_BLUETOOTH_FILL,
                                                STR_ID_SETTINGS_BLUETOOTH);
    lv_obj_add_event_cb(btn, _settings_view_bluetooth, LV_EVENT_CLICKED, NULL);
    // Display settings
    btn = eos_list_add_round_icon_button_str_id(settings_list, EOS_COLOR_ORANGE, RI_SUN_FILL, STR_ID_SETTINGS_DISPLAY);
    lv_obj_add_event_cb(btn, _settings_view_display, LV_EVENT_CLICKED, NULL);
    // Notification settings
    btn = eos_list_add_round_icon_button_str_id(settings_list,
                                                EOS_COLOR_RED,
                                                RI_NOTIFICATION_2_FILL,
                                                STR_ID_SETTINGS_NOTIFICATION);
    lv_obj_add_event_cb(btn, _settings_view_notification, LV_EVENT_CLICKED, NULL);
    // Password settings
    btn = eos_list_add_round_icon_button_str_id(settings_list,
                                                EOS_COLOR_TEAL_BLUE,
                                                RI_LOCK_LINE,
                                                STR_ID_SETTINGS_PASSWORD);
    lv_obj_add_event_cb(btn, _settings_view_password, LV_EVENT_CLICKED, NULL);
    // App list
    btn =
        eos_list_add_round_icon_button_str_id(settings_list, EOS_COLOR_GREY, RI_FILE_LIST_LINE, STR_ID_SETTINGS_APPS);
    lv_obj_add_event_cb(btn, _settings_view_apps, LV_EVENT_CLICKED, NULL);
    // General settings
    btn = eos_list_add_round_icon_button_str_id(settings_list, EOS_COLOR_GREY, RI_TOOLS_FILL, STR_ID_SETTINGS_GENERAL);
    lv_obj_add_event_cb(btn, _settings_view_general, LV_EVENT_CLICKED, NULL);

    eos_activity_enter(a);
}
