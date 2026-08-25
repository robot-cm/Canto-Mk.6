/**
 * @file eos_net_wifi.c
 * @brief Wi-Fi service implementation (Core system service)
 *
 * Platform-independent service logic. The radio backend is emulated on the
 * simulator (scan returns a fixed AP list, connect always succeeds and yields
 * a fake IP). On real hardware, replace the marked SIM sections with the
 * ESP-IDF Wi-Fi stack (esp_wifi_*) — the public API must stay identical.
 */

#include "eos_net_wifi.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "eos_log.h"
#include "eos_mem.h"
#include "eos_service_config.h"
#include "eos_service_time.h"
#include "eos_port.h" /* eos_fs_* file API (SD card) */
#if !EOS_SIMULATOR
#include "eos_net_wifi_esp32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif

/* Macros and Definitions -------------------------------------*/
#define EOS_WIFI_LOG_TAG "NetWifi"

/* Variables --------------------------------------------------*/
typedef struct
{
    bool            initialized;
    bool            enabled;
    eos_wifi_state_t state;
    char            ssid[EOS_NET_WIFI_SSID_MAX + 1];
    char            password[EOS_NET_WIFI_PASS_MAX + 1];
    bool            auto_connect;
    char            ip[EOS_NET_WIFI_IP_MAX];
    int8_t          rssi;
} eos_wifi_t;

static eos_wifi_t g_wifi = {0};

/* 异步 scan 结果缓存:worker 后台执行 esp_wifi_scan 后写入,
 * UI 线程通过 eos_net_wifi_scan_take() 无阻塞读取。 */
static eos_wifi_ap_t  s_scan_cache[EOS_NET_WIFI_SCAN_MAX];
static uint32_t       s_scan_count = 0;
static volatile bool  s_scan_busy  = false;

/* Static simulated AP table (simulator only) */
#if EOS_SIMULATOR
static const eos_wifi_ap_t s_sim_aps[] = {
    { "ElenixOS_AP",   -38, 1,  EOS_WIFI_AUTH_OPEN },
    { "XIAO-Lab",      -45, 1,  EOS_WIFI_AUTH_WPA3 },
    { "HomeMesh_2.4G", -62, 6,  EOS_WIFI_AUTH_WPA  },
    { "StarbucksWiFi", -55, 3,  EOS_WIFI_AUTH_OPEN },
    { "CMCC-Test",     -70, 11, EOS_WIFI_AUTH_WPA  },
    { "Office-Guest",  -66, 9,  EOS_WIFI_AUTH_OPEN },
};
#endif /* EOS_SIMULATOR */

/* Function Implementations -----------------------------------*/
static void _load_config(void)
{
    g_wifi.enabled = (bool)eos_config_get_bool(EOS_NET_WIFI_KEY_ENABLED, false);
    g_wifi.auto_connect = (bool)eos_config_get_bool(EOS_NET_WIFI_KEY_AUTO, false);

    char *ssid = eos_config_get_string(EOS_NET_WIFI_KEY_SSID, "");
    if (ssid)
    {
        strncpy(g_wifi.ssid, ssid, EOS_NET_WIFI_SSID_MAX);
        g_wifi.ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
        eos_free(ssid);
    }
    /* password is intentionally NOT loaded into RAM at init for privacy;
     * it is fetched only when connecting. */
}

void eos_net_wifi_init(void)
{
    if (g_wifi.initialized)
        return;
    memset(&g_wifi, 0, sizeof(g_wifi));
    g_wifi.state = EOS_WIFI_DISABLED;
    _load_config();
    if (g_wifi.enabled)
        g_wifi.state = EOS_WIFI_IDLE;
    g_wifi.initialized = true;

    EOS_LOG_I("[%s] init: enabled=%d auto_connect=%d ssid=\"%s\"",
              EOS_WIFI_LOG_TAG, g_wifi.enabled, g_wifi.auto_connect,
              g_wifi.ssid[0] ? g_wifi.ssid : "");
}

