/**
 * @file eos_net_bt.c
 * @brief Bluetooth (BLE) service implementation (Core system service)
 *
 * Platform-independent service logic. The radio backend is emulated on the
 * simulator (scan returns a fixed device list, connect pairs the device).
 * On real hardware, replace the marked SIM sections with the ESP-IDF
 * Bluedroid/NimBLE stack — the public API must stay identical.
 *
 * This file also provides the STRONG definitions of eos_bluetooth_enable()
 * and eos_bluetooth_disable() (declared in eos_port.h), overriding the weak
 * stubs in eos_port.c so the Settings app / control center route here.
 */

#include "eos_net_bt.h"
#include "eos_port.h"   /* eos_bluetooth_enable / eos_bluetooth_disable declarations */

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eos_log.h"
#include "eos_mem.h"
#include "eos_service_config.h"

/* Macros and Definitions -------------------------------------*/
#define EOS_BT_LOG_TAG "NetBt"

#define EOS_NET_BT_DEFAULT_NAME "Canto Mk.6" /* 设备 BLE 广播名,手机扫描可见 */

/* Variables --------------------------------------------------*/
typedef struct
{
    bool         initialized;
    bool         enabled;
    bool         discoverable;
    eos_bt_state_t state;
    char         name[EOS_NET_BT_NAME_MAX + 1];
    eos_bt_device_t paired[EOS_NET_BT_SCAN_MAX];
    uint32_t     paired_count;
    eos_bt_device_t connected;   /* zeroed when not connected */
    bool         has_connected;
} eos_bt_t;

static eos_bt_t g_bt = {0};

/* Static simulated device table (simulator only) */
static const eos_bt_device_t s_sim_devs[] = {
    { "Galaxy Watch",  "AA:BB:CC:11:22:33", -55, EOS_BT_DEV_LE,      false },
    { "AirPods Pro",   "AA:BB:CC:44:55:66", -62, EOS_BT_DEV_LE,      false },
    { "Mi Band 8",     "AA:BB:CC:77:88:99", -48, EOS_BT_DEV_LE,      false },
    { "ThinkPad X1",   "AA:BB:CC:AA:BB:CC", -70, EOS_BT_DEV_CLASSIC, false },
    { "Speaker-Mini",  "AA:BB:CC:DD:EE:FF", -60, EOS_BT_DEV_LE,      false },
};

/* Function Implementations -----------------------------------*/
static void _load_config(void)
{
    g_bt.enabled = (bool)eos_config_get_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, false);
    g_bt.discoverable = (bool)eos_config_get_bool(EOS_NET_BT_KEY_DISCOVERABLE, true);
    char *name = eos_config_get_string(EOS_NET_BT_KEY_NAME, EOS_NET_BT_DEFAULT_NAME);
    if (name)
    {
        strncpy(g_bt.name, name, EOS_NET_BT_NAME_MAX);
        g_bt.name[EOS_NET_BT_NAME_MAX] = '\0';
        eos_free(name);
    }
    else
    {
        strncpy(g_bt.name, EOS_NET_BT_DEFAULT_NAME, EOS_NET_BT_NAME_MAX);
    }
}

static void _ensure_init(void)
{
    if (!g_bt.initialized)
        eos_net_bt_init();
}

void eos_net_bt_init(void)
{
    if (g_bt.initialized)
        return;
    memset(&g_bt, 0, sizeof(g_bt));
    strncpy(g_bt.name, EOS_NET_BT_DEFAULT_NAME, EOS_NET_BT_NAME_MAX);
    _load_config();
    g_bt.state = g_bt.enabled ? EOS_BT_IDLE : EOS_BT_DISABLED;
    g_bt.initialized = true;

    EOS_LOG_I("[%s] init: enabled=%d name=\"%s\" discoverable=%d",
              EOS_BT_LOG_TAG, g_bt.enabled, g_bt.name, g_bt.discoverable);
}

const char *eos_net_bt_state_str(eos_bt_state_t s)
{
    switch (s)
    {
    case EOS_BT_DISABLED:  return "disabled";
    case EOS_BT_IDLE:      return "idle";
    case EOS_BT_SCANNING:  return "scanning";
    case EOS_BT_CONNECTED: return "connected";
    case EOS_BT_ERROR:     return "error";
    default:               return "unknown";
    }
}

eos_bt_state_t eos_net_bt_state(void) { return g_bt.state; }
bool           eos_net_bt_is_enabled(void) { return g_bt.enabled; }

