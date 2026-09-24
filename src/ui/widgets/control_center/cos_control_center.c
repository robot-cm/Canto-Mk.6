/**
 * @file eos_control_center.c
 * @brief Pull-up control center
 */

#include "eos_control_center.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "eos_swipe_panel.h"
#define EOS_LOG_TAG "ControlCenter"
#include "eos_log.h"
#include "eos_theme.h"
#include "eos_service_config.h"
#include "eos_port.h"
#include "eos_service_audio.h"
#include "eos_config.h"
#include "eos_event.h"
#include "eos_icon.h"
#include "eos_flash_light.h"
#include "eos_net_wifi.h"
#include "eos_net_bt.h"
#include "eos_service_power_save.h"
#include "eos_service_beast_mode.h"
#include "eos_anim.h"
#include "eos_lang.h"
#include "eos_service_display.h"
#include "eos_settings.h"
#include "eos_mem.h"
#include "eos_basic_widgets.h"
#include "eos_crown.h"
#include "eos_service_battery.h"
#include "eos_chrome_manager.h"
#include "eos_overlay_layer.h"
#include "eos_activity.h"
#include "eos_shell_framework.h"
/* SD 快照(/sdcard/history/cc/settings.txt):四项开关状态的跨深睡/跨卡持久化。
 * 无真 SD 卡时本服务完全透明(空操作),仍走 cfg.json 原路径。 */
#include "eos_service_cc_snapshot.h"

/* 彩色图标(由 resources/images/icon 下的 webp 转换的 ARGB8888 静态图) */
extern const lv_image_dsc_t eos_icon_bluetooth;
extern const lv_image_dsc_t eos_icon_wifi;
extern const lv_image_dsc_t eos_icon_torch;
extern const lv_image_dsc_t eos_icon_powersave;
extern const lv_image_dsc_t eos_icon_beastmode;
extern const lv_image_dsc_t eos_icon_setting;
extern const lv_image_dsc_t eos_icon_devmode;

/* Macros and Definitions -------------------------------------*/
#define _BTN_DEFAULT_COLOR EOS_THEME_SECONDARY_COLOR
/* 侧栏按钮尺寸:纯图标(无文字标签),52x40 紧凑尺寸使 2x4 八槽位一屏放下
 * 2 列:52*2 + 列距24 + 左右pad 28*2 = 184 ≤ 240,圆屏边缘留安全边距
 * 4 行:40*4 + 行距6*3 + 上下pad 14*2 = 206 ≤ 88%屏高(211),无需滚动 */
#define _CC_BTN_W 52
#define _CC_BTN_H 40
#define _CC_BTN_RADIUS 16
#define _SLIDER_DEFAULT_WIDTH 150
#define _SLIDER_DEFAULT_HEIGHT 360
#define _SLIDER_DEFAULT_RADIUS 50
#define _LIST_SCALE_THRESHOLD_Y ((float)EOS_DISPLAY_HEIGHT * 0.8)
/* Variables --------------------------------------------------*/
static eos_control_center_t *control_center_instance = NULL;
/* 亮度 slider 的全屏遮罩页(overlay layer 最顶层)。
 * CC 关闭时若仍残留(用户拖完直接下拉,未点空白处),会拦截触摸且 config
 * 不保存——必须随 CC hide 一起删除,并在删除时同步置 NULL 防悬垂。 */
static lv_obj_t *_brightness_slider_page = NULL;
static void _control_center_overlay_pull_back(void);
static void _control_center_overlay_hide(void);
static void _control_center_overlay_on_focus(void);
static bool _control_center_overlay_is_open(void);
static lv_obj_t *_control_center_overlay_get_scrollable(void);
static lv_obj_t *_control_center_overlay_get_foreground_obj(void);
static const eos_chrome_overlay_t _control_center_overlay = {
    .pull_back = _control_center_overlay_pull_back,
    .hide = _control_center_overlay_hide,
    .on_focus = _control_center_overlay_on_focus,
    .is_open = _control_center_overlay_is_open,
    .get_scrollable = _control_center_overlay_get_scrollable,
    .get_foreground_obj = _control_center_overlay_get_foreground_obj,
    .name = "control_center",
};
/* Function Implementations -----------------------------------*/
void eos_control_panel_slide_change(void);

/************************** List callback **************************/