const char *eos_net_wifi_state_str(eos_wifi_state_t s)
{
    switch (s)
    {
    case EOS_WIFI_DISABLED:    return "disabled";
    case EOS_WIFI_IDLE:        return "idle";
    case EOS_WIFI_SCANNING:    return "scanning";
    case EOS_WIFI_CONNECTING:  return "connecting";
    case EOS_WIFI_CONNECTED:   return "connected";
    case EOS_WIFI_DISCONNECTED:return "disconnected";
    case EOS_WIFI_ERROR:       return "error";
    default:                   return "unknown";
    }
}

eos_wifi_state_t eos_net_wifi_state(void) { return g_wifi.state; }
bool             eos_net_wifi_is_enabled(void) { return g_wifi.enabled; }

eos_result_t eos_net_wifi_set_enabled(bool enabled)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    EOS_LOG_D("wifi set_enabled: enabled=%d", enabled);
    g_wifi.enabled = enabled;
    g_wifi.state = enabled ? EOS_WIFI_IDLE : EOS_WIFI_DISABLED;
    if (!enabled)
    {
        g_wifi.ip[0] = '\0';
        g_wifi.rssi = 0;
    }
    return eos_config_set_bool(EOS_NET_WIFI_KEY_ENABLED, enabled);
}

eos_result_t eos_net_wifi_set_credentials(const char *ssid, const char *password)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    if (ssid)
    {
        strncpy(g_wifi.ssid, ssid, EOS_NET_WIFI_SSID_MAX);
        g_wifi.ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
    }
    if (password)
    {
        strncpy(g_wifi.password, password, EOS_NET_WIFI_PASS_MAX);
        g_wifi.password[EOS_NET_WIFI_PASS_MAX] = '\0';
    }
    return EOS_OK;
}

eos_result_t eos_net_wifi_save(void)
{
    eos_result_t r = eos_config_set_bool(EOS_NET_WIFI_KEY_ENABLED, g_wifi.enabled);
    if (r == EOS_OK) r = eos_config_set_bool(EOS_NET_WIFI_KEY_AUTO, g_wifi.auto_connect);
    if (r == EOS_OK) r = eos_config_set_string(EOS_NET_WIFI_KEY_SSID, g_wifi.ssid[0] ? g_wifi.ssid : "");
    /* password persisted via silent setter so it never appears in plaintext logs */
    if (r == EOS_OK) r = eos_config_set_string_silent(EOS_NET_WIFI_KEY_PASSWORD, g_wifi.password[0] ? g_wifi.password : "");
    return r;
}

eos_result_t eos_net_wifi_scan(eos_wifi_ap_t *out_aps,
                               uint32_t max_aps, uint32_t *out_count)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    if (!g_wifi.enabled)
        return EOS_ERR_NET_NOT_CONNECTED; /* radio off */
    if (!out_aps || !out_count || max_aps == 0)
        return EOS_ERR_VAR_NULL;

    g_wifi.state = EOS_WIFI_SCANNING;

#if EOS_SIMULATOR
    /* ---- SIM backend: return the fixed AP table (simulator only) ---- */
    uint32_t n = 0;
    uint32_t total = sizeof(s_sim_aps) / sizeof(s_sim_aps[0]);
    for (uint32_t i = 0; i < total && n < max_aps; i++)
    {
        out_aps[n] = s_sim_aps[i];
        n++;
    }
    *out_count = n;
#else
    /* ---- Real backend: ESP-IDF esp_wifi_* active scan ---- */
    if (eos_net_wifi_esp32_init() != ESP_OK)
    {
        g_wifi.state = EOS_WIFI_ERROR;
        return EOS_ERR_NET_SCAN;
    }
    uint32_t n = 0;
    esp_err_t r = eos_net_wifi_esp32_scan(out_aps, max_aps, &n);
    *out_count = n;
    if (r != ESP_OK)
    {
        g_wifi.state = EOS_WIFI_ERROR;
        return EOS_ERR_NET_SCAN;
    }
#endif /* EOS_SIMULATOR */

    g_wifi.state = g_wifi.enabled ? EOS_WIFI_IDLE : EOS_WIFI_DISABLED;
    return EOS_OK;
}