eos_result_t eos_net_bt_set_enabled(bool enabled)
{
    _ensure_init();
    g_bt.enabled = enabled;
    if (enabled)
    {
        g_bt.state = EOS_BT_IDLE;
    }
    else
    {
        g_bt.state = EOS_BT_DISABLED;
        g_bt.has_connected = false;
        memset(&g_bt.connected, 0, sizeof(g_bt.connected));
    }
    /* Real radio backend: on ESP32 this starts/stops NimBLE GAP advertising
     * so the device is discoverable by phones; the simulator stub is a no-op
     * and keeps the simulated state machine intact. */
    eos_result_t r = eos_net_bt_backend_set_enabled(enabled, g_bt.name);
    if (r != EOS_OK)
    {
        EOS_LOG_E("[%s] radio backend %s failed (%d)", EOS_BT_LOG_TAG,
                  enabled ? "enable" : "disable", (int)r);
        g_bt.state = EOS_BT_ERROR;
        return r;
    }
    /* Persist. The Settings app / control center already call
     * eos_config_set_bool(bluetooth, ...) before invoking us, but writing
     * here keeps the state authoritative and idempotent. */
    return eos_config_set_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, enabled);
}

eos_result_t eos_net_bt_set_name(const char *name)
{
    _ensure_init();
    if (!name || name[0] == '\0')
        return EOS_ERR_VAR_NULL;
    strncpy(g_bt.name, name, EOS_NET_BT_NAME_MAX);
    g_bt.name[EOS_NET_BT_NAME_MAX] = '\0';
    /* If the radio is already advertising, update the broadcast name live. */
    if (g_bt.enabled)
        return eos_net_bt_backend_set_enabled(true, g_bt.name);
    return EOS_OK;
}

eos_result_t eos_net_bt_set_discoverable(bool discoverable)
{
    _ensure_init();
    g_bt.discoverable = discoverable;
    return EOS_OK;
}

eos_result_t eos_net_bt_save(void)
{
    _ensure_init();
    eos_result_t r = eos_config_set_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, g_bt.enabled);
    if (r == EOS_OK) r = eos_config_set_bool(EOS_NET_BT_KEY_DISCOVERABLE, g_bt.discoverable);
    if (r == EOS_OK) r = eos_config_set_string(EOS_NET_BT_KEY_NAME, g_bt.name);
    return r;
}

eos_result_t eos_net_bt_scan(eos_bt_device_t *out_devs,
                              uint32_t max_devs, uint32_t *out_count)
{
    _ensure_init();
    if (!g_bt.enabled)
        return EOS_ERR_NET_BT_NOT_ENABLED;
    if (!out_devs || !out_count || max_devs == 0)
        return EOS_ERR_VAR_NULL;

    g_bt.state = EOS_BT_SCANNING;

    uint32_t n = 0;
    uint32_t total = sizeof(s_sim_devs) / sizeof(s_sim_devs[0]);
    for (uint32_t i = 0; i < total && n < max_devs; i++)
    {
        out_devs[n] = s_sim_devs[i];
        /* mark already-paired ones */
        for (uint32_t p = 0; p < g_bt.paired_count; p++)
        {
            if (strncmp(out_devs[n].addr, g_bt.paired[p].addr, EOS_NET_BT_ADDR_MAX) == 0)
                out_devs[n].paired = true;
        }
        n++;
    }
    *out_count = n;

    g_bt.state = EOS_BT_IDLE;
    return EOS_OK;
}

eos_result_t eos_net_bt_connect(const char *addr)
{
    _ensure_init();
    if (!g_bt.enabled)
        return EOS_ERR_NET_BT_NOT_ENABLED;
    if (!addr || addr[0] == '\0')
        return EOS_ERR_VAR_NULL;

    /* ---- SIM backend: pair whatever address is given ---- */
    bool found = false;
    for (uint32_t i = 0; i < EOS_NET_BT_SCAN_MAX; i++)
    {
        if (strncmp(s_sim_devs[i].addr, addr, EOS_NET_BT_ADDR_MAX) == 0)
        {
            g_bt.connected = s_sim_devs[i];
            g_bt.connected.paired = true;
            found = true;
            break;
        }
    }
    if (!found)
    {
        /* unknown address: still "connect" as a generic device for the stub */
        memset(&g_bt.connected, 0, sizeof(g_bt.connected));
        strncpy(g_bt.connected.addr, addr, EOS_NET_BT_ADDR_MAX - 1);
        g_bt.connected.addr[EOS_NET_BT_ADDR_MAX - 1] = '\0';
        strncpy(g_bt.connected.name, "Unknown Device", EOS_NET_BT_NAME_MAX);
        g_bt.connected.type = EOS_BT_DEV_LE;
    }
    /* record as paired */
    if (g_bt.paired_count < EOS_NET_BT_SCAN_MAX)
    {
        bool already = false;
        for (uint32_t p = 0; p < g_bt.paired_count; p++)
        {
            if (strncmp(g_bt.paired[p].addr, g_bt.connected.addr, EOS_NET_BT_ADDR_MAX) == 0)
                already = true;
        }
        if (!already)
            g_bt.paired[g_bt.paired_count++] = g_bt.connected;
    }
    /* ------------------------------------------------------- */

    g_bt.has_connected = true;
    g_bt.state = EOS_BT_CONNECTED;
    /* 任何成功的连接都写入 SD 记忆(无 SD 则内部忽略) */
    eos_net_bt_save_history(g_bt.connected.addr, g_bt.connected.name);
    return EOS_OK;
}