static void _list_scroll_cb(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_user_data(e);
    lv_coord_t list_h = lv_obj_get_height(list);
    lv_coord_t list_y = lv_obj_get_y(list);

    uint32_t count = lv_obj_get_child_count(list);

    lv_coord_t bottom_threshold = list_y + (list_h * 8) / 10;
    lv_coord_t list_bottom = list_y + list_h;

    for (uint32_t i = 0; i < count; i++)
    {
        lv_obj_t *child = lv_obj_get_child(list, i);

        lv_coord_t w = lv_obj_get_width(child);
        lv_coord_t h = lv_obj_get_height(child);

        if (i % 2 == 0)
        {
            // Left column -> Upper right corner
            lv_obj_set_style_transform_pivot_x(child, w, 0);
            lv_obj_set_style_transform_pivot_y(child, 0, 0);
        }
        else
        {
            // Right column -> Upper left corner
            lv_obj_set_style_transform_pivot_x(child, 0, 0);
            lv_obj_set_style_transform_pivot_y(child, 0, 0);
        }

        lv_area_t child_area;
        lv_obj_get_coords(child, &child_area);

        lv_coord_t child_center = (child_area.y1 + child_area.y2) / 2;

        if (child_center < bottom_threshold)
        {
            lv_obj_set_style_transform_scale(child, 256, 0);
            continue;
        }

        lv_coord_t diff = list_bottom - child_center;
        const lv_coord_t range = list_h / 10;
        const int16_t max_scale = 256; // Original scale
        const int16_t min_scale = 180; // Minimum scale (when just entering)

        /* Guard: list_h < 10 makes range == 0 -> idiv #DE crash (Round 38e:
         * headless home-gesture test hit list_h=0 while the control center
         * was mid-slide; the 180->256 enter animation divided by zero). */
        int16_t scale = max_scale;
        if (range > 0)
        {
            scale = min_scale + (diff * (max_scale - min_scale)) / range;
            if (scale > max_scale)
                scale = max_scale;
            else if (scale < min_scale)
                scale = min_scale;
        }

        lv_obj_set_style_transform_scale(child, scale, 0);
    }
}

static void _slide_widget_reached_threshold_cb(lv_event_t *e)
{
    lv_obj_t *container = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_scroll_to_y(container, 0, LV_ANIM_OFF);
    if (lv_obj_get_y(control_center_instance->swipe_panel->swipe_obj) < EOS_DISPLAY_HEIGHT)
        eos_crown_encoder_set_target_obj(container);
}

/************************** Basic components **************************/