eos_result_t eos_net_wifi_connect(const char *ssid, const char *password)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    if (!g_wifi.enabled)
        return EOS_ERR_NET_NOT_CONNECTED;

    const char *use_ssid = ssid ? ssid : (g_wifi.ssid[0] ? g_wifi.ssid : NULL);
    if (!use_ssid || use_ssid[0] == '\0')
        return EOS_ERR_NET_NO_SSID;

    if (password)
    {
        strncpy(g_wifi.password, password, EOS_NET_WIFI_PASS_MAX);
        g_wifi.password[EOS_NET_WIFI_PASS_MAX] = '\0';
    }

    g_wifi.state = EOS_WIFI_CONNECTING;

#if EOS_SIMULATOR
    /* ---- SIM backend: always succeed, assign a deterministic fake IP ---- */
    strncpy(g_wifi.ssid, use_ssid, EOS_NET_WIFI_SSID_MAX);
    g_wifi.ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
    strncpy(g_wifi.ip, "192.168.1.42", EOS_NET_WIFI_IP_MAX - 1);
    g_wifi.ip[EOS_NET_WIFI_IP_MAX - 1] = '\0';
    g_wifi.rssi = -48;
    /* --------------------------------------------------------------------- */
#else
    /* ---- Real backend: ESP-IDF connect + wait for GOT_IP (blocking) ---- */
    if (eos_net_wifi_esp32_init() != ESP_OK)
    {
        g_wifi.state = EOS_WIFI_ERROR;
        return EOS_ERR_NET_SCAN;
    }
    char ip_buf[EOS_NET_WIFI_IP_MAX] = "";
    int8_t rssi = 0;
    if (eos_net_wifi_esp32_connect(use_ssid, g_wifi.password,
                                   ip_buf, sizeof(ip_buf), &rssi) != ESP_OK)
    {
        g_wifi.state = EOS_WIFI_ERROR;
        return EOS_ERR_NET_SCAN;
    }
    strncpy(g_wifi.ssid, use_ssid, EOS_NET_WIFI_SSID_MAX);
    g_wifi.ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
    strncpy(g_wifi.ip, ip_buf, EOS_NET_WIFI_IP_MAX - 1);
    g_wifi.ip[EOS_NET_WIFI_IP_MAX - 1] = '\0';
    g_wifi.rssi = rssi;
    /* --------------------------------------------------------------------- */
#endif /* EOS_SIMULATOR */

    g_wifi.state = EOS_WIFI_CONNECTED;
    /* 任何成功的连接都写入 SD 记忆(无 SD 则内部忽略) */
    eos_net_wifi_save_history(use_ssid, g_wifi.rssi, g_wifi.password);
    /* 联网后立即尝试 SNTP 校时(时间服务内部防抖/成功后自动停止) */
    eos_time_ntp_sync_start();
    return EOS_OK;
}

eos_result_t eos_net_wifi_disconnect(void)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
#if !EOS_SIMULATOR
    eos_net_wifi_esp32_init();          /* idempotent */
    eos_net_wifi_esp32_disconnect();
#endif /* !EOS_SIMULATOR */
    g_wifi.ip[0] = '\0';
    g_wifi.rssi = 0;
    g_wifi.state = g_wifi.enabled ? EOS_WIFI_DISCONNECTED : EOS_WIFI_DISABLED;
    return EOS_OK;
}

eos_result_t eos_net_wifi_get_ip(char *buf, size_t buflen)
{
    if (!buf || buflen == 0)
        return EOS_ERR_VAR_NULL;
    if (g_wifi.state != EOS_WIFI_CONNECTED)
        return EOS_ERR_NET_NOT_CONNECTED;
    strncpy(buf, g_wifi.ip, buflen - 1);
    buf[buflen - 1] = '\0';
    return EOS_OK;
}

int8_t eos_net_wifi_rssi(void)
{
    return (g_wifi.state == EOS_WIFI_CONNECTED) ? g_wifi.rssi : 0;
}

const char *eos_net_wifi_connected_ssid(void)
{
    return (g_wifi.state == EOS_WIFI_CONNECTED) ? g_wifi.ssid : "";
}

/* ═══════════════ 用户需求 2026-08: AP 连接记忆(SD /history/wifi) ═══════════════ */
#define _WIFI_HISTORY_DIR  "/history/wifi"
#define _WIFI_HISTORY_FILE "/history/wifi/history.txt"
#define _WIFI_HISTORY_MAX  32               /* 最多记忆的 AP 数(超出滚动覆盖最早) */
#define _WIFI_LINE_MAX     (EOS_NET_WIFI_SSID_MAX + EOS_NET_WIFI_PASS_MAX + 16)