eos_result_t eos_net_bt_disconnect(void)
{
    _ensure_init();
    g_bt.has_connected = false;
    memset(&g_bt.connected, 0, sizeof(g_bt.connected));
    g_bt.state = g_bt.enabled ? EOS_BT_IDLE : EOS_BT_DISABLED;
    return EOS_OK;
}

eos_result_t eos_net_bt_get_paired(eos_bt_device_t *out_devs,
                                   uint32_t max_devs, uint32_t *out_count)
{
    _ensure_init();
    if (!out_devs || !out_count || max_devs == 0)
        return EOS_ERR_VAR_NULL;
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_bt.paired_count && n < max_devs; i++)
        out_devs[n++] = g_bt.paired[i];
    *out_count = n;
    return EOS_OK;
}

/* ═══════════════ 用户需求 2026-08: 设备连接记忆(SD /history/bt) ═══════════════ */
#define _BT_HISTORY_DIR  "/history/bt"
#define _BT_HISTORY_FILE "/history/bt/history.txt"
#define _BT_HISTORY_MAX  32               /* 最多记忆的设备数(超出滚动覆盖最早) */
#define _BT_LINE_MAX     (EOS_NET_BT_ADDR_MAX + EOS_NET_BT_NAME_MAX + 16)

typedef struct
{
    char  addr[EOS_NET_BT_ADDR_MAX];   /* XX:XX:XX:XX:XX:XX */
    char  name[EOS_NET_BT_NAME_MAX + 1];
    int   count;                        /* 连接次数(越频繁优先级越高) */
} _bt_mem_t;

