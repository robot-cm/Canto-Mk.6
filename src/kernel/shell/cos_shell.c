/**
 * @file cos_shell.c
 * @brief Canto Mk.6 low-level Shell implementation (Core subsystem)
 */

#include "cos_shell.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#include "cos_config.h"
#include "cos_log.h"
#include "cos_mem.h"
#include "cos_core.h"
#include "cos_app.h"
#include "cos_app_list.h"
#include "cos_service_storage.h"
#include "cos_storage_paths.h"
#include "cos_version.h"
#include "cos_net_proxy.h"
#include "cos_net_wifi.h"
#include "cos_net_bt.h"
#include "cos_activity.h"
#include "spm.h"
#include "cos_service_config.h"
#include "cos_service_time.h"
#include "cos_service_pm.h"
#include "cos_service_power_save.h"
#include "cos_service_beast_mode.h"
#include "ui/wos/cos_wos.h"
#include "ui/launcher/cos_launcher.h"
#include "services/plugin/cos_plugin_manager.h"
#include "services/ime/cos_pinyin.h"
#include "framework/watchface/cos_watchface.h"
#include "cos_service_display.h"
#include "cos_service_cc_snapshot.h" /* cc:控制中心设置快照诊断 */
#include "lvgl.h"
#ifdef COS_PLATFORM_ESP32
#include "esp_spiffs.h"    /* cmd_sd: SPIFFS 兜底时的容量统计 */
#include "esp_pm.h"        /* power status: DFS / Light-sleep 诊断 */
#include "esp_heap_caps.h" /* cmd_prof: heap_caps 内存池统计 */
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" /* cmd_prof: uxTaskGetSystemState 采样 */
#endif

#define COS_LOG_TAG "Shell"
#include "cos_log.h"

/* Macros and Definitions -------------------------------------*/
#define COS_SHELL_LINE_MAX   256
#define COS_SHELL_ARG_MAX    16

/* Variables --------------------------------------------------*/
static bool _inited = false;
static cos_shell_reboot_cb_t _reboot_cb = NULL;

/* Forward declarations --------------------------------------*/
typedef void (*cos_shell_cmd_fn_t)(cos_shell_output_cb_t out, void *user,
                                   int argc, char **argv);