typedef struct
{
    char   ssid[EOS_NET_WIFI_SSID_MAX + 1];
    int8_t rssi;   /* 最近一次连接时的信号强度(dBm) */
    char   password[EOS_NET_WIFI_PASS_MAX + 1];
} _wifi_mem_t;

/* 读取记忆文件,每行 "ssid|rssi|password",返回条目数 */
static int _wifi_history_load(_wifi_mem_t *out, int max)
{
    eos_file_t f = eos_fs_open_read(_WIFI_HISTORY_FILE);
    if (!f)
        return 0;
    int n = 0;
    char line[_WIFI_LINE_MAX];
    int in = 0;
    line[0] = '\0';
    for (;;)
    {
        char c;
        if (eos_fs_read(f, &c, 1) != 1)
            break;
        if (c == '\n')
        {
            line[in] = '\0';
            if (line[0] && n < max)
            {
                char *p1 = line;
                char *p2 = strchr(p1, '|');
                if (p2)
                {
                    *p2 = '\0';
                    char *p3 = strchr(p2 + 1, '|');
                    if (p3)
                    {
                        *p3 = '\0';
                        strncpy(out[n].ssid, p1, EOS_NET_WIFI_SSID_MAX);
                        out[n].ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
                        out[n].rssi = (int8_t)atoi(p2 + 1);
                        strncpy(out[n].password, p3 + 1, EOS_NET_WIFI_PASS_MAX);
                        out[n].password[EOS_NET_WIFI_PASS_MAX] = '\0';
                        n++;
                    }
                }
            }
            in = 0;
            line[0] = '\0';
        }
        else if (in < (int)sizeof(line) - 1)
        {
            line[in++] = c;
        }
    }
    eos_fs_close(f);
    return n;
}

static void _wifi_history_store(const _wifi_mem_t *items, int n)
{
    eos_fs_mkdir(_WIFI_HISTORY_DIR); /* 无 SD 卡时失败,忽略保存 */
    eos_file_t f = eos_fs_open_write(_WIFI_HISTORY_FILE);
    if (!f)
    {
        EOS_LOG_W("[%s] wifi history ignored (no SD / write fail)", EOS_WIFI_LOG_TAG);
        return;
    }
    for (int i = 0; i < n; i++)
    {
        char line[_WIFI_LINE_MAX];
        int len = snprintf(line, sizeof(line), "%s|%d|%s\n",
                           items[i].ssid, (int)items[i].rssi, items[i].password);
        if (len > 0)
            eos_fs_write(f, line, (size_t)len);
    }
    eos_fs_close(f);
    EOS_LOG_I("[%s] wifi history saved: %d AP(s) -> %s", EOS_WIFI_LOG_TAG, n, _WIFI_HISTORY_FILE);
}

eos_result_t eos_net_wifi_save_history(const char *ssid, int8_t rssi, const char *password)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    if (!ssid || ssid[0] == '\0')
        return EOS_ERR_VAR_NULL;

    _wifi_mem_t items[_WIFI_HISTORY_MAX];
    int n = _wifi_history_load(items, _WIFI_HISTORY_MAX);

    for (int i = 0; i < n; i++)
    {
        if (strcmp(items[i].ssid, ssid) == 0)
        {
            items[i].rssi = rssi;
            if (password && password[0])
            {
                strncpy(items[i].password, password, EOS_NET_WIFI_PASS_MAX);
                items[i].password[EOS_NET_WIFI_PASS_MAX] = '\0';
            }
            _wifi_history_store(items, n);
            return EOS_OK;
        }
    }
    if (n < _WIFI_HISTORY_MAX)
    {
        strncpy(items[n].ssid, ssid, EOS_NET_WIFI_SSID_MAX);
        items[n].ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
        items[n].rssi = rssi;
        items[n].password[0] = '\0';
        if (password)
        {
            strncpy(items[n].password, password, EOS_NET_WIFI_PASS_MAX);
            items[n].password[EOS_NET_WIFI_PASS_MAX] = '\0';
        }
        _wifi_history_store(items, n + 1);
    }
    else
    {
        /* 记忆已满:滚动覆盖最早一条 */
        for (int i = 1; i < _WIFI_HISTORY_MAX; i++)
            items[i - 1] = items[i];
        strncpy(items[_WIFI_HISTORY_MAX - 1].ssid, ssid, EOS_NET_WIFI_SSID_MAX);
        items[_WIFI_HISTORY_MAX - 1].ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
        items[_WIFI_HISTORY_MAX - 1].rssi = rssi;
        items[_WIFI_HISTORY_MAX - 1].password[0] = '\0';
        if (password)
        {
            strncpy(items[_WIFI_HISTORY_MAX - 1].password, password, EOS_NET_WIFI_PASS_MAX);
            items[_WIFI_HISTORY_MAX - 1].password[EOS_NET_WIFI_PASS_MAX] = '\0';
        }
        _wifi_history_store(items, _WIFI_HISTORY_MAX);
    }
    EOS_LOG_I("[%s] wifi history updated: %s (rssi=%d)", EOS_WIFI_LOG_TAG, ssid, (int)rssi);
    return EOS_OK;
}

