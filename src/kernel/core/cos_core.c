/**
 * @file cos_core.c
 * @brief CantoMk6 OS core code implementation
 */

#include "cos_core.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_heap_caps.h" /* [DIAG] heap_caps_get_free_size */
#include "lvgl.h"
#include "cos_image.h"
#include "cos_msg_list.h"
#include "cos_lang.h"
#include "cos_basic_widgets.h"
#include "cos_event.h"
#include "cos_test.h"
#include "cos_version.h"
#include "cos_port.h"
#include "cos_swipe_panel.h"
#include "cos_service_display.h"
#include "cos_service_config.h"
#include "cos_service_cc_snapshot.h"
#include "cos_service_lock.h"
#include "services/alarm/cos_service_alarm.h"
#include "services/countdown/cos_service_countdown.h"
#include "cos_app.h"
#include "script_engine_core.h"
#include "spm.h"
#include "cos_watchface.h"
#include "cos_watchface_list.h"
#include "cos_app_list.h"
#ifdef COS_USE_CUSTOM_LAUNCHER
#include "ui/launcher/cos_launcher.h"
#endif
#if COS_SIMULATOR
#include "cos_sim_hw_mock.h"
#endif
#include "cos_theme.h"
#include "cos_config.h"
#include "cos_config_internal.h"
#include "jerryscript.h"
#include "cos_font.h"
#include "cos_service_sensor.h"
#include "cos_service_cache.h"
#include "cos_dispatcher.h"
#include "cos_anim.h"
#include "cos_control_center.h"
#include "cos_cards_page.h"
#include "cos_chrome_manager.h"
#include "cos_service_audio.h"
#include "cos_service_storage.h"
#include "cos_service_state.h"
#include "cos_service_battery.h"
#include "cos_service_pm.h"
#include "cos_service_power_save.h"
#include "cos_dfw.h"
#include "cos_app_header.h"
#include "cos_shell.h"
#include "services/plugin/cos_plugin_manager.h"
#include "cos_net_proxy.h"
#include "cos_net_wifi.h"
#include "cos_net_bt.h"
#include "cos_toast.h"
#include "cos_service_haptic.h"
#include "cos_crown.h"
#include "cos_service_permission.h"
#include "cos_overlay_layer.h"
#include "cos_bg_indicator.h"
#include "ui/wos/cos_wos.h"
#include "cos_icon.h"
#include "cos_activity.h"
#include "cos_std_widgets.h"
#include "cos_service_storage.h"
#include "ui/system/cos_boot_anim.h"
#define COS_LOG_TAG "Core"
#include "cos_log.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/
static bool _is_inited = false;
static bool _pending_root_start = false;
/* 深睡/待机唤醒后跳过开机动画(一次性):极简时钟 5 连击升级完整系统时由板级调用,
 * 使系统接近瞬时恢复(正常开机仍保留动画)。 */
static bool _boot_anim_skip = false;

/* Periodic memory report (real hardware only; disabled on simulator) */
#if !COS_SIMULATOR
static lv_timer_t *_mem_report_timer = NULL;
static uint32_t _mem_report_interval_sec = 0;
#define COS_MEM_REPORT_DEFAULT_SEC 5

static void _mem_report_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    COS_LOG_I("[MemReport] internal: free=%uB largest=%uB | psram: free=%uB largest=%uB | dma: free=%uB largest=%uB",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

void cos_mem_report_set_interval(uint32_t sec)
{
    _mem_report_interval_sec = sec;
    if (_mem_report_timer == NULL)
        return;
    if (sec == 0)
    {
        lv_timer_pause(_mem_report_timer);
    }
    else
    {
        lv_timer_set_period(_mem_report_timer, sec * 1000u);
        lv_timer_resume(_mem_report_timer);
    }
}

uint32_t cos_mem_report_get_interval(void)
{
    return _mem_report_interval_sec;
}
#endif /* !COS_SIMULATOR */

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
    COS_LOG_I("System initializing...");
    COS_LOG_I("CantoMk6 v" CANTOMK6_OS_VERSION_FULL);
    COS_LOG_I("build: %s %s", build_date, __TIME__);
    COS_LOG_I("build mode: %s", COS_COMPILE_MODE == DEBUG ? "DEBUG" : "RELEASE");
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
    COS_LOG_W("Not found input device: key");
}

