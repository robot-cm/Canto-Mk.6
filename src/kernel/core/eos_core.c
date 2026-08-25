/**
 * @file eos_core.c
 * @brief Elenix OS core code implementation
 */

#include "eos_core.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "lvgl.h"
#include "eos_image.h"
#include "eos_msg_list.h"
#include "eos_lang.h"
#include "eos_basic_widgets.h"
#include "eos_event.h"
#include "eos_test.h"
#include "eos_version.h"
#include "eos_port.h"
#include "eos_swipe_panel.h"
#include "eos_service_display.h"
#include "eos_service_config.h"
#include "eos_service_lock.h"
#include "services/alarm/eos_service_alarm.h"
#include "eos_app.h"
#include "script_engine_core.h"
#include "spm.h"
#include "eos_watchface.h"
#include "eos_watchface_list.h"
#include "eos_app_list.h"
#ifdef EOS_USE_CUSTOM_LAUNCHER
#include "ui/launcher/eos_launcher.h"
#endif
#ifdef EOS_SIMULATOR
#include "eos_sim_hw_mock.h"
#endif
#include "eos_theme.h"
#include "eos_config.h"
#include "eos_config_internal.h"
#include "jerryscript.h"
#include "eos_font.h"
#include "eos_service_sensor.h"
#include "eos_service_cache.h"
#include "eos_dispatcher.h"
#include "eos_anim.h"
#include "eos_control_center.h"
#include "eos_cards_page.h"
#include "eos_chrome_manager.h"
#include "eos_service_audio.h"
#include "eos_service_storage.h"
#include "eos_service_state.h"
#include "eos_service_battery.h"
#include "eos_service_pm.h"
#include "eos_dfw.h"
#include "eos_app_header.h"
#include "eos_shell.h"
#include "services/plugin/eos_plugin_manager.h"
#include "eos_net_proxy.h"
#include "eos_net_wifi.h"
#include "eos_net_bt.h"
#include "eos_toast.h"
#include "eos_service_haptic.h"
#include "eos_crown.h"
#include "eos_service_permission.h"
#include "eos_overlay_layer.h"
#include "eos_bg_indicator.h"
#include "ui/wos/eos_wos.h"
#include "eos_icon.h"
#include "eos_activity.h"
#include "eos_std_widgets.h"
#include "eos_service_storage.h"
#include "ui/system/eos_boot_anim.h"
#define EOS_LOG_TAG "Core"
#include "eos_log.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/
static bool _is_inited = false;
static bool _pending_root_start = false;

/* Function Implementations -----------------------------------*/

static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";

static void _format_build_date(char *out, size_t size)
{
    char month_str[4];
    int day, year;

    sscanf(__DATE__, "%3s %d %d", month_str, &day, &year);

    int month = (strstr(months, month_str) - months) / 3 + 1;

    snprintf(out, size, "%04d-%02d-%02d", year, month, day);
}

static void _print_boot_info(void)
{
    char build_date[16];
    _format_build_date(build_date, sizeof(build_date));
    EOS_LOG_I("System initializing...");
    EOS_LOG_I("ElenixOS v" ELENIX_OS_VERSION_FULL);
    EOS_LOG_I("build: %s %s", build_date, __TIME__);
    EOS_LOG_I("build mode: %s", EOS_COMPILE_MODE == DEBUG ? "DEBUG" : "RELEASE");
}

static lv_indev_t *_get_key_indev()
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev)
    {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD)
        {
            return indev;
        }
        indev = lv_indev_get_next(indev);
    }
    EOS_LOG_W("Not found input device: key");
}

/* Invoked from an lv_timer context when the boot animation ends: only set a
 * flag here; the real work (activity controller init) happens in
 * eos_main_loop() to stay timer-callback-safe. */
static void _on_boot_anim_done(void)
{
    _pending_root_start = true;
}

void _sys_init_err_handler(const char *err_msg)
{
    EOS_LOG_E("System initialization failed: %s", err_msg);
    lv_obj_t *list = eos_std_info_create(lv_layer_sys(),
                                         EOS_COLOR_RED,
                                         RI_BUG_LINE,
                                         eos_lang_get_text(STR_ID_SYS_INIT_FAILED),
                                         eos_lang_get_text(STR_ID_SYS_INIT_FAILED_CONTENT));
    lv_obj_set_style_pad_top(list, 80, 0);
    char info_str[1024];
    snprintf(info_str, sizeof(info_str), "Error: %s", err_msg);
    lv_obj_t *err_label = eos_list_add_comment(list, info_str);
    (void)err_label;

    for (uint8_t i = 0; i < 3; ++i)
    {
        uint32_t delay = lv_timer_handler();
        eos_delay(delay);
    }
}