/* 同步执行"扫描+连接记忆 AP"。可能阻塞数秒(esp_wifi_scan/connect 阻塞),
 * 必须运行在后台 worker task,禁止在 LVGL/UI 线程调用(见 eos_port.h)。
 * 注意:aps/items 必须 static(worker 栈仅 8KB,栈上 4KB 数组会溢出→WDT 复位);
 *       worker 单命令串行执行,无并发访问问题。 */
static eos_result_t _wifi_connect_from_history_sync(void)
{
    static eos_wifi_ap_t aps[EOS_NET_WIFI_SCAN_MAX];
    static _wifi_mem_t   items[_WIFI_HISTORY_MAX];
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    if (!g_wifi.enabled)
        return EOS_ERR_NET_NOT_CONNECTED;

    /* 1. 扫描当前可见 AP */
    uint32_t n_aps = 0;
    if (eos_net_wifi_scan(aps, EOS_NET_WIFI_SCAN_MAX, &n_aps) != EOS_OK || n_aps == 0)
        return EOS_ERR_NET_SCAN;

    /* 2. 加载记忆 */
    int n_mem = _wifi_history_load(items, _WIFI_HISTORY_MAX);
    if (n_mem == 0)
        return EOS_ERR_NET_NO_SSID; /* 无记忆 */

    /* 3. 在可见 AP 中挑选"记忆中信号最强"的候选 */
    const char *best_ssid = NULL;
    const char *best_pwd = NULL;
    int8_t best_rssi = INT8_MIN;
    for (uint32_t i = 0; i < n_aps; i++)
    {
        for (int j = 0; j < n_mem; j++)
        {
            if (strcmp(aps[i].ssid, items[j].ssid) == 0 && items[j].rssi > best_rssi)
            {
                best_rssi = items[j].rssi;
                best_ssid = items[j].ssid;
                best_pwd = items[j].password;
            }
        }
    }
    if (!best_ssid)
        return EOS_ERR_NET_NO_SSID; /* 记忆中的 AP 都不可见 */

    /* 4. 连接;成功后用本次扫描 rssi 刷新记忆 */
    eos_result_t r = eos_net_wifi_connect(best_ssid, best_pwd);
    if (r == EOS_OK)
    {
        for (uint32_t i = 0; i < n_aps; i++)
        {
            if (strcmp(aps[i].ssid, best_ssid) == 0)
            {
                eos_net_wifi_save_history(best_ssid, aps[i].rssi, best_pwd);
                break;
            }
        }
        EOS_LOG_I("[%s] auto-connected from history: %s", EOS_WIFI_LOG_TAG, best_ssid);
    }
    return r;
}

#if !EOS_SIMULATOR
/* ═══════════════════════════════════════════════════════════════
 *  WiFi 后台 worker
 *  阻塞的 esp_wifi_scan/connect(最长 15s)绝不能跑在 LVGL/UI 线程,
 *  否则屏幕冻结、心跳停止(与 BT 的 eos_bt_init task 同理)。
 *  服务层 API 只向队列投递命令,立即返回;worker 在后台低优先级执行。
 * ═══════════════════════════════════════════════════════════════ */