/* Invoked from an lv_timer context when the boot animation ends: only set a
 * flag here; the real work (activity controller init) happens in
 * cos_main_loop() to stay timer-callback-safe. */
static void _on_boot_anim_done(void)
{
    _pending_root_start = true;
}

/* 请求跳过下一次开机动画(供深睡唤醒快速恢复;仅生效一次)。
 * 正常开机路径不调用,动画保留。 */
void cos_boot_anim_skip_request(void)
{
    _boot_anim_skip = true;
}

void _sys_init_err_handler(const char *err_msg)
{
    COS_LOG_E("System initialization failed: %s", err_msg);
    lv_obj_t *list = cos_std_info_create(lv_layer_sys(),
                                         COS_COLOR_RED,
                                         RI_BUG_LINE,
                                         cos_lang_get_text(STR_ID_SYS_INIT_FAILED),
                                         cos_lang_get_text(STR_ID_SYS_INIT_FAILED_CONTENT));
    lv_obj_set_style_pad_top(list, 80, 0);
    char info_str[1024];
    snprintf(info_str, sizeof(info_str), "Error: %s", err_msg);
    lv_obj_t *err_label = cos_list_add_comment(list, info_str);
    (void)err_label;

    for (uint8_t i = 0; i < 3; ++i)
    {
        uint32_t delay = lv_timer_handler();
        cos_delay(delay);
    }
}