static lv_obj_t *_control_center_create_switch_btn(lv_obj_t *parent, const char *symbol, const char *label_text, lv_color_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_size(btn, _CC_BTN_W, _CC_BTN_H);
    lv_obj_set_style_radius(btn, _CC_BTN_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, _BTN_DEFAULT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, color, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_pad_all(btn, 0, 0);

    /* 图标(默认字体含 RemixIcon glyph);label_text 为空时仅图标居中辨认 */
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, EOS_COLOR_WHITE, 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    if (label_text != NULL && label_text[0] != '\0')
    {
        /* 图标在上、文字在下 */
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, label_text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, _CC_BTN_W);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, EOS_COLOR_WHITE, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
    else
    {
        /* 纯图标:居中 */
        lv_obj_center(icon);
    }
    return btn;
}

static void _control_center_slider_page_clicked_cb(lv_event_t *e)
{
    lv_obj_t *page = lv_event_get_target(e);
    eos_anim_fade_start(page, LV_OPA_80, LV_OPA_TRANSP, 200, true);
}

static lv_obj_t *_control_center_slider_create(const char *symbol)
{
    /* 重复点亮度按钮:先清理上一次可能残留的遮罩页(含 slider 等子对象) */
    if (_brightness_slider_page != NULL)
    {
        lv_obj_delete(_brightness_slider_page);
        _brightness_slider_page = NULL;
    }

    lv_obj_t *slider_page = lv_obj_create(eos_overlay_get_overlay_layer());
    _brightness_slider_page = slider_page;
    lv_obj_remove_style_all(slider_page);
    lv_obj_set_size(slider_page, lv_pct(100), lv_pct(100));
    lv_obj_move_foreground(slider_page);
    lv_obj_set_style_bg_opa(slider_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(slider_page, EOS_COLOR_BLACK, 0);
    lv_obj_add_event_cb(slider_page, _control_center_slider_page_clicked_cb, LV_EVENT_CLICKED, NULL);

    eos_anim_t *a = eos_anim_fade_create(slider_page, LV_OPA_TRANSP, LV_OPA_80, 200, false);
    eos_anim_fade_set_layered(a, false);
    eos_anim_start(a);

    lv_obj_t *slider_mask = lv_obj_create(slider_page);
    lv_obj_remove_style_all(slider_mask);
    lv_obj_set_size(slider_mask, _SLIDER_DEFAULT_WIDTH, _SLIDER_DEFAULT_HEIGHT);
    lv_obj_center(slider_mask);
    lv_obj_set_style_clip_corner(slider_mask, true, 0);
    lv_obj_set_style_radius(slider_mask, 50, 0);

    lv_obj_t *slider = lv_slider_create(slider_mask);
    lv_bar_set_orientation(slider, LV_BAR_ORIENTATION_VERTICAL);
    lv_obj_set_size(slider, _SLIDER_DEFAULT_WIDTH, _SLIDER_DEFAULT_HEIGHT);
    lv_obj_center(slider);

    lv_obj_set_style_clip_corner(slider, true, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_set_style_width(slider, _SLIDER_DEFAULT_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_height(slider, _SLIDER_DEFAULT_HEIGHT, LV_PART_MAIN);

    lv_obj_set_style_bg_color(slider, EOS_COLOR_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 50, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(slider, EOS_COLOR_DARK_GREY_1, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 50, LV_PART_MAIN);

    lv_obj_set_style_bg_color(slider, EOS_COLOR_WHITE, LV_PART_INDICATOR | LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(slider_page);
    lv_label_set_text(label, symbol);
    lv_obj_set_user_data(slider, (void *)label);
    lv_obj_set_style_text_color(label, EOS_COLOR_BLACK, 0);
    lv_obj_move_foreground(label);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -100);

    lv_obj_update_layout(label);
    lv_obj_set_style_transform_pivot_x(label, lv_obj_get_width(label) / 2, 0);
    lv_obj_set_style_transform_pivot_y(label, lv_obj_get_height(label) / 2, 0);

    eos_anim_fade_start(slider, LV_OPA_TRANSP, LV_OPA_COVER, 200, false);

    return slider;
}

static lv_obj_t *_control_center_create_btn(lv_obj_t *parent, const char *symbol, const char *label_text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, _CC_BTN_W, _CC_BTN_H);
    lv_obj_set_style_radius(btn, _CC_BTN_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, _BTN_DEFAULT_COLOR, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, EOS_COLOR_WHITE, 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    if (label_text != NULL && label_text[0] != '\0')
    {
        /* 图标在上、文字在下 */
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, label_text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, _CC_BTN_W);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, EOS_COLOR_WHITE, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
    else
    {
        /* 纯图标:居中(Control Center 一屏六按钮仅图标辨认) */
        lv_obj_center(icon);
    }
    return btn;
}

/* 图片图标版按钮:icon 用 ARGB8888 静态图(webp 转换),布局与字符版一致 */
static lv_obj_t *_control_center_create_img_btn(lv_obj_t *parent, const lv_image_dsc_t *img, const char *label_text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, _CC_BTN_W, _CC_BTN_H);
    lv_obj_set_style_radius(btn, _CC_BTN_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, _BTN_DEFAULT_COLOR, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *icon = lv_image_create(btn);
    lv_image_set_src(icon, img);
    if (label_text != NULL && label_text[0] != '\0')
    {
        /* 图标在上、文字在下 */
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, label_text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, _CC_BTN_W);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, EOS_COLOR_WHITE, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
    else
    {
        /* 纯图标:居中 */
        lv_obj_center(icon);
    }
    return btn;
}

/* 图片图标版开关按钮:checked 状态背景色变化,图标保持原色 */
static lv_obj_t *_control_center_create_img_switch_btn(lv_obj_t *parent, const lv_image_dsc_t *img, const char *label_text, lv_color_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_size(btn, _CC_BTN_W, _CC_BTN_H);
    lv_obj_set_style_radius(btn, _CC_BTN_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, _BTN_DEFAULT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, color, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *icon = lv_image_create(btn);
    lv_image_set_src(icon, img);
    if (label_text != NULL && label_text[0] != '\0')
    {
        /* 图标在上、文字在下 */
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 4);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, label_text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, _CC_BTN_W);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, EOS_COLOR_WHITE, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }
    else
    {
        /* 纯图标:居中 */
        lv_obj_center(icon);
    }
    return btn;
}

/************************** Functional components **************************/

static void _control_center_bluetooth_switch_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    if (lv_obj_has_state(btn, LV_STATE_CHECKED))
    {
        EOS_LOG_D("CHECKED");
        eos_bluetooth_enable();
        eos_config_set_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, true);
        eos_cc_snapshot_store_bt(true);
        /* 优先连接记忆中最频繁连接的可见设备 */
        eos_net_bt_connect_from_history();
    }
    else
    {
        EOS_LOG_D("UNCHECKED");
        eos_bluetooth_disable();
        eos_config_set_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, false);
        eos_cc_snapshot_store_bt(false);
    }
}

static void _control_center_brightness_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *label = (lv_obj_t *)lv_obj_get_user_data(slider);

    // Get current Slider value
    int32_t value = lv_slider_get_value(slider);
    /* 亮度下发是功能本体,label(旋转装饰)缺失不得阻断——必须先设置亮度 */
    eos_display_set_brightness((uint8_t)value, EOS_DISPLAY_DURATION_OFF, false);

    if (label != NULL)
    {
        // Map Slider value to angle
        int32_t angle = (int32_t)value * 18;
        // Set Label rotation
        lv_obj_set_style_transform_rotation(label, angle, 0);
    }
}

