/**
 * @file eos_shell.c
 * @brief ElenixOS low-level Shell implementation (Core subsystem)
 */

#include "eos_shell.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#include "eos_config.h"
#include "eos_log.h"
#include "eos_mem.h"
#include "eos_app.h"
#include "eos_app_list.h"
#include "eos_service_storage.h"
#include "eos_storage_paths.h"
#include "eos_version.h"
#include "eos_net_proxy.h"
#include "eos_net_wifi.h"
#include "eos_net_bt.h"
#include "eos_activity.h"
#include "spm.h"
#include "eos_service_config.h"
#include "eos_service_pm.h"
#include "ui/wos/eos_wos.h"
#include "ui/launcher/eos_launcher.h"
#include "services/plugin/eos_plugin_manager.h"
#include "services/ime/eos_pinyin.h"
#include "framework/watchface/eos_watchface.h"
#include "lvgl.h"

#define EOS_LOG_TAG "Shell"
#include "eos_log.h"

/* Macros and Definitions -------------------------------------*/
#define EOS_SHELL_LINE_MAX   256
#define EOS_SHELL_ARG_MAX    16

/* Variables --------------------------------------------------*/
static bool _inited = false;
static eos_shell_reboot_cb_t _reboot_cb = NULL;

/* Forward declarations --------------------------------------*/
typedef void (*eos_shell_cmd_fn_t)(eos_shell_output_cb_t out, void *user,
                                   int argc, char **argv);