typedef enum
{
    EOS_WIFI_CMD_CONNECT_HISTORY = 1,
    EOS_WIFI_CMD_DISCONNECT,
    EOS_WIFI_CMD_SCAN,
    EOS_WIFI_CMD_CONNECT,
} eos_wifi_cmd_t;

static QueueHandle_t s_wifi_q = NULL;

/* 后台执行阻塞扫描,结果缓存到 s_scan_cache(UI 线程可无阻塞取回)。
 * aps 用 static:worker 栈仅 8KB,勿放 640B 数组进栈(worker 串行,无并发问题)。 */
static void _wifi_scan_to_cache(void)
{
    static eos_wifi_ap_t aps[EOS_NET_WIFI_SCAN_MAX];
    uint32_t n = 0;
    eos_result_t r = eos_net_wifi_scan(aps, EOS_NET_WIFI_SCAN_MAX, &n);
    if (r == EOS_OK && n > 0)
    {
        uint32_t keep = n > EOS_NET_WIFI_SCAN_MAX ? EOS_NET_WIFI_SCAN_MAX : n;
        memcpy(s_scan_cache, aps, keep * sizeof(eos_wifi_ap_t));
        s_scan_count = keep;
    }
    else
    {
        s_scan_count = 0;
    }
    s_scan_busy = false;
}

static void _wifi_worker(void *arg)
{
    (void)arg;
    eos_wifi_cmd_t cmd;
    for (;;)
    {
        if (xQueueReceive(s_wifi_q, &cmd, portMAX_DELAY) != pdTRUE)
            continue;
        switch (cmd)
        {
        case EOS_WIFI_CMD_CONNECT_HISTORY:
            EOS_LOG_I("[%s] worker: connect_from_history start", EOS_WIFI_LOG_TAG);
            _wifi_connect_from_history_sync();
            EOS_LOG_I("[%s] worker: connect_from_history done (state=%d)",
                      EOS_WIFI_LOG_TAG, (int)g_wifi.state);
            break;
        case EOS_WIFI_CMD_DISCONNECT:
            EOS_LOG_I("[%s] worker: disconnect", EOS_WIFI_LOG_TAG);
            eos_net_wifi_disconnect();
            break;
        case EOS_WIFI_CMD_SCAN:
            EOS_LOG_I("[%s] worker: scan start", EOS_WIFI_LOG_TAG);
            _wifi_scan_to_cache();
            EOS_LOG_I("[%s] worker: scan done (%u AP, state=%d)",
                      EOS_WIFI_LOG_TAG, (unsigned)s_scan_count, (int)g_wifi.state);
            break;
        case EOS_WIFI_CMD_CONNECT:
            EOS_LOG_I("[%s] worker: connect (ssid=%s)", EOS_WIFI_LOG_TAG,
                      g_wifi.ssid[0] ? g_wifi.ssid : "");
            eos_net_wifi_connect(NULL, NULL);   /* 用 g_wifi 中已缓存的凭据 */
            EOS_LOG_I("[%s] worker: connect done (state=%d)",
                      EOS_WIFI_LOG_TAG, (int)g_wifi.state);
            break;
        default:
            break;
        }
    }
}

static void _wifi_ensure_worker(void)
{
    if (s_wifi_q)
        return;
    s_wifi_q = xQueueCreate(4, sizeof(eos_wifi_cmd_t));
    if (!s_wifi_q)
        return;
    /* 低优先级(1);栈 8KB 走内部 RAM(含 esp_wifi 调用链)。
     * 之前 4KB 栈装不下 4KB 数组 → 溢出 → TG1WDT 复位。 */
    if (xTaskCreatePinnedToCore(_wifi_worker, "wifi_wk", 8192, NULL, 1, NULL, 1) != pdPASS)
    {
        vQueueDelete(s_wifi_q);
        s_wifi_q = NULL;
    }
}
#endif /* !EOS_SIMULATOR */