static void _control_center_brightness_slider_released_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    /* 松手即持久化:不依赖 slider 被删除(用户可能拖完直接下拉关 CC,
     * 遮罩页由 overlay_hide 清理时才触发 DELETE,那会丢配置)。 */
    eos_config_set_number(EOS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, lv_slider_get_value(slider));
    if (eos_cc_snapshot_storage_available())
        eos_cc_snapshot_store_brightness((uint8_t)lv_slider_get_value(slider));
}

static void _control_center_brightness_slider_delete_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    eos_config_set_number(EOS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, lv_slider_get_value(slider));
    if (eos_cc_snapshot_storage_available())
        eos_cc_snapshot_store_brightness((uint8_t)lv_slider_get_value(slider));
    _brightness_slider_page = NULL;
}

static void _control_center_brightness_btn_clicked_cb(lv_event_t *e)
{
    lv_obj_t *slider = _control_center_slider_create(RI_SUN_LINE);
    lv_slider_set_range(slider, EOS_DISPLAY_BRIGHTNESS_MIN, EOS_DISPLAY_BRIGHTNESS_MAX);
    lv_slider_set_value(slider, eos_config_get_number(EOS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, 50), LV_ANIM_ON);
    lv_obj_add_event_cb(slider, _control_center_brightness_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, _control_center_brightness_slider_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, _control_center_brightness_slider_delete_cb, LV_EVENT_DELETE, NULL);
}

static void _control_center_flash_light_btn_clicked_cb(lv_event_t *e)
{
    eos_flash_light_show();
}

/* WiFi 开关回滚轮询上下文(异步初始化结果检查) */
typedef struct
{
    lv_obj_t *btn;
    int       tries;
} wifi_rollback_ctx_t;

/* 每 2s 轮询一次:radio 起不来(EOS_WIFI_ERROR)则回滚开关;
 * 连接成功(CONNECTED)或关闭(DISABLED)或超过 20s 则停止。
 * 说明:打开开关后扫描+连接在后台 worker 执行(最长约 15s),失败是
 * 异步发生的,不能在开关回调里同步判断,故用轮询代替原同步回滚。 */
static void _wifi_switch_rollback_cb(lv_timer_t *t)
{
    wifi_rollback_ctx_t *ctx = lv_timer_get_user_data(t);
    if (!ctx)
    {
        lv_timer_del(t);
        return;
    }
    eos_wifi_state_t st = eos_net_wifi_state();
    if (st == EOS_WIFI_ERROR)
    {
        if (lv_obj_has_state(ctx->btn, LV_STATE_CHECKED))
        {
            EOS_LOG_W("WiFi enable failed (async), switch rolled back");
            eos_net_wifi_set_enabled(false);
            eos_cc_snapshot_store_wifi(false);
            lv_obj_clear_state(ctx->btn, LV_STATE_CHECKED);
        }
        lv_timer_del(t);
        lv_free(ctx);
        return;
    }
    if (st == EOS_WIFI_CONNECTED || st == EOS_WIFI_DISABLED || ++ctx->tries >= 10)
    {
        lv_timer_del(t);
        lv_free(ctx);
    }
}