static void cmd_help(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_version(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_mem(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_psram(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_flash(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_sd(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_rtc(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_display(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_touch(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_wifi(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_bt(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_proxy(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_apps(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_app(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_log(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_reboot(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_plugin(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_ime(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_launcher(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_power(eos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_wos(eos_shell_output_cb_t out, void *user, int argc, char **argv);

static void shell_log_cb(const char *line, void *user);

typedef struct
{
    const char *name;
    const char *help;
    eos_shell_cmd_fn_t fn;
} eos_shell_cmd_t;

/* Helpers ----------------------------------------------------*/

static void sh_out(eos_shell_output_cb_t out, void *user, const char *fmt, ...)
{
    if (!out)
        return;
    static char buf[EOS_SHELL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out(buf, user);
}

/* Command handlers ------------------------------------------*/

static void cmd_help(eos_shell_output_cb_t out, void *user, int argc, char **argv);

static const eos_shell_cmd_t s_cmds[] =
{
    {"help",    "list available commands",                 cmd_help},
    {"version", "show firmware version",                   cmd_version},
    {"ver",     "alias of version",                        cmd_version},
    {"mem",     "show memory usage",                       cmd_mem},
    {"heap",    "alias of mem",                            cmd_mem},
    {"psram",   "show PSRAM usage",                        cmd_psram},
    {"flash",   "show flash layout",                       cmd_flash},
    {"sd",      "show SD / storage status",                cmd_sd},
    {"rtc",     "show current date/time",                  cmd_rtc},
    {"display", "show display info",                       cmd_display},
    {"touch",   "show input devices",                      cmd_touch},
    {"wifi",    "wifi <status|enable|disable|scan|connect|disconnect|set>", cmd_wifi},
    {"bt",      "bt <status|enable|disable|scan|connect|disconnect|paired>", cmd_bt},
    {"proxy",   "show SOCKS5 proxy status",                cmd_proxy},
    {"apps",    "list installed apps",                    cmd_apps},
    {"app",     "app <list|start|stop|info|restart|disable> <id>", cmd_app},
    {"log",     "log [level <debug|info|warn|error>]",     cmd_log},
    {"reboot",  "reboot the system",                       cmd_reboot},
    {"plugin",  "plugin <scan [--force] | list>",          cmd_plugin},
    {"ime",     "ime <pinyin>  (pinyin -> Chinese)",       cmd_ime},
    {"launcher","launcher mode <v1|v2>  (switch launcher impl)", cmd_launcher},
    {"power",   "power <status|deep-sleep [sec]|wake>  (PM control)", cmd_power},
    {"wos",     "wos <list|open <id>|close|status|notify>  (WOS UI framework)", cmd_wos},
};

static void cmd_launcher(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "mode") == 0 && argc >= 3)
    {
        const char *m = argv[2];
        if (strcmp(m, "v1") != 0 && strcmp(m, "v2") != 0)
        {
            sh_out(out, user, "error: mode must be 'v1' or 'v2'");
            return;
        }
        eos_config_set_string(EOS_CONFIG_KEY_LAUNCHER_MODE, m);
        sh_out(out, user, "launcher mode -> %s (applies on next open)", m);
        /* If a Launcher page is currently shown, swap it live. */
        eos_activity_t *cur = eos_activity_get_current();
        if (cur && eos_activity_get_type(cur) == EOS_ACTIVITY_TYPE_APP_LIST)
        {
            eos_activity_back_to_watchface();
            eos_launcher_enter();
            sh_out(out, user, "  re-opened with %s", m);
        }
        return;
    }
    char *mode = eos_config_get_string(EOS_CONFIG_KEY_LAUNCHER_MODE, "v2");
    sh_out(out, user, "usage: launcher mode <v1|v2>");
    sh_out(out, user, "  current mode: %s", mode ? mode : "v2");
    if (mode) eos_free(mode);
}

static void cmd_help(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "ElenixOS Shell - available commands:");
    for (size_t i = 0; i < sizeof(s_cmds) / sizeof(s_cmds[0]); i++)
    {
        sh_out(out, user, "  %-8s %s", s_cmds[i].name, s_cmds[i].help);
    }
}

static void cmd_version(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "ElenixOS v" ELENIX_OS_VERSION_FULL);
    sh_out(out, user, "api level: %d", (int)ELENIX_OS_API_LEVEL);
}

static void cmd_mem(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[mem] heap accounting: not available on this simulator toolchain");
    sh_out(out, user, "  On real ESP32-S3 (FreeRTOS heap_caps) this reports:");
    sh_out(out, user, "    free / used / largest-free-block / minimum-free-heap");
}

static void cmd_psram(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[psram] N/A on simulator (PSRAM is an ESP32-S3 resource)");
    sh_out(out, user, "  On real hardware PSRAM holds LVGL buffers, image/animation caches,");
    sh_out(out, user, "  page data and plugin runtime objects.");
}

static void cmd_flash(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[flash] N/A on simulator (flash map is an ESP32-S3 resource)");
    sh_out(out, user, "  On real hardware: bootloader / partition / core / services / sys-res / reserved");
}

static void cmd_sd(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bool mounted = eos_storage_is_dir(EOS_SYS_ROOT_DIR);
    sh_out(out, user, "[sd] storage root: %s", EOS_SYS_ROOT_DIR);
    sh_out(out, user, "  mounted : %s", mounted ? "yes" : "no");
    sh_out(out, user, "  fs      : %s", "host/POSIX (simulator)");
    sh_out(out, user, "  capacity/free: N/A on simulator");
    sh_out(out, user, "  app dir : %s", EOS_APP_INSTALLED_DIR);
}

static void cmd_rtc(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    time_t now = time(NULL);
    struct tm t;
#if defined(_WIN32)
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    sh_out(out, user, "[rtc] %04d-%02d-%02d %02d:%02d:%02d (local)",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min, t.tm_sec);
}

static void cmd_power(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "usage: power <status|deep-sleep [sec]|wake>");
        return;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        const char *s = "unknown";
        switch (eos_pm_get_state())
        {
        case EOS_PM_DISPLAY_ON:  s = "DISPLAY_ON"; break;
        case EOS_PM_DISPLAY_AOD: s = "DISPLAY_AOD"; break;
        case EOS_PM_SLEEP:       s = "SLEEP"; break;
        case EOS_PM_DEEP_SLEEP:  s = "DEEP_SLEEP"; break;
        default: break;
        }
        sh_out(out, user, "[power] state : %s", s);
    }
    else if (strcmp(argv[1], "deep-sleep") == 0)
    {
        uint32_t sec = 0;
        if (argc >= 3)
            sec = (uint32_t)atoi(argv[2]);
        sh_out(out, user, "[power] deep-sleep requested (auto-wake=%u s)", (unsigned)sec);
        eos_pm_deep_sleep_request(sec);
    }
    else if (strcmp(argv[1], "wake") == 0)
    {
        eos_pm_wake_up();
        sh_out(out, user, "[power] wake requested");
    }
    else
    {
        sh_out(out, user, "usage: power <status|deep-sleep [sec]|wake>");
    }
}

static void cmd_wos(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "usage: wos <list|open <id>|close|status|notify <title> <body>>");
        return;
    }
    if (strcmp(argv[1], "list") == 0)
    {
        int n = wos_app_manager_registered_count();
        sh_out(out, user, "[wos] registered apps: %d", n);
        int i;
        for (i = 0; i < n; i++)
        {
            const eos_wos_app_desc_t *d = wos_app_manager_registered(i);
            if (d)
                sh_out(out, user, "  %s  (%s)", d->id, d->name ? d->name : "");
        }
    }
    else if (strcmp(argv[1], "open") == 0 && argc >= 3)
    {
        bool ok = wos_app_manager_open(argv[2]);
        sh_out(out, user, "[wos] open '%s' -> %s", argv[2], ok ? "OK" : "FAILED");
    }
    else if (strcmp(argv[1], "close") == 0)
    {
        wos_app_manager_close();
        sh_out(out, user, "[wos] close requested");
    }
    else if (strcmp(argv[1], "status") == 0)
    {
        const char *s = "IDLE";
        switch (wos_app_manager_get_state())
        {
        case WOS_APP_STATE_LAUNCHING: s = "LAUNCHING"; break;
        case WOS_APP_STATE_ACTIVE:    s = "ACTIVE"; break;
        case WOS_APP_STATE_CLOSING:   s = "CLOSING"; break;
        default: break;
        }
        sh_out(out, user, "[wos] state=%s active=%s", s, wos_app_manager_active_id());
    }
    else if (strcmp(argv[1], "notify") == 0)
    {
        const char *title = argc >= 3 ? argv[2] : "Notification";
        const char *body = argc >= 4 ? argv[3] : "test";
        wos_notification_show(title, body, 3000);
        sh_out(out, user, "[wos] notification shown");
    }
    else
    {
        sh_out(out, user, "usage: wos <list|open <id>|close|status|notify <title> <body>>");
    }
}

static void cmd_display(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    lv_display_t *d = lv_display_get_default();
    int w = EOS_DISPLAY_WIDTH;
    int h = EOS_DISPLAY_HEIGHT;
    if (d)
    {
        /* Prefer the live value; fall back to the configured constant. */
        int rw = lv_display_get_horizontal_resolution(d);
        int rh = lv_display_get_vertical_resolution(d);
        if (rw > 0)
            w = rw;
        if (rh > 0)
            h = rh;
    }
    sh_out(out, user, "[display] resolution : %dx%d", w, h);
    sh_out(out, user, "  shape       : round (1.28\" GC9A01 on real hardware)");
    sh_out(out, user, "  renderer    : LVGL %d.%d.%d",
           (int)LVGL_VERSION_MAJOR, (int)LVGL_VERSION_MINOR, (int)LVGL_VERSION_PATCH);
}

static void cmd_touch(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[touch] input devices:");
    int n = 0;
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev)
    {
        const char *type = "unknown";
        switch (lv_indev_get_type(indev))
        {
        case LV_INDEV_TYPE_POINTER: type = "pointer (touch/mouse)"; break;
        case LV_INDEV_TYPE_KEYPAD:  type = "keypad"; break;
        case LV_INDEV_TYPE_ENCODER: type = "encoder"; break;
        case LV_INDEV_TYPE_BUTTON:  type = "button"; break;
        case LV_INDEV_TYPE_NONE:    type = "none"; break;
        default: break;
        }
        sh_out(out, user, "  - %s", type);
        n++;
        indev = lv_indev_get_next(indev);
    }
    if (n == 0)
        sh_out(out, user, "  (none)");
}

static void cmd_wifi(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        /* default: status */
        sh_out(out, user, "[wifi] Wi-Fi service (Core system service)");
        sh_out(out, user, "  Enabled : %s", eos_net_wifi_is_enabled() ? "yes" : "no");
        sh_out(out, user, "  State   : %s", eos_net_wifi_state_str(eos_net_wifi_state()));
        char ip[EOS_NET_WIFI_IP_MAX];
        if (eos_net_wifi_get_ip(ip, sizeof(ip)) == EOS_OK)
            sh_out(out, user, "  IP      : %s", ip);
        else
            sh_out(out, user, "  IP      : (not connected)");
        sh_out(out, user, "  SSID    : %s", eos_net_wifi_connected_ssid()[0]
                                 ? eos_net_wifi_connected_ssid() : "(none)");
        sh_out(out, user, "  RSSI    : %d dBm", eos_net_wifi_rssi());
        return;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "status") == 0)
    {
        /* re-dispatch to default */
        cmd_wifi(out, user, 1, argv);
    }
    else if (strcmp(sub, "enable") == 0)
    {
        eos_net_wifi_set_enabled(true);
        sh_out(out, user, "[wifi] enabled");
    }
    else if (strcmp(sub, "disable") == 0)
    {
        eos_net_wifi_set_enabled(false);
        sh_out(out, user, "[wifi] disabled");
    }
    else if (strcmp(sub, "scan") == 0)
    {
        eos_wifi_ap_t aps[EOS_NET_WIFI_SCAN_MAX];
        uint32_t n = 0;
        eos_result_t r = eos_net_wifi_scan(aps, EOS_NET_WIFI_SCAN_MAX, &n);
        if (r != EOS_OK)
        {
            sh_out(out, user, "[wifi] scan failed: %d (radio enabled?)", r);
            return;
        }
        sh_out(out, user, "[wifi] found %u AP(s):", n);
        for (uint32_t i = 0; i < n; i++)
        {
            const char *sec = (aps[i].auth == EOS_WIFI_AUTH_OPEN) ? "open"
                             : (aps[i].auth == EOS_WIFI_AUTH_WPA3) ? "WPA3" : "WPA/WPA2";
            sh_out(out, user, "  %2u. %-16s rssi=%4d ch=%2u %s",
                   i, aps[i].ssid, aps[i].rssi, aps[i].channel, sec);
        }
    }
    else if (strcmp(sub, "connect") == 0)
    {
        const char *ssid = (argc >= 3) ? argv[2] : NULL;
        const char *pass = (argc >= 4) ? argv[3] : NULL;
        sh_out(out, user, "[wifi] connecting to %s ...", ssid ? ssid : "(saved)");
        eos_result_t r = eos_net_wifi_connect(ssid, pass);
        if (r == EOS_OK)
        {
            char ip[EOS_NET_WIFI_IP_MAX];
            eos_net_wifi_get_ip(ip, sizeof(ip));
            sh_out(out, user, "[wifi] connected: %s (IP %s, RSSI %d dBm)",
                   eos_net_wifi_connected_ssid(), ip, eos_net_wifi_rssi());
        }
        else
        {
            sh_out(out, user, "[wifi] connect failed: %d", r);
        }
    }
    else if (strcmp(sub, "disconnect") == 0)
    {
        eos_net_wifi_disconnect();
        sh_out(out, user, "[wifi] disconnected");
    }
    else if (strcmp(sub, "set") == 0)
    {
        if (argc < 4)
        {
            sh_out(out, user, "usage: wifi set <ssid|pass|auto> <value>");
            return;
        }
        const char *what = argv[2];
        const char *val = argv[3];
        if (strcmp(what, "ssid") == 0)
        {
            eos_net_wifi_set_credentials(val, NULL);
            sh_out(out, user, "[wifi] ssid = %s", val);
        }
        else if (strcmp(what, "pass") == 0)
        {
            eos_net_wifi_set_credentials(NULL, val);
            sh_out(out, user, "[wifi] password = **** (hidden, not logged)");
        }
        else if (strcmp(what, "auto") == 0)
        {
            bool a = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
            eos_config_set_bool(EOS_NET_WIFI_KEY_AUTO, a);
            sh_out(out, user, "[wifi] auto_connect = %s", a ? "on" : "off");
        }
        else
        {
            sh_out(out, user, "unknown field: %s (ssid|pass|auto)", what);
        }
    }
    else if (strcmp(sub, "save") == 0)
    {
        eos_net_wifi_save();
        sh_out(out, user, "[wifi] config saved (password stored silently)");
    }
    else
    {
        sh_out(out, user, "[wifi] unknown subcommand: %s", sub);
    }
}

static void cmd_bt(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "[bt] Bluetooth service (Core system service)");
        sh_out(out, user, "  Enabled : %s", eos_net_bt_is_enabled() ? "yes" : "no");
        sh_out(out, user, "  State   : %s", eos_net_bt_state_str(eos_net_bt_state()));
        char *n = eos_config_get_string(EOS_NET_BT_KEY_NAME, "ElenixOS");
        sh_out(out, user, "  Name    : %s", n ? n : "ElenixOS");
        if (n) eos_free(n);
        return;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "status") == 0)
    {
        cmd_bt(out, user, 1, argv);
    }
    else if (strcmp(sub, "enable") == 0)
    {
        /* routes through port hook -> eos_net_bt_set_enabled(true) */
        eos_bluetooth_enable();
        sh_out(out, user, "[bt] enabled");
    }
    else if (strcmp(sub, "disable") == 0)
    {
        eos_bluetooth_disable();
        sh_out(out, user, "[bt] disabled");
    }
    else if (strcmp(sub, "scan") == 0)
    {
        eos_bt_device_t devs[EOS_NET_BT_SCAN_MAX];
        uint32_t n = 0;
        eos_result_t r = eos_net_bt_scan(devs, EOS_NET_BT_SCAN_MAX, &n);
        if (r != EOS_OK)
        {
            sh_out(out, user, "[bt] scan failed: %d (radio enabled?)", r);
            return;
        }
        sh_out(out, user, "[bt] found %u device(s):", n);
        for (uint32_t i = 0; i < n; i++)
        {
            const char *t = (devs[i].type == EOS_BT_DEV_CLASSIC) ? "BR/EDR" : "LE";
            sh_out(out, user, "  %2u. %-16s %s rssi=%4d %s", i, devs[i].name,
                   devs[i].addr, devs[i].rssi, t);
        }
    }
    else if (strcmp(sub, "connect") == 0)
    {
        if (argc < 3)
        {
            sh_out(out, user, "usage: bt connect <addr>");
            return;
        }
        sh_out(out, user, "[bt] connecting to %s ...", argv[2]);
        eos_result_t r = eos_net_bt_connect(argv[2]);
        sh_out(out, user, r == EOS_OK ? "[bt] connected & paired" : "[bt] connect failed: %d");
    }
    else if (strcmp(sub, "disconnect") == 0)
    {
        eos_net_bt_disconnect();
        sh_out(out, user, "[bt] disconnected");
    }
    else if (strcmp(sub, "paired") == 0)
    {
        eos_bt_device_t devs[EOS_NET_BT_SCAN_MAX];
        uint32_t n = 0;
        eos_net_bt_get_paired(devs, EOS_NET_BT_SCAN_MAX, &n);
        sh_out(out, user, "[bt] paired %u device(s):", n);
        for (uint32_t i = 0; i < n; i++)
            sh_out(out, user, "  %2u. %-16s %s", i, devs[i].name, devs[i].addr);
    }
    else if (strcmp(sub, "save") == 0)
    {
        eos_net_bt_save();
        sh_out(out, user, "[bt] config saved");
    }
    else
    {
        sh_out(out, user, "[bt] unknown subcommand: %s", sub);
    }
}

static void _proxy_status(eos_shell_output_cb_t out, void *user)
{
    char srv[64];
    eos_net_proxy_server_str(srv, sizeof(srv));
    sh_out(out, user, "[proxy] SOCKS5 client (Core system service)");
    sh_out(out, user, "  Enabled : %s", eos_net_proxy_is_enabled() ? "yes" : "no");
    sh_out(out, user, "  Server  : %s", srv);
    sh_out(out, user, "  Auth    : %s", eos_net_proxy_auth_configured() ? "configured" : "none");
    sh_out(out, user, "  Status  : %s", eos_net_proxy_status_str(eos_net_proxy_status()));
    sh_out(out, user, "  (password is NEVER shown in shell output)");
}

static void cmd_proxy(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        _proxy_status(out, user);
        return;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "status") == 0)
    {
        _proxy_status(out, user);
    }
    else if (strcmp(sub, "enable") == 0)
    {
        eos_net_proxy_set_enabled(true);
        sh_out(out, user, "[proxy] enabled");
    }
    else if (strcmp(sub, "disable") == 0)
    {
        eos_net_proxy_set_enabled(false);
        sh_out(out, user, "[proxy] disabled");
    }
    else if (strcmp(sub, "set") == 0)
    {
        if (argc < 4)
        {
            sh_out(out, user, "usage: proxy set <host|port|user|pass> <value>");
            return;
        }
        const char *what = argv[2];
        const char *val = argv[3];
        if (strcmp(what, "host") == 0)
        {
            eos_net_proxy_set_server(val, eos_net_proxy_port());
            sh_out(out, user, "[proxy] host = %s", val);
        }
        else if (strcmp(what, "port") == 0)
        {
            int p = atoi(val);
            eos_net_proxy_set_server(eos_net_proxy_host(), (uint16_t)p);
            sh_out(out, user, "[proxy] port = %d", p);
        }
        else if (strcmp(what, "user") == 0)
        {
            eos_net_proxy_set_credentials(val, NULL);
            sh_out(out, user, "[proxy] username = %s", val);
        }
        else if (strcmp(what, "pass") == 0)
        {
            eos_net_proxy_set_credentials(NULL, val);
            sh_out(out, user, "[proxy] password = **** (hidden, not logged)");
        }
        else
        {
            sh_out(out, user, "unknown field: %s (use host|port|user|pass)", what);
        }
    }
    else if (strcmp(sub, "save") == 0)
    {
        eos_net_proxy_save();
        sh_out(out, user, "[proxy] config saved to system config");
    }
    else if (strcmp(sub, "connect") == 0)
    {
        if (argc < 4)
        {
            sh_out(out, user, "usage: proxy connect <host> <port>");
            return;
        }
        const char *th = argv[2];
        int tp = atoi(argv[3]);
        char srv[64];
        eos_net_proxy_server_str(srv, sizeof(srv));
        sh_out(out, user, "[proxy] dialing %s:%d via SOCKS5 %s ...", th, tp, srv);
        eos_net_proxy_tunnel_t *t = NULL;
        eos_result_t r = eos_net_proxy_dial(th, (uint16_t)tp, &t);
        if (r == EOS_OK)
        {
            sh_out(out, user, "[proxy] CONNECT success (SOCKS5 handshake OK)");
            eos_net_proxy_tunnel_close(t);
            sh_out(out, user, "[proxy] tunnel closed");
        }
        else
        {
            sh_out(out, user, "[proxy] CONNECT failed: %d", r);
        }
    }
    else
    {
        _proxy_status(out, user);
    }
}

static void cmd_apps(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t n = eos_app_get_installed();
    sh_out(out, user, "[apps] installed: %u", n);
    for (uint32_t i = 0; i < n; i++)
    {
        const char *id = eos_app_list_get_id(i);
        sh_out(out, user, "  %u: %s", i, id ? id : "(null)");
    }
    if (n == 0)
        sh_out(out, user, "  (no apps installed)");
}

static void cmd_app(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "usage: app <list|start|stop|restart|info|install|uninstall|disable|enable> <id|path>");
        return;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "list") == 0)
    {
        cmd_apps(out, user, 0, NULL);
        return;
    }

    if (argc < 3)
    {
        sh_out(out, user, "app %s requires an <id>", sub);
        return;
    }
    const char *id = argv[2];

    if (strcmp(sub, "info") == 0)
    {
        bool installed = eos_app_list_contains(id);
        sh_out(out, user, "[app:%s] installed: %s", id, installed ? "yes" : "no");
        char path[EOS_SHELL_LINE_MAX];
        snprintf(path, sizeof(path), "%s%s/manifest.json", EOS_APP_INSTALLED_DIR, id);
        cJSON *manifest = eos_storage_json_load(path);
        if (manifest)
        {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(manifest, "name");
            cJSON *ver = cJSON_GetObjectItemCaseSensitive(manifest, "version");
            cJSON *appId = cJSON_GetObjectItemCaseSensitive(manifest, "appId");
            if (appId && cJSON_IsString(appId))
                sh_out(out, user, "  appId  : %s", appId->valuestring);
            if (name && cJSON_IsString(name))
                sh_out(out, user, "  name   : %s", name->valuestring);
            if (ver && cJSON_IsString(ver))
                sh_out(out, user, "  version: %s", ver->valuestring);
            cJSON_Delete(manifest);
        }
        else
        {
            sh_out(out, user, "  manifest: not found (%s)", path);
        }
        return;
    }

    if (strcmp(sub, "start") == 0)
    {
        if (!eos_app_list_contains(id))
        {
            sh_out(out, user, "app '%s' is not installed", id);
            return;
        }
        if (eos_app_is_disabled(id))
        {
            sh_out(out, user, "app '%s' is disabled (use: app enable %s)", id, id);
            return;
        }
        eos_result_t r = eos_app_launch_immediately(id);
        sh_out(out, user, "app '%s' launch requested (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "install") == 0)
    {
        /* Real install path: scan .eapk -> parse manifest -> unpack ->
         * register with the Plugin Manager -> refresh list -> post event. */
        eos_result_t r = eos_app_install(id);
        sh_out(out, user, "app install '%s' (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "stop") == 0 || strcmp(sub, "restart") == 0)
    {
        /* Release the running foreground app. eos_activity_back() pops the
         * app activity, whose on_destroy calls spm_app_stop() to free the
         * JerryScript realm + SNI context + timers. If a transition is still
         * mid-flight (e.g. headless --shell-test with no render loop), fall
         * back to stopping the script program directly so resources are freed. */
        eos_result_t r = eos_activity_back();
        if (r != EOS_OK)
        {
            spm_app_stop();
            r = EOS_OK;
        }
        sh_out(out, user, "app '%s' stop (result=%d)", id, (int)r);
        if (strcmp(sub, "restart") == 0)
        {
            eos_result_t r2 = eos_app_launch_immediately(id);
            sh_out(out, user, "app '%s' restart (result=%d)", id, (int)r2);
        }
        return;
    }

    if (strcmp(sub, "disable") == 0)
    {
        eos_result_t r = eos_app_disable(id);
        sh_out(out, user, "app '%s' disable (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "enable") == 0)
    {
        eos_result_t r = eos_app_enable(id);
        sh_out(out, user, "app '%s' enable (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "uninstall") == 0)
    {
        if (!eos_app_list_contains(id))
        {
            sh_out(out, user, "app '%s' is not installed", id);
            return;
        }
        eos_result_t r = eos_app_uninstall(id);
        sh_out(out, user, "app '%s' uninstall (result=%d)", id, (int)r);
        return;
    }

    sh_out(out, user, "unknown app subcommand: %s", sub);
}

static void cmd_log(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "level") == 0)
    {
        const char *lv = argv[2];
        eos_log_level_t lvl = EOS_LOG_LEVEL_DEBUG;
        if (strcmp(lv, "debug") == 0)       lvl = EOS_LOG_LEVEL_DEBUG;
        else if (strcmp(lv, "info") == 0)   lvl = EOS_LOG_LEVEL_INFO;
        else if (strcmp(lv, "warn") == 0)   lvl = EOS_LOG_LEVEL_WARN;
        else if (strcmp(lv, "error") == 0)  lvl = EOS_LOG_LEVEL_ERROR;
        else
        {
            sh_out(out, user, "unknown level '%s' (debug|info|warn|error)", lv);
            return;
        }
        eos_log_set_min_level(lvl);
        sh_out(out, user, "log level set to %s", lv);
        return;
    }
    sh_out(out, user, "[log] current min level: %d (0=debug 1=info 2=warn 3=error)",
           (int)eos_log_get_min_level());
}

static void cmd_reboot(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[reboot] reboot requested.");
    if (_reboot_cb)
    {
        sh_out(out, user, "  invoking reboot hook...");
        _reboot_cb();
    }
    else
    {
        sh_out(out, user, "  (no reboot hook on this platform - simulator cannot reboot)");
    }
}

/* Dispatcher -------------------------------------------------*/

int eos_shell_exec(const char *cmdline, eos_shell_output_cb_t out, void *user)
{
    if (!_inited)
    {
        if (out)
            out("[shell] not initialized", user);
        return -1;
    }
    if (!cmdline)
        return -1;

    /* Copy into a mutable buffer for tokenization. */
    char buf[EOS_SHELL_LINE_MAX];
    strncpy(buf, cmdline, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Trim leading/trailing whitespace. */
    char *p = buf;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p == '\0')
        return 0; /* empty line */

    char *argv[EOS_SHELL_ARG_MAX];
    int argc = 0;
    char *tok = strtok(p, " \t\r\n");
    while (tok && argc < EOS_SHELL_ARG_MAX)
    {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    if (argc == 0)
        return 0;

    for (size_t i = 0; i < sizeof(s_cmds) / sizeof(s_cmds[0]); i++)
    {
        if (strcmp(s_cmds[i].name, argv[0]) == 0)
        {
            s_cmds[i].fn(out, user, argc, argv);
            return 0;
        }
    }

    sh_out(out, user, "unknown command: %s (type 'help')", argv[0]);
    return -1;
}

/* Public API ------------------------------------------------*/

void eos_shell_init(void)
{
    _inited = true;
}

void eos_shell_deinit(void)
{
    _inited = false;
    _reboot_cb = NULL;
}

void eos_shell_set_reboot_cb(eos_shell_reboot_cb_t cb)
{
    _reboot_cb = cb;
}

void eos_shell_exec_log(const char *cmdline)
{
    eos_shell_exec(cmdline, shell_log_cb, NULL);
}

static void cmd_plugin(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "usage: plugin <scan [--force] | list>");
        return;
    }
    const char *sub = argv[1];
    if (strcmp(sub, "scan") == 0)
    {
        bool force = (argc >= 3 && strcmp(argv[2], "--force") == 0);
        eos_plugin_scan_result_t res;
        eos_plugin_manager_scan(force, &res);
        sh_out(out, user, "plugin scan: scanned=%u installed=%u skipped=%u failed=%u",
               res.scanned, res.installed, res.skipped, res.failed);
        return;
    }
    if (strcmp(sub, "list") == 0)
    {
        uint32_t n = eos_app_get_installed();
        sh_out(out, user, "installed apps: %u", n);
        for (uint32_t i = 0; i < n; i++)
        {
            const char *id = eos_app_list_get_id(i);
            if (!id)
                continue;
            const char *name = eos_app_get_name(id);
            bool dis = eos_app_is_disabled(id);
            sh_out(out, user, "  %s  (%s)%s", id, name ? name : "?",
                  dis ? " [disabled]" : "");
        }
        size_t wn = eos_watchface_list_size();
        sh_out(out, user, "installed watchfaces: %u", (unsigned)wn);
        for (size_t i = 0; i < wn; i++)
        {
            const char *id = eos_watchface_list_get_id(i);
            if (id)
                sh_out(out, user, "  %s", id);
        }
        return;
    }
    sh_out(out, user, "unknown subcommand: %s", sub);
}

static void cmd_ime(eos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "usage: ime <pinyin>   (e.g. ime ni)");
        return;
    }
    const char *py = argv[1];
    const char *cands[16];
    int n = 0;
    int total = eos_pinyin_lookup(py, cands, 16, &n);
    if (n == 0)
    {
        sh_out(out, user, "ime '%s': no candidates", py);
        return;
    }
    sh_out(out, user, "ime '%s': %d candidate(s):", py, total);
    for (int i = 0; i < n; i++)
        sh_out(out, user, "  %d. %s", i + 1, cands[i]);
}

/* log trampoline (kept at bottom to avoid forward-decl noise) */
static void shell_log_cb(const char *line, void *user)
{
    (void)user;
    EOS_LOG_I("%s", line);
}