static void cmd_help(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_version(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_mem(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_memlog(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_psram(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_flash(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_sd(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_rtc(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_time(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_display(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_touch(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_wifi(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_bt(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_proxy(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_apps(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_app(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_log(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_reboot(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_plugin(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_ime(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_launcher(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_power(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_wos(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_prof(cos_shell_output_cb_t out, void *user, int argc, char **argv);
static void cmd_cc(cos_shell_output_cb_t out, void *user, int argc, char **argv);

static void shell_log_cb(const char *line, void *user);

typedef struct
{
    const char *name;
    const char *help;
    cos_shell_cmd_fn_t fn;
} cos_shell_cmd_t;

/* Helpers ----------------------------------------------------*/

static void sh_out(cos_shell_output_cb_t out, void *user, const char *fmt, ...)
{
    if (!out)
        return;
    static char buf[COS_SHELL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out(buf, user);
}

/* Command handlers ------------------------------------------*/

static void cmd_help(cos_shell_output_cb_t out, void *user, int argc, char **argv);

static const cos_shell_cmd_t s_cmds[] =
{
    {"help",    "list available commands",                 cmd_help},
    {"version", "show firmware version",                   cmd_version},
    {"ver",     "alias of version",                        cmd_version},
    {"mem",     "show memory usage",                       cmd_mem},
    {"heap",    "alias of mem",                            cmd_mem},
    {"memlog",  "memlog [<sec>|off]  periodic mem report", cmd_memlog},
    {"psram",   "show PSRAM usage",                        cmd_psram},
    {"flash",   "show flash layout",                       cmd_flash},
    {"sd",      "show SD / storage status",                cmd_sd},
    {"rtc",     "show RTC device + system time status",    cmd_rtc},
    {"time",    "time | time set <YYYY-MM-DD HH:MM:SS> | time unix <sec> | time ntp", cmd_time},
    {"display", "display [brightness <0-100> | bltest <0|1>]  (info / A/B test)", cmd_display},
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
    {"power",   "power <status|deep-sleep [sec]|standby|wake>  (PM control)", cmd_power},
    {"wos",     "wos <list|open <id>|close|status|notify>  (WOS UI framework)", cmd_wos},
    {"prof",    "prof - show resource utilization (SRAM/PSRAM/DMA/CPU)", cmd_prof},
    {"cc",      "cc <show|save|load>  (/sdcard/history/cc settings snapshot)", cmd_cc},
};

static void cmd_launcher(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "mode") == 0 && argc >= 3)
    {
        const char *m = argv[2];
        if (strcmp(m, "v1") != 0 && strcmp(m, "v2") != 0)
        {
            sh_out(out, user, "error: mode must be 'v1' or 'v2'");
            return;
        }
        cos_config_set_string(COS_CONFIG_KEY_LAUNCHER_MODE, m);
        sh_out(out, user, "launcher mode -> %s (applies on next open)", m);
        /* If a Launcher page is currently shown, swap it live. */
        cos_activity_t *cur = cos_activity_get_current();
        if (cur && cos_activity_get_type(cur) == COS_ACTIVITY_TYPE_APP_LIST)
        {
            cos_activity_back_to_watchface();
            cos_launcher_enter();
            sh_out(out, user, "  re-opened with %s", m);
        }
        return;
    }
    char *mode = cos_config_get_string(COS_CONFIG_KEY_LAUNCHER_MODE, "v2");
    sh_out(out, user, "usage: launcher mode <v1|v2>");
    sh_out(out, user, "  current mode: %s", mode ? mode : "v2");
    if (mode) cos_free(mode);
}

static void cmd_help(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "CantoMk6 Shell - available commands:");
    for (size_t i = 0; i < sizeof(s_cmds) / sizeof(s_cmds[0]); i++)
    {
        sh_out(out, user, "  %-8s %s", s_cmds[i].name, s_cmds[i].help);
    }
}

static void cmd_version(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "CantoMk6 v" CANTOMK6_OS_VERSION_FULL);
    sh_out(out, user, "api level: %d", (int)CANTOMK6_OS_API_LEVEL);
}

static void cmd_mem(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[mem] heap accounting: not available on this simulator toolchain");
    sh_out(out, user, "  On real ESP32-S3 (FreeRTOS heap_caps) this reports:");
    sh_out(out, user, "    free / used / largest-free-block / minimum-free-heap");
}

static void cmd_psram(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[psram] N/A on simulator (PSRAM is an ESP32-S3 resource)");
    sh_out(out, user, "  On real hardware PSRAM holds LVGL buffers, image/animation caches,");
    sh_out(out, user, "  page data and plugin runtime objects.");
}

/* cc - control-center settings snapshot on the SD card
 * (/sdcard/history/cc/settings.txt: brightness / bt / wifi / power mode).
 * Useful to verify what deep sleep will restore, and to force a save/load. */
static void cmd_cc(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    const char *sub = (argc > 1 && argv[1]) ? argv[1] : "show";

    if (strcmp(sub, "show") == 0)
    {
        char buf[320];
        cos_cc_snapshot_dump(buf, sizeof(buf));
        /* dump() is multi-line: emit it line by line through the shell sink */
        char *save = NULL;
        for (char *line = strtok_r(buf, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save))
        {
            sh_out(out, user, "%s", line);
        }
        return;
    }
    if (strcmp(sub, "save") == 0)
    {
        cos_result_t r = cos_cc_snapshot_capture_now();
        sh_out(out, user, "cc snapshot save: %s", r == COS_OK ? "ok" : "failed");
        return;
    }
    if (strcmp(sub, "load") == 0)
    {
        cos_result_t r = cos_cc_snapshot_restore_now();
        sh_out(out, user, "cc snapshot load: %s", r == COS_OK ? "ok" : "no snapshot");
        return;
    }
    sh_out(out, user, "usage: cc <show|save|load>");
}

/* prof - simple performance monitor: SRAM / PSRAM / DMA utilization and
 * dual-core CPU usage sampled from FreeRTOS run-time stats over 100ms. */
static void cmd_prof(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
#ifdef COS_PLATFORM_ESP32
    /* ---- memory pools ---- */
    static const struct
    {
        const char *name;
        uint32_t caps;
    } pools[] = {
        {"SRAM", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT},
        {"PSRAM", MALLOC_CAP_SPIRAM},
        {"DMA", MALLOC_CAP_DMA},
    };
    size_t i;
    for (i = 0; i < sizeof(pools) / sizeof(pools[0]); i++)
    {
        size_t total = heap_caps_get_total_size(pools[i].caps);
        size_t free_b = heap_caps_get_free_size(pools[i].caps);
        size_t used = total - free_b;
        unsigned pct = total ? (unsigned)((used * 1000u) / total) : 0u;
        sh_out(out, user, "  %-5s: used=%uKB (%u.%u%%)  free=%uKB  largest=%uKB",
               pools[i].name,
               (unsigned)(used / 1024u), pct / 10u, pct % 10u,
               (unsigned)(free_b / 1024u),
               (unsigned)(heap_caps_get_largest_free_block(pools[i].caps) / 1024u));
    }
    size_t min_int = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t min_psr = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    sh_out(out, user, "  min-free (all-time low): SRAM=%uKB PSRAM=%uKB",
           (unsigned)(min_int / 1024u), (unsigned)(min_psr / 1024u));

    /* ---- CPU: FreeRTOS run-time stats over a 100ms window ---- */
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0)
    {
        return;
    }
    TaskStatus_t *arr = (TaskStatus_t *)cos_malloc(n * sizeof(TaskStatus_t));
    if (!arr)
    {
        sh_out(out, user, "  CPU: sample failed (no memory)");
        return;
    }
    uint32_t *run_a = (uint32_t *)cos_malloc(n * sizeof(uint32_t));
    if (!run_a)
    {
        cos_free(arr);
        sh_out(out, user, "  CPU: sample failed (no memory)");
        return;
    }

    uint32_t total_a = 0, total_b = 0;
    UBaseType_t cap = n; /* array capacity = initial task count */
    n = uxTaskGetSystemState(arr, cap, &total_a);
    for (UBaseType_t k = 0; k < n; k++)
    {
        run_a[k] = arr[k].ulRunTimeCounter;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    UBaseType_t n2 = uxTaskGetSystemState(arr, cap, &total_b);
    if (n2 < n)
    {
        n = n2; /* a task exited during the window; only compare common indices */
    }
    /* Run-time counter is uint32_t (CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U32)
     * and ticks at 1MHz via ESP timer -> overflows after ~71min. Compute the
     * delta in 64-bit with wrap-around compensation so dt never goes negative
     * or explodes into thousands of percent. */
    uint64_t dt = (uint64_t)total_b - (uint64_t)total_a;
    if (total_b < total_a)
    {
        dt += (uint64_t)1u << 32; /* counter wrapped between samples */
    }
    if (dt == 0)
    {
        sh_out(out, user, "  CPU: run-time counter not ticking");
        cos_free(run_a);
        cos_free(arr);
        return;
    }

    /* total busy = 100% - combined idle (one IDLE task per core) */
    uint64_t idle_run = 0;
    for (UBaseType_t k = 0; k < n; k++)
    {
        if (strncmp(arr[k].pcTaskName, "IDLE", 4) == 0)
        {
            uint64_t dr = (uint64_t)arr[k].ulRunTimeCounter - (uint64_t)run_a[k];
            if (arr[k].ulRunTimeCounter < run_a[k])
            {
                dr += (uint64_t)1u << 32; /* per-task wrap */
            }
            idle_run += dr;
        }
    }
    uint32_t busy = (uint32_t)(1000u - (idle_run * 1000u) / dt);

    sh_out(out, user, "  CPU  : %u.%u%% busy  (dual-core, %u tasks, 100ms window)",
           busy / 10u, busy % 10u, (unsigned)n2);

    sh_out(out, user, "  top tasks:");
    UBaseType_t shown = 0;
    while (shown < n && shown < 8)
    {
        UBaseType_t best = 0;
        uint64_t best_dr = 0;
        for (UBaseType_t k = 0; k < n; k++)
        {
            uint64_t dr = (uint64_t)arr[k].ulRunTimeCounter - (uint64_t)run_a[k];
            if (arr[k].ulRunTimeCounter < run_a[k])
            {
                dr += (uint64_t)1u << 32; /* per-task wrap */
            }
            if (dr > best_dr)
            {
                best_dr = dr;
                best = k;
            }
        }
        if (best_dr == 0)
        {
            break;
        }
        uint32_t pct = (uint32_t)((best_dr * 1000u) / dt);
        sh_out(out, user, "    %-12s %u.%u%%", arr[best].pcTaskName, pct / 10u, pct % 10u);
        run_a[best] = arr[best].ulRunTimeCounter; /* exclude from next pick */
        shown++;
    }

    cos_free(run_a);
    cos_free(arr);
#else
    sh_out(out, user, "[prof] performance monitor: N/A on simulator toolchain");
    sh_out(out, user, "  On real ESP32-S3 this reports SRAM / PSRAM / DMA usage and");
    sh_out(out, user, "  dual-core CPU utilization (100ms run-time-stats sample).");
#endif
}

static void cmd_memlog(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
#if COS_SIMULATOR
    (void)argc;
    (void)argv;
    sh_out(out, user, "[memlog] periodic memory report is only available on real ESP32-S3");
    sh_out(out, user, "  (the desktop simulator has no heap_caps backend)");
#else
    if (argc >= 2)
    {
        if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0)
        {
            cos_mem_report_set_interval(0);
            sh_out(out, user, "[memlog] periodic memory report disabled");
            return;
        }
        char *end = NULL;
        long sec = strtol(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || sec <= 0 || sec > 3600)
        {
            sh_out(out, user, "usage: memlog <seconds>   (1-3600, or 'off' to disable)");
            return;
        }
        cos_mem_report_set_interval((uint32_t)sec);
        sh_out(out, user, "[memlog] report every %lds (see serial monitor: [MemReport])", sec);
        return;
    }
    uint32_t cur = cos_mem_report_get_interval();
    if (cur == 0)
        sh_out(out, user, "[memlog] periodic memory report: disabled");
    else
    {
        sh_out(out, user, "[memlog] periodic memory report: enabled");
        sh_out(out, user, "  interval: %us (see serial monitor: [MemReport])", (unsigned)cur);
    }
    sh_out(out, user, "  usage: memlog <seconds> | memlog off");
#endif
}

static void cmd_flash(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sh_out(out, user, "[flash] N/A on simulator (flash map is an ESP32-S3 resource)");
    sh_out(out, user, "  On real hardware: bootloader / partition / core / services / sys-res / reserved");
}

static void cmd_sd(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bool mounted = cos_storage_is_dir(COS_SYS_ROOT_DIR);
    sh_out(out, user, "[sd] storage root: %s", COS_SYS_ROOT_DIR);
    sh_out(out, user, "  mounted : %s", mounted ? "yes" : "no");
#ifdef COS_PLATFORM_ESP32
    {
        /* 真机:SDSPI 真 SD 优先挂载,无卡回退 SPIFFS(见 port/esp32s3/main.c) */
        bool spiffs_fallback = esp_spiffs_mounted("spiffs");
        sh_out(out, user, "  fs      : %s", spiffs_fallback ? "SPIFFS (fallback, no SD)"
                                                           : "SD/FAT (real microSD)");
        if (spiffs_fallback)
        {
            size_t total = 0, used = 0;
            esp_spiffs_info("spiffs", &total, &used);
            sh_out(out, user, "  capacity: %u KB", (unsigned)(total / 1024));
            sh_out(out, user, "  free    : %u KB", (unsigned)((total - used) / 1024));
        }
        else
        {
            sh_out(out, user, "  capacity: see boot log (SD card info printed on mount)");
        }
    }
#else
    sh_out(out, user, "  fs      : %s", "host/POSIX (simulator)");
    sh_out(out, user, "  capacity/free: N/A on simulator");
#endif
    sh_out(out, user, "  app dir : %s", COS_APP_INSTALLED_DIR);
    sh_out(out, user, "  eapk src: /sdcard/apps (scanned at boot, auto install)");
}

static const char *_time_source_name(cos_time_source_t s)
{
    switch (s)
    {
    case COS_TIME_SOURCE_RTC:     return "RTC";
    case COS_TIME_SOURCE_BACKUP:  return "backup";
    case COS_TIME_SOURCE_COMPILE: return "build-time";
    default:                      return "none";
    }
}

static void cmd_rtc(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    cos_datetime_t now = cos_time_get();
    cos_datetime_t rtc = cos_time_get_rtc();
    sh_out(out, user, "[time] %04d-%02d-%02d %02d:%02d:%02d (system, source=%s)",
           now.year, now.month, now.day, now.hour, now.min, now.sec,
           _time_source_name(cos_time_get_source()));
    sh_out(out, user, "[rtc ] %04d-%02d-%02d %02d:%02d:%02d (BM8563 raw)",
           rtc.year, rtc.month, rtc.day, rtc.hour, rtc.min, rtc.sec);
    if (rtc.year == 0) {
        sh_out(out, user, "       RTC invalid (VL/power-loss) or I2C not ready");
    }
}

static void cmd_time(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        cos_datetime_t now = cos_time_get();
        sh_out(out, user, "%04d-%02d-%02d %02d:%02d:%02d (system, source=%s)",
               now.year, now.month, now.day, now.hour, now.min, now.sec,
               _time_source_name(cos_time_get_source()));
        sh_out(out, user, "usage: time | time set <YYYY-MM-DD HH:MM:SS> | time unix <sec> | time ntp");
        return;
    }

    if (strcmp(argv[1], "set") == 0)
    {
        if (argc < 4)
        {
            sh_out(out, user, "usage: time set <YYYY-MM-DD HH:MM:SS>");
            return;
        }
        cos_datetime_t dt;
        memset(&dt, 0, sizeof(dt));
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if (sscanf(argv[2], "%d-%d-%d", &y, &mo, &d) != 3 ||
            sscanf(argv[3], "%d:%d:%d", &h, &mi, &s) != 3)
        {
            sh_out(out, user, "error: expected YYYY-MM-DD HH:MM:SS");
            return;
        }
        dt.year  = (uint16_t)y;
        dt.month = (uint8_t)mo;
        dt.day   = (uint8_t)d;
        dt.hour  = (uint8_t)h;
        dt.min   = (uint8_t)mi;
        dt.sec   = (uint8_t)s;
        if (cos_time_set(dt) == COS_OK)
        {
            sh_out(out, user, "time set OK (RTC + backup + libc)");
        }
        else
        {
            sh_out(out, user, "time set FAILED (invalid value)");
        }
        return;
    }

    if (strcmp(argv[1], "unix") == 0)
    {
        if (argc < 3)
        {
            sh_out(out, user, "usage: time unix <sec>");
            return;
        }
        uint32_t ts = (uint32_t)strtoul(argv[2], NULL, 10);
        if (cos_time_set_unix(ts) == COS_OK)
        {
            sh_out(out, user, "time set OK (unix %u)", (unsigned)ts);
        }
        else
        {
            sh_out(out, user, "time set FAILED");
        }
        return;
    }

    if (strcmp(argv[1], "ntp") == 0)
    {
#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
        cos_time_ntp_force_sync();
        sh_out(out, user, "time: NTP re-sync triggered (watch serial log)");
#else
        sh_out(out, user, "time: NTP not available on simulator");
#endif
        return;
    }

    sh_out(out, user, "usage: time | time set <YYYY-MM-DD HH:MM:SS> | time unix <sec> | time ntp");
}

static void cmd_power(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "usage: power <status|deep-sleep [sec]|wake>");
        return;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        const char *s = "unknown";
        switch (cos_pm_get_state())
        {
        case COS_PM_DISPLAY_ON:  s = "DISPLAY_ON"; break;
        case COS_PM_DISPLAY_AOD: s = "DISPLAY_AOD"; break;
        case COS_PM_SLEEP:       s = "SLEEP"; break;
        case COS_PM_DEEP_SLEEP:  s = "DEEP_SLEEP"; break;
        default: break;
        }
        sh_out(out, user, "[power] state : %s", s);
        /* 省电模式(可触摸唤醒的深度省电:DFS + 自动 Light-sleep) */
        sh_out(out, user, "[power] power-save : %s",
               cos_power_save_is_active() ? "ACTIVE" : "inactive");
        /* 性能模式(锁 240MHz 全速,与省电互斥;两者都关 = 智能模式) */
        sh_out(out, user, "[power] beast-mode : %s",
               cos_beast_mode_is_active() ? "ACTIVE" : "inactive");
        sh_out(out, user, "[power] wifi : %s | bt : %s",
               cos_net_wifi_is_enabled() ? "on" : "off",
               cos_net_bt_is_enabled() ? "on" : "off");
#ifdef COS_PLATFORM_ESP32
        /* DFS / 自动 Light-sleep 实际配置(esp_pm_configure 的真实结果) */
        esp_pm_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        esp_err_t perr = esp_pm_get_configuration(&cfg);
        if (perr == ESP_OK)
        {
            sh_out(out, user, "[power] pm : CPU %d-%dMHz, auto-light-sleep=%d",
                   (int)cfg.min_freq_mhz, (int)cfg.max_freq_mhz,
                   (int)cfg.light_sleep_enable);
        }
        else
        {
            sh_out(out, user, "[power] pm config read failed: %s",
                   esp_err_to_name(perr));
        }
        /* PM 锁列表:直接旁路输出到 stderr(esp_pm_dump_locks 只接受 FILE*) */
        sh_out(out, user, "[power] pm locks (see serial):");
        esp_pm_dump_locks(stderr);
#endif
    }
    else if (strcmp(argv[1], "deep-sleep") == 0)
    {
        uint32_t sec = 0;
        if (argc >= 3)
            sec = (uint32_t)atoi(argv[2]);
        sh_out(out, user, "[power] deep-sleep requested (auto-wake=%u s)", (unsigned)sec);
        cos_pm_deep_sleep_request(sec);
    }
    else if (strcmp(argv[1], "standby") == 0)
    {
        sh_out(out, user,
               "[power] entering L2 standby deep sleep (minimal clock + 5-tap to boot)");
#ifdef COS_PLATFORM_ESP32
        extern void cos_board_enter_standby_deep_sleep(void);
        cos_board_enter_standby_deep_sleep();
        sh_out(out, user, "[power] (unexpected) returned from deep sleep");
#else
        sh_out(out, user,
               "[power] standby deep sleep only supported on ESP32 target");
#endif
    }
    else if (strcmp(argv[1], "wake") == 0)
    {
        cos_pm_wake_up();
        sh_out(out, user, "[power] wake requested");
    }
    else
    {
        sh_out(out, user, "usage: power <status|deep-sleep [sec]|wake>");
    }
}

static void cmd_wos(cos_shell_output_cb_t out, void *user, int argc, char **argv)
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
            const cos_wos_app_desc_t *d = wos_app_manager_registered(i);
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

static void cmd_display(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    /* display brightness <0-100> — instant A/B test of the PWM backlight.
     * No animation (duration=0) so this is safe from any task context. */
    if (argc >= 3 && strcmp(argv[1], "brightness") == 0)
    {
        char *end = NULL;
        long v = strtol(argv[2], &end, 10);
        if (end == argv[2] || v < COS_DISPLAY_BRIGHTNESS_MIN || v > COS_DISPLAY_BRIGHTNESS_MAX)
        {
            sh_out(out, user, "error: brightness must be %d..%d (got '%s')",
                   COS_DISPLAY_BRIGHTNESS_MIN, COS_DISPLAY_BRIGHTNESS_MAX, argv[2]);
            return;
        }
        cos_display_set_brightness((uint8_t)v, COS_DISPLAY_DURATION_OFF, false);
        sh_out(out, user, "[display] brightness set to %ld (instant)", v);
        sh_out(out, user, "  -> if screen does NOT change, PWM is not reaching the backlight");
        return;
    }

    /* display bltest <0|1> — bypass PWM, drive the BL pin (GPIO43) directly
     * HIGH/LOW to verify the physical path from pin to backlight (no meter
     * needed): 1 -> fully bright, 0 -> dark. Any display brightness call
     * afterwards automatically rebinds the pin to the LEDC PWM. */
    if (argc >= 3 && strcmp(argv[1], "bltest") == 0)
    {
        char *end = NULL;
        long v = strtol(argv[2], &end, 10);
        if (end == argv[2] || (v != 0 && v != 1))
        {
            sh_out(out, user, "error: bltest must be 0 or 1 (got '%s')", argv[2]);
            return;
        }
        cos_result_t err = cos_display_bltest(v == 1);
        if (err != COS_OK)
        {
            sh_out(out, user, "error: bltest not supported by this board driver (%d)", (int)err);
            return;
        }
        sh_out(out, user, "[display] bltest: BL(GPIO43) %s (PWM bypassed)",
               v ? "HIGH -> screen should be FULLY BRIGHT" : "LOW -> screen should go DARK");
        sh_out(out, user, "  follows? -> BL pin is wired to backlight. restore: display brightness 100");
        return;
    }

    lv_display_t *d = lv_display_get_default();
    int w = COS_DISPLAY_WIDTH;
    int h = COS_DISPLAY_HEIGHT;
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
    sh_out(out, user, "  brightness  : %d (range %d..%d)",
           cos_display_get_brightness(), COS_DISPLAY_BRIGHTNESS_MIN, COS_DISPLAY_BRIGHTNESS_MAX);
    sh_out(out, user, "  usage       : display brightness <0-100> | display bltest <0|1>");
}

static void cmd_touch(cos_shell_output_cb_t out, void *user, int argc, char **argv)
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

static void cmd_wifi(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        /* default: status */
        sh_out(out, user, "[wifi] Wi-Fi service (Core system service)");
        sh_out(out, user, "  Enabled : %s", cos_net_wifi_is_enabled() ? "yes" : "no");
        sh_out(out, user, "  State   : %s", cos_net_wifi_state_str(cos_net_wifi_state()));
        char ip[COS_NET_WIFI_IP_MAX];
        if (cos_net_wifi_get_ip(ip, sizeof(ip)) == COS_OK)
            sh_out(out, user, "  IP      : %s", ip);
        else
            sh_out(out, user, "  IP      : (not connected)");
        sh_out(out, user, "  SSID    : %s", cos_net_wifi_connected_ssid()[0]
                                 ? cos_net_wifi_connected_ssid() : "(none)");
        sh_out(out, user, "  RSSI    : %d dBm", cos_net_wifi_rssi());
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
        cos_net_wifi_set_enabled(true);
        sh_out(out, user, "[wifi] enabled");
    }
    else if (strcmp(sub, "disable") == 0)
    {
        cos_net_wifi_set_enabled(false);
        sh_out(out, user, "[wifi] disabled");
    }
    else if (strcmp(sub, "scan") == 0)
    {
        cos_wifi_ap_t aps[COS_NET_WIFI_SCAN_MAX];
        uint32_t n = 0;
        cos_result_t r = cos_net_wifi_scan(aps, COS_NET_WIFI_SCAN_MAX, &n);
        if (r != COS_OK)
        {
            sh_out(out, user, "[wifi] scan failed: %d (radio enabled?)", r);
            return;
        }
        sh_out(out, user, "[wifi] found %u AP(s):", n);
        for (uint32_t i = 0; i < n; i++)
        {
            const char *sec = (aps[i].auth == COS_WIFI_AUTH_OPEN) ? "open"
                             : (aps[i].auth == COS_WIFI_AUTH_WPA3) ? "WPA3" : "WPA/WPA2";
            sh_out(out, user, "  %2u. %-16s rssi=%4d ch=%2u %s",
                   i, aps[i].ssid, aps[i].rssi, aps[i].channel, sec);
        }
    }
    else if (strcmp(sub, "connect") == 0)
    {
        const char *ssid = (argc >= 3) ? argv[2] : NULL;
        const char *pass = (argc >= 4) ? argv[3] : NULL;
        sh_out(out, user, "[wifi] connecting to %s ...", ssid ? ssid : "(saved)");
        cos_result_t r = cos_net_wifi_connect(ssid, pass);
        if (r == COS_OK)
        {
            char ip[COS_NET_WIFI_IP_MAX];
            cos_net_wifi_get_ip(ip, sizeof(ip));
            sh_out(out, user, "[wifi] connected: %s (IP %s, RSSI %d dBm)",
                   cos_net_wifi_connected_ssid(), ip, cos_net_wifi_rssi());
        }
        else
        {
            sh_out(out, user, "[wifi] connect failed: %d", r);
        }
    }
    else if (strcmp(sub, "disconnect") == 0)
    {
        cos_net_wifi_disconnect();
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
            cos_net_wifi_set_credentials(val, NULL);
            sh_out(out, user, "[wifi] ssid = %s", val);
        }
        else if (strcmp(what, "pass") == 0)
        {
            cos_net_wifi_set_credentials(NULL, val);
            sh_out(out, user, "[wifi] password = **** (hidden, not logged)");
        }
        else if (strcmp(what, "auto") == 0)
        {
            bool a = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
            cos_config_set_bool(COS_NET_WIFI_KEY_AUTO, a);
            sh_out(out, user, "[wifi] auto_connect = %s", a ? "on" : "off");
        }
        else
        {
            sh_out(out, user, "unknown field: %s (ssid|pass|auto)", what);
        }
    }
    else if (strcmp(sub, "save") == 0)
    {
        cos_net_wifi_save();
        sh_out(out, user, "[wifi] config saved (password stored silently)");
    }
    else
    {
        sh_out(out, user, "[wifi] unknown subcommand: %s", sub);
    }
}

static void cmd_bt(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "[bt] Bluetooth service (Core system service)");
        sh_out(out, user, "  Enabled : %s", cos_net_bt_is_enabled() ? "yes" : "no");
        sh_out(out, user, "  State   : %s", cos_net_bt_state_str(cos_net_bt_state()));
        char *n = cos_config_get_string(COS_NET_BT_KEY_NAME, "CantoMk6");
        sh_out(out, user, "  Name    : %s", n ? n : "CantoMk6");
        if (n) cos_free(n);
        return;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "status") == 0)
    {
        cmd_bt(out, user, 1, argv);
    }
    else if (strcmp(sub, "enable") == 0)
    {
        /* routes through port hook -> cos_net_bt_set_enabled(true) */
        cos_bluetooth_enable();
        sh_out(out, user, "[bt] enabled");
    }
    else if (strcmp(sub, "disable") == 0)
    {
        cos_bluetooth_disable();
        sh_out(out, user, "[bt] disabled");
    }
    else if (strcmp(sub, "scan") == 0)
    {
        cos_bt_device_t devs[COS_NET_BT_SCAN_MAX];
        uint32_t n = 0;
        cos_result_t r = cos_net_bt_scan(devs, COS_NET_BT_SCAN_MAX, &n);
        if (r != COS_OK)
        {
            sh_out(out, user, "[bt] scan failed: %d (radio enabled?)", r);
            return;
        }
        sh_out(out, user, "[bt] found %u device(s):", n);
        for (uint32_t i = 0; i < n; i++)
        {
            const char *t = (devs[i].type == COS_BT_DEV_CLASSIC) ? "BR/EDR" : "LE";
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
        cos_result_t r = cos_net_bt_connect(argv[2]);
        sh_out(out, user, r == COS_OK ? "[bt] connected & paired" : "[bt] connect failed: %d");
    }
    else if (strcmp(sub, "disconnect") == 0)
    {
        cos_net_bt_disconnect();
        sh_out(out, user, "[bt] disconnected");
    }
    else if (strcmp(sub, "paired") == 0)
    {
        cos_bt_device_t devs[COS_NET_BT_SCAN_MAX];
        uint32_t n = 0;
        cos_net_bt_get_paired(devs, COS_NET_BT_SCAN_MAX, &n);
        sh_out(out, user, "[bt] paired %u device(s):", n);
        for (uint32_t i = 0; i < n; i++)
            sh_out(out, user, "  %2u. %-16s %s", i, devs[i].name, devs[i].addr);
    }
    else if (strcmp(sub, "save") == 0)
    {
        cos_net_bt_save();
        sh_out(out, user, "[bt] config saved");
    }
    else
    {
        sh_out(out, user, "[bt] unknown subcommand: %s", sub);
    }
}

static void _proxy_status(cos_shell_output_cb_t out, void *user)
{
    char srv[64];
    cos_net_proxy_server_str(srv, sizeof(srv));
    sh_out(out, user, "[proxy] SOCKS5 client (Core system service)");
    sh_out(out, user, "  Enabled : %s", cos_net_proxy_is_enabled() ? "yes" : "no");
    sh_out(out, user, "  Server  : %s", srv);
    sh_out(out, user, "  Auth    : %s", cos_net_proxy_auth_configured() ? "configured" : "none");
    sh_out(out, user, "  Status  : %s", cos_net_proxy_status_str(cos_net_proxy_status()));
    sh_out(out, user, "  (password is NEVER shown in shell output)");
}

static void cmd_proxy(cos_shell_output_cb_t out, void *user, int argc, char **argv)
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
        cos_net_proxy_set_enabled(true);
        sh_out(out, user, "[proxy] enabled");
    }
    else if (strcmp(sub, "disable") == 0)
    {
        cos_net_proxy_set_enabled(false);
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
            cos_net_proxy_set_server(val, cos_net_proxy_port());
            sh_out(out, user, "[proxy] host = %s", val);
        }
        else if (strcmp(what, "port") == 0)
        {
            int p = atoi(val);
            cos_net_proxy_set_server(cos_net_proxy_host(), (uint16_t)p);
            sh_out(out, user, "[proxy] port = %d", p);
        }
        else if (strcmp(what, "user") == 0)
        {
            cos_net_proxy_set_credentials(val, NULL);
            sh_out(out, user, "[proxy] username = %s", val);
        }
        else if (strcmp(what, "pass") == 0)
        {
            cos_net_proxy_set_credentials(NULL, val);
            sh_out(out, user, "[proxy] password = **** (hidden, not logged)");
        }
        else
        {
            sh_out(out, user, "unknown field: %s (use host|port|user|pass)", what);
        }
    }
    else if (strcmp(sub, "save") == 0)
    {
        cos_net_proxy_save();
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
        cos_net_proxy_server_str(srv, sizeof(srv));
        sh_out(out, user, "[proxy] dialing %s:%d via SOCKS5 %s ...", th, tp, srv);
        cos_net_proxy_tunnel_t *t = NULL;
        cos_result_t r = cos_net_proxy_dial(th, (uint16_t)tp, &t);
        if (r == COS_OK)
        {
            sh_out(out, user, "[proxy] CONNECT success (SOCKS5 handshake OK)");
            cos_net_proxy_tunnel_close(t);
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

static void cmd_apps(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t n = cos_app_get_installed();
    sh_out(out, user, "[apps] installed: %u", n);
    for (uint32_t i = 0; i < n; i++)
    {
        const char *id = cos_app_list_get_id(i);
        sh_out(out, user, "  %u: %s", i, id ? id : "(null)");
    }
    if (n == 0)
        sh_out(out, user, "  (no apps installed)");
}

static void cmd_app(cos_shell_output_cb_t out, void *user, int argc, char **argv)
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
        bool installed = cos_app_list_contains(id);
        sh_out(out, user, "[app:%s] installed: %s", id, installed ? "yes" : "no");
        char path[COS_SHELL_LINE_MAX];
        snprintf(path, sizeof(path), "%s%s/manifest.json", COS_APP_INSTALLED_DIR, id);
        cJSON *manifest = cos_storage_json_load(path);
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
        if (!cos_app_list_contains(id))
        {
            sh_out(out, user, "app '%s' is not installed", id);
            return;
        }
        if (cos_app_is_disabled(id))
        {
            sh_out(out, user, "app '%s' is disabled (use: app enable %s)", id, id);
            return;
        }
        cos_result_t r = cos_app_launch_immediately(id);
        sh_out(out, user, "app '%s' launch requested (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "install") == 0)
    {
        /* Real install path: scan .eapk -> parse manifest -> unpack ->
         * register with the Plugin Manager -> refresh list -> post event. */
        cos_result_t r = cos_app_install(id);
        sh_out(out, user, "app install '%s' (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "stop") == 0 || strcmp(sub, "restart") == 0)
    {
        /* Release the running foreground app. cos_activity_back() pops the
         * app activity, whose on_destroy calls spm_app_stop() to free the
         * JerryScript realm + SNI context + timers. If a transition is still
         * mid-flight (e.g. headless --shell-test with no render loop), fall
         * back to stopping the script program directly so resources are freed. */
        cos_result_t r = cos_activity_back();
        if (r != COS_OK)
        {
            spm_app_stop();
            r = COS_OK;
        }
        sh_out(out, user, "app '%s' stop (result=%d)", id, (int)r);
        if (strcmp(sub, "restart") == 0)
        {
            cos_result_t r2 = cos_app_launch_immediately(id);
            sh_out(out, user, "app '%s' restart (result=%d)", id, (int)r2);
        }
        return;
    }

    if (strcmp(sub, "disable") == 0)
    {
        cos_result_t r = cos_app_disable(id);
        sh_out(out, user, "app '%s' disable (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "enable") == 0)
    {
        cos_result_t r = cos_app_enable(id);
        sh_out(out, user, "app '%s' enable (result=%d)", id, (int)r);
        return;
    }

    if (strcmp(sub, "uninstall") == 0)
    {
        if (!cos_app_list_contains(id))
        {
            sh_out(out, user, "app '%s' is not installed", id);
            return;
        }
        cos_result_t r = cos_app_uninstall(id);
        sh_out(out, user, "app '%s' uninstall (result=%d)", id, (int)r);
        return;
    }

    sh_out(out, user, "unknown app subcommand: %s", sub);
}

static void cmd_log(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "level") == 0)
    {
        const char *lv = argv[2];
        cos_log_level_t lvl = COS_LOG_LEVEL_DEBUG;
        if (strcmp(lv, "debug") == 0)       lvl = COS_LOG_LEVEL_DEBUG;
        else if (strcmp(lv, "info") == 0)   lvl = COS_LOG_LEVEL_INFO;
        else if (strcmp(lv, "warn") == 0)   lvl = COS_LOG_LEVEL_WARN;
        else if (strcmp(lv, "error") == 0)  lvl = COS_LOG_LEVEL_ERROR;
        else
        {
            sh_out(out, user, "unknown level '%s' (debug|info|warn|error)", lv);
            return;
        }
        cos_log_set_min_level(lvl);
        sh_out(out, user, "log level set to %s", lv);
        return;
    }
    sh_out(out, user, "[log] current min level: %d (0=debug 1=info 2=warn 3=error)",
           (int)cos_log_get_min_level());
}

static void cmd_reboot(cos_shell_output_cb_t out, void *user, int argc, char **argv)
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

int cos_shell_exec(const char *cmdline, cos_shell_output_cb_t out, void *user)
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
    char buf[COS_SHELL_LINE_MAX];
    strncpy(buf, cmdline, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Trim leading/trailing whitespace. */
    char *p = buf;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p == '\0')
        return 0; /* empty line */

    char *argv[COS_SHELL_ARG_MAX];
    int argc = 0;
    char *tok = strtok(p, " \t\r\n");
    while (tok && argc < COS_SHELL_ARG_MAX)
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

void cos_shell_init(void)
{
    _inited = true;
}

void cos_shell_deinit(void)
{
    _inited = false;
    _reboot_cb = NULL;
}

void cos_shell_set_reboot_cb(cos_shell_reboot_cb_t cb)
{
    _reboot_cb = cb;
}

void cos_shell_exec_log(const char *cmdline)
{
    cos_shell_exec(cmdline, shell_log_cb, NULL);
}

static void cmd_plugin(cos_shell_output_cb_t out, void *user, int argc, char **argv)
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
        cos_plugin_scan_result_t res;
        cos_plugin_manager_scan(force, &res);
        sh_out(out, user, "plugin scan: scanned=%u installed=%u skipped=%u failed=%u",
               res.scanned, res.installed, res.skipped, res.failed);
        return;
    }
    if (strcmp(sub, "list") == 0)
    {
        uint32_t n = cos_app_get_installed();
        sh_out(out, user, "installed apps: %u", n);
        for (uint32_t i = 0; i < n; i++)
        {
            const char *id = cos_app_list_get_id(i);
            if (!id)
                continue;
            const char *name = cos_app_get_name(id);
            bool dis = cos_app_is_disabled(id);
            sh_out(out, user, "  %s  (%s)%s", id, name ? name : "?",
                  dis ? " [disabled]" : "");
        }
        size_t wn = cos_watchface_list_size();
        sh_out(out, user, "installed watchfaces: %u", (unsigned)wn);
        for (size_t i = 0; i < wn; i++)
        {
            const char *id = cos_watchface_list_get_id(i);
            if (id)
                sh_out(out, user, "  %s", id);
        }
        return;
    }
    sh_out(out, user, "unknown subcommand: %s", sub);
}

static void cmd_ime(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc < 2)
    {
        sh_out(out, user, "usage: ime <pinyin>   (e.g. ime ni)");
        return;
    }
    const char *py = argv[1];
    const char *cands[16];
    int n = 0;
    int total = cos_pinyin_lookup(py, cands, 16, &n);
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
    COS_LOG_I("%s", line);
}