static void _control_center_wifi_switch_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    if (lv_obj_has_state(btn, LV_STATE_CHECKED))
    {
        EOS_LOG_I("WiFi switch ON");
        eos_net_wifi_set_enabled(true);
        eos_cc_snapshot_store_wifi(true);
        /* 异步执行"扫描+连接记忆 AP"(后台 worker task),立即返回,不阻塞 UI */
        eos_net_wifi_connect_from_history();
        /* 异步结果轮询:radio 起不来则回滚开关(避免"界面已开、实际没开") */
        wifi_rollback_ctx_t *ctx = lv_malloc(sizeof(wifi_rollback_ctx_t));
        if (ctx)
        {
            ctx->btn = btn;
            ctx->tries = 0;
            lv_timer_t *rt = lv_timer_create(_wifi_switch_rollback_cb, 2000, ctx);
            lv_timer_set_repeat_count(rt, 11); /* 上限 22s,回调内也会自删 */
        }
    }
    else
    {
        EOS_LOG_I("WiFi switch OFF");
        eos_net_wifi_set_enabled(false);
        eos_cc_snapshot_store_wifi(false);
    }
}

static void _control_center_power_save_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    if (lv_obj_has_state(btn, LV_STATE_CHECKED))
    {
        EOS_LOG_I("Power save switch ON");
        /* 互斥:若性能模式已开启,eos_power_save_enter 会自动退出;
         * 此处同步清除其开关状态,避免 UI 显示两个都开 */
        if (eos_beast_mode_is_active() && control_center_instance &&
            control_center_instance->beast_mode_btn)
        {
            lv_obj_clear_state(control_center_instance->beast_mode_btn, LV_STATE_CHECKED);
        }
        eos_power_save_enter();
        eos_cc_snapshot_store_power(EOS_CC_POWER_SAVE);
        /* 省电模式只能停留在主界面:收起控制中心 */
        eos_control_center_hide();
    }
    else
    {
        EOS_LOG_I("Power save switch OFF");
        eos_power_save_exit();
        eos_cc_snapshot_store_power(EOS_CC_POWER_SMART);
    }
}

static void _control_center_beast_mode_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    if (lv_obj_has_state(btn, LV_STATE_CHECKED))
    {
        EOS_LOG_I("Beast mode switch ON");
        /* 互斥:若省电模式已开启,eos_beast_mode_enter 会自动退出;
         * 此处同步清除其开关状态,避免 UI 显示两个都开 */
        if (eos_power_save_is_active() && control_center_instance &&
            control_center_instance->power_save_btn)
        {
            lv_obj_clear_state(control_center_instance->power_save_btn, LV_STATE_CHECKED);
        }
        eos_beast_mode_enter();
        eos_cc_snapshot_store_power(EOS_CC_POWER_BEAST);
    }
    else
    {
        EOS_LOG_I("Beast mode switch OFF");
        eos_beast_mode_exit();
        eos_cc_snapshot_store_power(EOS_CC_POWER_SMART);
    }
}

static void _control_center_dev_mode_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    if (lv_obj_has_state(btn, LV_STATE_CHECKED))
    {
        EOS_LOG_I("Dev mode switch ON (shell + serial log)");
        eos_shell_framework_set_console_enabled(true);
        eos_config_set_bool(EOS_CONFIG_KEY_DEV_MODE_BOOL, true);
    }
    else
    {
        EOS_LOG_I("Dev mode switch OFF");
        eos_shell_framework_set_console_enabled(false);
        eos_config_set_bool(EOS_CONFIG_KEY_DEV_MODE_BOOL, false);
    }
}

static void _control_center_settings_entry_cb(lv_event_t *e)
{
    eos_settings_enter();
}

static void _control_center_opened_cb(lv_event_t *e)
{
    EOS_LOG_I("Control center opened");
    eos_chrome_manager_notify_overlay_opened(&_control_center_overlay);
    /* Panel is open: show the strip so the user can drag it shut. The strip
     * also has the full-drag callbacks now (set via set_full_drag(true)) so
     * the panel itself can be dragged shut anywhere on screen. */
    lv_obj_clear_flag(eos_slide_widget_get_touch_obj(control_center_instance->swipe_panel->sw), LV_OBJ_FLAG_HIDDEN);
}