void eos_logo_play(bool anim)
{
    eos_display_set_brightness(EOS_DISPLAY_BRIGHTNESS_MAX, EOS_DISPLAY_DURATION_OFF, false);

    lv_obj_t *scr = lv_screen_active();

    if (!scr)
    {
        EOS_LOG_W("Active screen not found, creating a new screen for logo");
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_screen_load(scr);
    }

    // Create full screen container
    lv_obj_t *logo_container = lv_obj_create(scr);
    lv_obj_set_style_bg_color(logo_container, EOS_COLOR_BLACK, 0);
    lv_obj_set_size(logo_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(logo_container, 0, 0);
    lv_obj_move_foreground(logo_container);

    // Create LOGO image object
    lv_obj_t *logo_img = lv_image_create(logo_container);
    lv_image_set_src(logo_img, EOS_IMG_LOGO);
    lv_obj_center(logo_img);

    /* Render the splash now (the caller has finished font/theme init, so the
     * PNG decoder is fully ready and won't emit a spurious "Failed to open
     * image"). The logo screen is a transient boot splash that
     * eos_activity_controller_init() replaces with the Launcher. */
    lv_timer_handler();
}

void eos_init(void)
{
    /************************** Log system initialization **************************/
    eos_service_log_init();

    /************************** Image cache initialization **************************/
    eos_service_cache_init();

    _print_boot_info();
    /************************** System components initialization **************************/
    eos_service_storage_init();
    eos_logo_play(true);
    eos_lang_init();
    eos_dispatcher_init();
    eos_toast_init();
    eos_service_haptic_init();
    eos_crown_init();
    script_engine_init();
    spm_init();
    eos_service_config_init();
    eos_service_state_init();
    eos_service_permission_init();
    /* RTC 校对(须在 config 之后:依赖 Flash 备份;在 UI/App 之前:时间须先就绪) */
    eos_service_time_init();
#if EOS_SIMULATOR
    /* Register fake hardware devices (time/battery/power/sensors) BEFORE the
     * services that consume them, so they stop failing with "device OPS not
     * available" on the desktop simulator. */
    eos_sim_hw_mock_init();
#endif
    eos_service_battery_init();
    /* 内置 C 字体:编译进 Flash(XIP),不会失败,无需兜底卡死 */
    lv_font_t *default_font = eos_font_init();
    eos_theme_set(lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), default_font);
    eos_app_init();
    eos_watchface_init();
    /* Plugin Manager: scan the SD card and register any new .eapk/.ewpk
     * packages. Idempotent; a missing SD card is not an error. */
    eos_plugin_manager_init();
    eos_overlay_layer_init();
    eos_bg_indicator_init();
    eos_app_header_init();
    eos_wos_framework_init();
    eos_msg_list_init();
    eos_control_center_init();
    eos_cards_page_init();
    eos_chrome_manager_init();
    eos_service_pm_init();
    eos_service_audio_init();
    eos_service_lock_init();
    /* Persistent alarm trigger (Core): checks Alarm app config every 1s. */
    eos_service_alarm_init();

    /* Low-level shell (Core). Must init even if SD/apps are unavailable. */
    eos_shell_init();

    /* SOCKS5 client (Core system service). Loads proxy.* config. */
    eos_net_proxy_init();

    /* Wi-Fi + Bluetooth (Core system services). Load persisted config.
     * Note: eos_net_bt_set_enabled() may already have been invoked earlier by
     * eos_service_config_init() (it reads the `bluetooth` key and calls the
     * port hook), so the BT service uses lazy init to stay safe. */
    eos_net_wifi_init();
    eos_net_bt_init();

    /* The watchface (built-in clock) is the home/root activity. The adaptive
       Launcher is a SEPARATE app-list page entered from the watchface (crown /
       up-swipe / shortcut). EOS_USE_CUSTOM_LAUNCHER (simulator only) still
       selects the custom framework Home as that page (see eos_app_list_enter).
       The root activity is intentionally NOT started here: the boot animation
       must finish first, so the main UI is never visible while it runs. */
    if (!eos_watchface_get_activity())
        _sys_init_err_handler("Failed to get watchface activity");

    /* Boot self-test animation (timer-driven, never blocks the main loop).
     * Its completion callback only raises a flag; eos_main_loop() then starts
     * the activity controller (which deletes the Logo Screen and shows the
     * watchface), so the main UI appears only after the animation ends. */
    eos_boot_anim_start(_on_boot_anim_done);
    _is_inited = true;
}

bool eos_is_initialized(void)
{
    return _is_inited;
}

uint32_t eos_main_loop(void)
{
    if (!_is_inited)
    {
        EOS_LOG_E("System not initialized. Please call eos_init() before eos_main_loop().");
        return 0;
    }
    if (_pending_root_start)
    {
        _pending_root_start = false;
        /* Activity controller will automatically delete Logo Screen */
        eos_activity_t *root_activity = eos_watchface_get_activity();
        if (!root_activity)
            _sys_init_err_handler("Failed to get watchface activity");
        else if (eos_activity_controller_init(root_activity) != EOS_OK)
            _sys_init_err_handler("Failed to initialize activity controller");
    }
    eos_dispatch_tick();
    return lv_timer_handler();
}

uint32_t eos_tick_get(void)
{
    return lv_tick_get();
}