eos_result_t eos_net_wifi_connect_from_history(void)
{
#if EOS_SIMULATOR
    /* 模拟器:无真实射频,直接同步执行(UI 线程无阻塞问题) */
    return _wifi_connect_from_history_sync();
#else
    /* 真实硬件:投递到后台 worker,立即返回,不阻塞 UI 线程 */
    _wifi_ensure_worker();
    if (!s_wifi_q)
        return EOS_ERR_BUSY;
    eos_wifi_cmd_t cmd = EOS_WIFI_CMD_CONNECT_HISTORY;
    if (xQueueSend(s_wifi_q, &cmd, 0) != pdTRUE)
        return EOS_ERR_BUSY;
    EOS_LOG_I("[%s] connect_from_history queued (async)", EOS_WIFI_LOG_TAG);
    return EOS_OK;
#endif
}

/* ═══════════════ 异步 scan / connect(UI 线程安全) ═══════════════ */
eos_result_t eos_net_wifi_scan_async(void)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    if (!g_wifi.enabled)
        return EOS_ERR_NET_NOT_CONNECTED; /* radio off */
#if EOS_SIMULATOR
    /* 模拟器:无阻塞,直接执行并缓存结果 */
    eos_wifi_ap_t aps[EOS_NET_WIFI_SCAN_MAX];
    uint32_t n = 0;
    eos_result_t r = eos_net_wifi_scan(aps, EOS_NET_WIFI_SCAN_MAX, &n);
    s_scan_count = (r == EOS_OK) ? n : 0;
    if (r == EOS_OK && n > 0)
        memcpy(s_scan_cache, aps, n * sizeof(eos_wifi_ap_t));
    s_scan_busy = false;
    return r;
#else
    _wifi_ensure_worker();
    if (!s_wifi_q)
        return EOS_ERR_BUSY;
    if (s_scan_busy)
        return EOS_ERR_BUSY; /* 上一次扫描仍在后台进行 */
    s_scan_busy = true;
    s_scan_count = 0;
    eos_wifi_cmd_t cmd = EOS_WIFI_CMD_SCAN;
    if (xQueueSend(s_wifi_q, &cmd, 0) != pdTRUE)
    {
        s_scan_busy = false;
        return EOS_ERR_BUSY;
    }
    EOS_LOG_I("[%s] scan queued (async)", EOS_WIFI_LOG_TAG);
    return EOS_OK;
#endif /* EOS_SIMULATOR */
}

bool eos_net_wifi_scan_busy(void)
{
    return s_scan_busy;
}

eos_result_t eos_net_wifi_scan_take(eos_wifi_ap_t *out_aps,
                                    uint32_t max_aps, uint32_t *out_count)
{
    if (!out_aps || !out_count || max_aps == 0)
        return EOS_ERR_VAR_NULL;
    if (s_scan_busy)
        return EOS_ERR_BUSY; /* 尚未完成 */
    uint32_t n = s_scan_count > max_aps ? max_aps : s_scan_count;
    if (n > 0)
        memcpy(out_aps, s_scan_cache, n * sizeof(eos_wifi_ap_t));
    *out_count = n;
    return EOS_OK;
}

eos_result_t eos_net_wifi_connect_async(const char *ssid, const char *password)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
    if (!g_wifi.enabled)
        return EOS_ERR_NET_NOT_CONNECTED;
    /* 先缓存凭据(worker 执行时 ctx 已释放,须拷贝) */
    if (ssid)
    {
        strncpy(g_wifi.ssid, ssid, EOS_NET_WIFI_SSID_MAX);
        g_wifi.ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
    }
    if (password)
    {
        strncpy(g_wifi.password, password, EOS_NET_WIFI_PASS_MAX);
        g_wifi.password[EOS_NET_WIFI_PASS_MAX] = '\0';
    }
#if EOS_SIMULATOR
    return eos_net_wifi_connect(NULL, NULL);
#else
    _wifi_ensure_worker();
    if (!s_wifi_q)
        return EOS_ERR_BUSY;
    eos_wifi_cmd_t cmd = EOS_WIFI_CMD_CONNECT;
    if (xQueueSend(s_wifi_q, &cmd, 0) != pdTRUE)
        return EOS_ERR_BUSY;
    EOS_LOG_I("[%s] connect queued (async)", EOS_WIFI_LOG_TAG);
    return EOS_OK;
#endif /* EOS_SIMULATOR */
}