static void _control_center_closed_cb(lv_event_t *e)
{
    EOS_LOG_I("Control center closed");
    eos_chrome_manager_notify_overlay_closed(&_control_center_overlay);
    /* Panel is closed: hide the strip so the home catcher (not the strip)
     * owns the left-edge right-swipe that re-opens it. Always hide, on any
     * activity — including the watchface, which is where the open gesture
     * originates. */
    lv_obj_add_flag(eos_slide_widget_get_touch_obj(control_center_instance->swipe_panel->sw), LV_OBJ_FLAG_HIDDEN);
}

static void _control_center_overlay_pull_back(void)
{
    eos_control_center_t *cc = eos_control_center_get_instance();
    if (cc && cc->swipe_panel)
    {
        eos_swipe_panel_pull_back(cc->swipe_panel);
    }
}

static void _control_center_overlay_hide(void)
{
    /* CC 收起时清理可能残留的亮度 slider 遮罩页:用户未点空白处就
     * 下拉关闭/切走时,遮罩页会残留在 overlay 最顶层拦截触摸。
     * 删除触发 slider 的 DELETE 回调,亮度配置一并持久化。 */
    if (_brightness_slider_page != NULL)
    {
        lv_obj_delete(_brightness_slider_page);
        _brightness_slider_page = NULL;
    }
    eos_control_center_hide();
}

static void _control_center_overlay_on_focus(void)
{
    EOS_LOG_D("Control center focused");
}

static bool _control_center_overlay_is_open(void)
{
    eos_control_center_t *cc = eos_control_center_get_instance();
    if (cc && cc->swipe_panel && cc->swipe_panel->sw)
    {
        return eos_slide_widget_get_state(cc->swipe_panel->sw) == EOS_SLIDE_WIDGET_STATE_OPEN;
    }
    return false;
}

bool eos_control_center_is_open(void)
{
    return _control_center_overlay_is_open();
}

static lv_obj_t *_control_center_overlay_get_scrollable(void)
{
    eos_control_center_t *cc = eos_control_center_get_instance();
    if (cc && cc->container)
    {
        return cc->container;
    }
    return NULL;
}

static lv_obj_t *_control_center_overlay_get_foreground_obj(void)
{
    eos_control_center_t *cc = eos_control_center_get_instance();
    if (cc && cc->swipe_panel && cc->swipe_panel->sw)
    {
        return eos_slide_widget_get_touch_obj(cc->swipe_panel->sw);
    }
    return NULL;
}

/************************** Control center **************************/