/* 读取记忆文件,每行 "addr|name|count",返回条目数 */
static int _bt_history_load(_bt_mem_t *out, int max)
{
    eos_file_t f = eos_fs_open_read(_BT_HISTORY_FILE);
    if (!f)
        return 0;
    int n = 0;
    char line[_BT_LINE_MAX];
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
                        strncpy(out[n].addr, p1, EOS_NET_BT_ADDR_MAX - 1);
                        out[n].addr[EOS_NET_BT_ADDR_MAX - 1] = '\0';
                        strncpy(out[n].name, p2 + 1, EOS_NET_BT_NAME_MAX);
                        out[n].name[EOS_NET_BT_NAME_MAX] = '\0';
                        out[n].count = atoi(p3 + 1);
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

static void _bt_history_store(const _bt_mem_t *items, int n)
{
    eos_fs_mkdir(_BT_HISTORY_DIR); /* 无 SD 卡时失败,忽略保存 */
    eos_file_t f = eos_fs_open_write(_BT_HISTORY_FILE);
    if (!f)
    {
        EOS_LOG_W("[%s] bt history ignored (no SD / write fail)", EOS_BT_LOG_TAG);
        return;
    }
    for (int i = 0; i < n; i++)
    {
        char line[_BT_LINE_MAX];
        int len = snprintf(line, sizeof(line), "%s|%s|%d\n",
                           items[i].addr, items[i].name, items[i].count);
        if (len > 0)
            eos_fs_write(f, line, (size_t)len);
    }
    eos_fs_close(f);
    EOS_LOG_I("[%s] bt history saved: %d device(s) -> %s", EOS_BT_LOG_TAG, n, _BT_HISTORY_FILE);
}

eos_result_t eos_net_bt_save_history(const char *addr, const char *name)
{
    _ensure_init();
    if (!addr || addr[0] == '\0')
        return EOS_ERR_VAR_NULL;

    _bt_mem_t items[_BT_HISTORY_MAX];
    int n = _bt_history_load(items, _BT_HISTORY_MAX);

    for (int i = 0; i < n; i++)
    {
        if (strncmp(items[i].addr, addr, EOS_NET_BT_ADDR_MAX) == 0)
        {
            items[i].count++; /* 连接一次,计数 +1 */
            if (name && name[0])
            {
                strncpy(items[i].name, name, EOS_NET_BT_NAME_MAX);
                items[i].name[EOS_NET_BT_NAME_MAX] = '\0';
            }
            _bt_history_store(items, n);
            EOS_LOG_I("[%s] bt history bumped: %s (count=%d)", EOS_BT_LOG_TAG, addr, items[i].count);
            return EOS_OK;
        }
    }
    if (n < _BT_HISTORY_MAX)
    {
        strncpy(items[n].addr, addr, EOS_NET_BT_ADDR_MAX - 1);
        items[n].addr[EOS_NET_BT_ADDR_MAX - 1] = '\0';
        strncpy(items[n].name, name ? name : "Unknown Device", EOS_NET_BT_NAME_MAX);
        items[n].name[EOS_NET_BT_NAME_MAX] = '\0';
        items[n].count = 1;
        _bt_history_store(items, n + 1);
    }
    else
    {
        /* 记忆已满:滚动覆盖最早一条 */
        for (int i = 1; i < _BT_HISTORY_MAX; i++)
            items[i - 1] = items[i];
        strncpy(items[_BT_HISTORY_MAX - 1].addr, addr, EOS_NET_BT_ADDR_MAX - 1);
        items[_BT_HISTORY_MAX - 1].addr[EOS_NET_BT_ADDR_MAX - 1] = '\0';
        strncpy(items[_BT_HISTORY_MAX - 1].name, name ? name : "Unknown Device", EOS_NET_BT_NAME_MAX);
        items[_BT_HISTORY_MAX - 1].name[EOS_NET_BT_NAME_MAX] = '\0';
        items[_BT_HISTORY_MAX - 1].count = 1;
        _bt_history_store(items, _BT_HISTORY_MAX);
    }
    EOS_LOG_I("[%s] bt history added: %s", EOS_BT_LOG_TAG, addr);
    return EOS_OK;
}

eos_result_t eos_net_bt_connect_from_history(void)
{
    _ensure_init();
    if (!g_bt.enabled)
        return EOS_ERR_NET_BT_NOT_ENABLED;

    /* 1. 扫描当前可见设备 */
    eos_bt_device_t devs[EOS_NET_BT_SCAN_MAX];
    uint32_t n_devs = 0;
    if (eos_net_bt_scan(devs, EOS_NET_BT_SCAN_MAX, &n_devs) != EOS_OK || n_devs == 0)
        return EOS_ERR_NET_SCAN;

    /* 2. 加载记忆 */
    _bt_mem_t items[_BT_HISTORY_MAX];
    int n_mem = _bt_history_load(items, _BT_HISTORY_MAX);
    if (n_mem == 0)
        return EOS_ERR_NET_BT_NOT_ENABLED; /* 无记忆 */

    /* 3. 在可见设备中挑选"最频繁连接"的候选 */
    const char *best_addr = NULL;
    const char *best_name = NULL;
    int best_count = -1;
    for (uint32_t i = 0; i < n_devs; i++)
    {
        for (int j = 0; j < n_mem; j++)
        {
            if (strncmp(devs[i].addr, items[j].addr, EOS_NET_BT_ADDR_MAX) == 0 &&
                items[j].count > best_count)
            {
                best_count = items[j].count;
                best_addr = items[j].addr;
                best_name = items[j].name;
            }
        }
    }
    if (!best_addr)
        return EOS_ERR_NET_BT_NOT_ENABLED; /* 记忆中的设备都不可见 */

    /* 4. 连接;成功后刷新记忆(计数 +1) */
    eos_result_t r = eos_net_bt_connect(best_addr);
    if (r == EOS_OK)
    {
        eos_net_bt_save_history(best_addr, best_name);
        EOS_LOG_I("[%s] auto-connected from history: %s (%s)", EOS_BT_LOG_TAG, best_addr, best_name);
    }
    return r;
}

/* ---- Port hook overrides (strong defs; replace eos_port.c weak stubs) ---- */
void eos_bluetooth_enable(void)
{
    eos_net_bt_set_enabled(true);
}

void eos_bluetooth_disable(void)
{
    eos_net_bt_set_enabled(false);
}