void cos_logo_play(bool anim)
{
    cos_display_set_brightness(COS_DISPLAY_BRIGHTNESS_MAX, COS_DISPLAY_DURATION_OFF, false);

    lv_obj_t *scr = lv_screen_active();

    if (!scr)
    {
        COS_LOG_W("Active screen not found, creating a new screen for logo");
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_screen_load(scr);
    }

    // Create full screen container
    lv_obj_t *logo_container = lv_obj_create(scr);
    lv_obj_set_style_bg_color(logo_container, COS_COLOR_BLACK, 0);
    lv_obj_set_size(logo_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(logo_container, 0, 0);
    lv_obj_move_foreground(logo_container);

    // Create LOGO image object
    lv_obj_t *logo_img = lv_image_create(logo_container);
    lv_image_set_src(logo_img, COS_IMG_LOGO);
    lv_obj_center(logo_img);

    /* Render the splash now (the caller has finished font/theme init, so the
     * PNG decoder is fully ready and won't emit a spurious "Failed to open
     * image"). The logo screen is a transient boot splash that
     * cos_activity_controller_init() replaces with the Launcher. */
    lv_timer_handler();
}

void cos_init(void)
{
    /************************** Log system initialization **************************/
    cos_service_log_init();

    /************************** Image cache initialization **************************/
    cos_service_cache_init();

    _print_boot_info();
    /************************** System components initialization **************************/
    cos_service_storage_init();
    cos_logo_play(true);
    cos_lang_init();
    cos_dispatcher_init();
    cos_toast_init();
    cos_service_haptic_init();
    cos_crown_init();
    script_engine_init();
    COS_LOG_I("[DIAG] after script_engine: DMA free=%u largest=%u",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    spm_init();
    COS_LOG_I("[DIAG] after spm: DMA free=%u largest=%u",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    cos_service_config_init();
    /* 控制中心四项设置的 SD 快照(/sdcard/history/cc/settings.txt)。
     * 必须晚于 storage/config(读 cfg.json 需要),早于 power_save/beast_mode
     * 服务(它们进入模式时会往快照里写电源模式)。无真 SD 卡时本服务完全
     * 透明,所有读写都退化为空操作,仍以 cfg.json 为准。 */
    cos_cc_snapshot_init();
    cos_service_state_init();
    cos_service_permission_init();
    /* RTC 校对(须在 config 之后:依赖 Flash 备份;在 UI/App 之前:时间须先就绪) */
    cos_service_time_init();
#if COS_SIMULATOR
    /* Register fake hardware devices (time/battery/power/sensors) BEFORE the
     * services that consume them, so they stop failing with "device OPS not
     * available" on the desktop simulator. */
    cos_sim_hw_mock_init();
#endif
    cos_service_battery_init();
    /* Load radio preferences before Power Save snapshots them. This is also
     * necessary when a persisted power-save state must later restore Wi-Fi or
     * BLE to the user's pre-save choice. */
    cos_net_wifi_init();
    cos_net_bt_init();
    /* Must run before the control-center widget is created, otherwise a
     * persisted power-save state is not reflected by its switch at boot. */
    cos_service_power_save_init();
    /* Beast mode (性能模式, 与省电互斥):同样需在 control-center 创建前恢复状态 */
    cos_service_beast_mode_init();
    /* 内置 C 字体:编译进 Flash(XIP),不会失败,无需兜底卡死 */
    lv_font_t *default_font = cos_font_init();
    cos_theme_set(lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), default_font);
    cos_app_init();
    cos_watchface_init();
    /* Plugin Manager: scan the SD card and register any new .eapk/.ewpk
     * packages. Idempotent; a missing SD card is not an error. */
    cos_plugin_manager_init();
    cos_overlay_layer_init();
    cos_bg_indicator_init();
    cos_app_header_init();
    cos_wos_framework_init();
    cos_msg_list_init();
    cos_control_center_init();
    cos_cards_page_init();
    cos_chrome_manager_init();
    cos_service_pm_init();
    cos_service_audio_init();
    cos_service_lock_init();
    /* Persistent alarm trigger (Core): checks Alarm app config every 1s. */
    cos_service_alarm_init();
    /* Persistent countdown trigger (Core): relaunches Timer app when a RUN
     * task reaches its end timestamp (checks its config every 1s). */
    cos_service_countdown_init();

    /* Low-level shell (Core). Must init even if SD/apps are unavailable. */
    cos_shell_init();

#if !COS_SIMULATOR
    /* Periodic memory report (default every 5s, visible in `idf.py monitor`).
     * Real-hardware only; the desktop simulator has no heap_caps. */
    _mem_report_timer = lv_timer_create(_mem_report_tick_cb,
                                        COS_MEM_REPORT_DEFAULT_SEC * 1000u, NULL);
    _mem_report_interval_sec = COS_MEM_REPORT_DEFAULT_SEC;
#endif

    /* SOCKS5 client (Core system service). Loads proxy.* config. */
    cos_net_proxy_init();

    /* Wi-Fi + Bluetooth (Core system services). Load persisted config.
     * Note: cos_net_bt_set_enabled() may already have been invoked earlier by
     * cos_service_config_init() (it reads the `bluetooth` key and calls the
     * port hook), so the BT service uses lazy init to stay safe. */
    cos_net_wifi_init();
    cos_net_bt_init();

    /* The watchface (built-in clock) is the home/root activity. The adaptive
       Launcher is a SEPARATE app-list page entered from the watchface (crown /
       up-swipe / shortcut). COS_USE_CUSTOM_LAUNCHER (simulator only) still
       selects the custom framework Home as that page (see cos_app_list_enter).
       The root activity is intentionally NOT started here: the boot animation
       must finish first, so the main UI is never visible while it runs. */
    if (!cos_watchface_get_activity())
        _sys_init_err_handler("Failed to get watchface activity");

    /* Boot self-test animation (timer-driven, never blocks the main loop).
     * Its completion callback only raises a flag; cos_main_loop() then starts
     * the activity controller (which deletes the Logo Screen and shows the
     * watchface), so the main UI appears only after the animation ends.
     * 电池优化:待机(Deep Sleep)唤醒需快速恢复,按板级请求跳过动画直接启动 root。 */
    if (_boot_anim_skip)
    {
        _boot_anim_skip = false; /* 一次性 */
        _on_boot_anim_done();
    }
    else
    {
        cos_boot_anim_start(_on_boot_anim_done);
    }
    _is_inited = true;
}

bool cos_is_initialized(void)
{
    return _is_inited;
}

uint32_t cos_main_loop(void)
{
    if (!_is_inited)
    {
        COS_LOG_E("System not initialized. Please call cos_init() before cos_main_loop().");
        return 0;
    }
    if (_pending_root_start)
    {
        _pending_root_start = false;
        /* Activity controller will automatically delete Logo Screen */
        cos_activity_t *root_activity = cos_watchface_get_activity();
        if (!root_activity)
            _sys_init_err_handler("Failed to get watchface activity");
        else if (cos_activity_controller_init(root_activity) != COS_OK)
            _sys_init_err_handler("Failed to initialize activity controller");
    }
    cos_dispatch_tick();
    return lv_timer_handler();
}

uint32_t cos_tick_get(void)
{
    return lv_tick_get();
}