eos_control_center_t *eos_control_center_create(lv_obj_t *parent)
{
    eos_control_center_t *cc = eos_malloc_zeroed(sizeof(eos_control_center_t));
    EOS_CHECK_PTR_RETURN_VAL(cc, NULL);

    eos_swipe_panel_t *swipe_panel = eos_swipe_panel_create(parent);
    /* Redmi-Watch style: right-swipe from the LEFT edge reveals the control
     * center. The touch strip is a 50px column on the left side of the screen,
     * and the vertical pill indicator (handle bar) sits on the left edge.
     * Left swipe opens the app list (handled by the watchface). */
    eos_swipe_panel_set_dir(swipe_panel, EOS_SWIPE_DIR_RIGHT);
    /* Without full-drag the touch strip intercepts the left-edge press but
     * cannot drag the panel open, so a right-swipe from the left edge does
     * nothing. (The cards page sets this too; the control center forgot it.) */
    eos_swipe_panel_set_full_drag(swipe_panel, true);
    eos_crown_encoder_register_slide_widget(swipe_panel->sw);
    eos_swipe_panel_show_handle_bar(swipe_panel);
    /* When CLOSED the strip must be HIDDEN, otherwise it sits on the overlay
     * layer covering the left edge and intercepts left-edge presses (LVGL 9
     * delivers PRESSED/PRESSING/RELEASED to the topmost object at the press
     * point regardless of CLICKABLE; a 50px strip then cannot capture a drag
     * that leaves its bounds, so PRESS_LOST drops the release and the panel
     * never opens). The home catcher on the watchface handles the open swipe
     * instead. The strip is re-shown in _control_center_opened_cb so the user
     * can drag the panel shut. */
    lv_obj_add_flag(eos_slide_widget_get_touch_obj(swipe_panel->sw), LV_OBJ_FLAG_HIDDEN);

    eos_slide_widget_add_event_cb_opened(swipe_panel->sw, _control_center_opened_cb, NULL);
    eos_slide_widget_add_event_cb_closed(swipe_panel->sw, _control_center_closed_cb, NULL);

    cc->swipe_panel = swipe_panel;

    lv_obj_t *container = lv_list_create(swipe_panel->swipe_obj);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(88));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    /* 2x4 八槽位一屏放下:左右 pad 28(圆屏边距),上下 pad 14(4 行不超出 88% 高度) */
    lv_obj_set_style_pad_left(container, 28, 0);
    lv_obj_set_style_pad_right(container, 28, 0);
    lv_obj_set_style_pad_top(container, 14, 0);
    lv_obj_set_style_pad_bottom(container, 14, 0);
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_scroll_dir(container, LV_DIR_NONE);
    lv_obj_set_style_pad_column(container, 24, 0); // Column spacing
    lv_obj_set_style_pad_row(container, 6, 0); // Row spacing (2x3 grid: 3 rows fit in 88% height)
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_CENTER, // Main axis (horizontal direction) starts from the beginning (left-aligned)
        LV_FLEX_ALIGN_CENTER, // Cross axis (vertical direction) centered
        LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动:七按钮固定一屏
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(container, _list_scroll_cb, LV_EVENT_SCROLL, container);
    eos_slide_widget_add_event_cb_moving(swipe_panel->sw, _list_scroll_cb, container);
    eos_slide_widget_add_event_cb_done(swipe_panel->sw, _list_scroll_cb, container);
    eos_slide_widget_add_event_cb_reached_threshold(swipe_panel->sw, _slide_widget_reached_threshold_cb, container);
    cc->container = container;
    lv_obj_t *btn;
    /* 侧栏功能集(2x4 七图标):Flash / 亮度 / 省电 / 性能 / WiFi / 蓝牙 / 设置。
     * 仅图标辨认(label_text=NULL),无英文/文字标注 */
    /************************** Flashlight **************************/
    btn = _control_center_create_img_btn(container, &eos_icon_torch, NULL);
    lv_obj_add_event_cb(btn, _control_center_flash_light_btn_clicked_cb, LV_EVENT_CLICKED, 0);
    cc->flash_light_btn = btn;
    /************************** Brightness adjustment scrollbar **************************/
    btn = _control_center_create_btn(container, RI_SUN_FILL, NULL);
    lv_obj_add_event_cb(btn, _control_center_brightness_btn_clicked_cb, LV_EVENT_CLICKED, 0);
    cc->brightness_btn = btn;
    /************************** Power save mode **************************/
    btn = _control_center_create_img_switch_btn(container, &eos_icon_powersave, NULL, EOS_COLOR_GREEN);
    lv_obj_add_event_cb(btn, _control_center_power_save_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (eos_power_save_is_active())
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
    cc->power_save_btn = btn;
    /************************** Beast mode (性能模式,与省电互斥) **************************/
    btn = _control_center_create_img_switch_btn(container, &eos_icon_beastmode, NULL, EOS_COLOR_ORANGE);
    lv_obj_add_event_cb(btn, _control_center_beast_mode_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (eos_beast_mode_is_active())
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
    cc->beast_mode_btn = btn;
    /************************** WiFi switch **************************/
    btn = _control_center_create_img_switch_btn(container, &eos_icon_wifi, NULL, EOS_COLOR_BLUE);
    lv_obj_add_event_cb(btn, _control_center_wifi_switch_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (eos_net_wifi_is_enabled())
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
    cc->wifi_btn = btn;
    /************************** Bluetooth switch **************************/
    btn = _control_center_create_img_switch_btn(container, &eos_icon_bluetooth, NULL, EOS_COLOR_BLUE);
    lv_obj_add_event_cb(btn, _control_center_bluetooth_switch_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (eos_config_get_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, false))
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
    cc->bl_btn = btn;
    /************************** Settings entry **************************/
    btn = _control_center_create_img_btn(container, &eos_icon_setting, NULL);
    lv_obj_add_event_cb(btn, _control_center_settings_entry_cb, LV_EVENT_CLICKED, 0);
    cc->settings_btn = btn;
    /************************** Dev mode (开发者模式:shell + 串口日志) **************************/
    btn = _control_center_create_img_switch_btn(container, &eos_icon_devmode, NULL, EOS_COLOR_PURPLE);
    lv_obj_add_event_cb(btn, _control_center_dev_mode_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (eos_shell_framework_get_console_enabled())
    {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
    cc->dev_btn = btn;
    return cc;
}

void eos_control_panel_slide_change(void)
{
    EOS_CHECK_PTR_RETURN(control_center_instance);
    eos_swipe_panel_t *sp = control_center_instance->swipe_panel;
    if (eos_slide_widget_get_state(sp->sw) == EOS_SLIDE_WIDGET_STATE_OPEN)
    {
        eos_swipe_panel_pull_back(sp);
    }
    else
    {
        eos_swipe_panel_slide_down(sp);
    }
}

void eos_control_center_show(void)
{
    EOS_CHECK_PTR_RETURN(control_center_instance);
    lv_obj_t *strip = eos_slide_widget_get_touch_obj(control_center_instance->swipe_panel->sw);
    /* Reveal the panel content so it can be slid open. Keep the 50px left-edge
     * touch strip HIDDEN while the panel is CLOSED: if it is visible it sits on
     * the overlay layer ABOVE the home gesture catcher and steals the left-edge
     * right-swipe that is supposed to open the panel (the catcher reacts to that
     * gesture by calling eos_swipe_panel_slide_down). Leaving the strip hidden
     * lets the catcher own the open gesture. _control_center_opened_cb re-shows
     * the strip once the panel is actually open (when full_drag lets the user
     * drag it shut), and _control_center_closed_cb hides it again. */
    lv_obj_remove_flag(control_center_instance->swipe_panel->swipe_obj, LV_OBJ_FLAG_HIDDEN);
    if (eos_slide_widget_get_state(control_center_instance->swipe_panel->sw) == EOS_SLIDE_WIDGET_STATE_OPEN)
        lv_obj_clear_flag(strip, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(strip, LV_OBJ_FLAG_HIDDEN);
    eos_crown_encoder_activate_current_overlay_scrollable();
}

void eos_control_center_hide(void)
{
    EOS_CHECK_PTR_RETURN(control_center_instance);
    lv_obj_add_flag(eos_slide_widget_get_touch_obj(control_center_instance->swipe_panel->sw), LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_get_y(control_center_instance->swipe_panel->swipe_obj) < EOS_DISPLAY_HEIGHT)
        lv_obj_add_flag(control_center_instance->swipe_panel->swipe_obj, LV_OBJ_FLAG_HIDDEN);
    eos_crown_encoder_activate_current_overlay_scrollable();
}

eos_control_center_t *eos_control_center_get_instance(void)
{
    return control_center_instance;
}

static void _system_config_update_event_cb(eos_event_t *e)
{
    LV_UNUSED(e);
    EOS_CHECK_PTR_RETURN(control_center_instance);

    // Update Bluetooth switch state
    if (eos_config_get_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, false))
    {
        lv_obj_add_state(control_center_instance->bl_btn, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(control_center_instance->bl_btn, LV_STATE_CHECKED);
    }

    // Update WiFi switch state
    if (control_center_instance->wifi_btn)
    {
        if (eos_net_wifi_is_enabled())
        {
            lv_obj_add_state(control_center_instance->wifi_btn, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(control_center_instance->wifi_btn, LV_STATE_CHECKED);
        }
    }

    // Update Dev mode switch state
    if (control_center_instance->dev_btn)
    {
        if (eos_shell_framework_get_console_enabled())
        {
            lv_obj_add_state(control_center_instance->dev_btn, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(control_center_instance->dev_btn, LV_STATE_CHECKED);
        }
    }

    EOS_LOG_D("Switch buttons updated");
}

void eos_control_center_init(void)
{
    control_center_instance = eos_control_center_create(eos_overlay_get_overlay_layer());
    eos_event_subscribe(EOS_EVENT_SYSTEM_CONFIG_UPDATE, _system_config_update_event_cb, NULL);
}

const eos_chrome_overlay_t *eos_control_center_get_overlay_descriptor(void)
{
    return &_control_center_overlay;
}
