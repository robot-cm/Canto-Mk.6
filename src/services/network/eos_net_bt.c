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

#define EOS_NET_BT_DEFAULT_NAME "ElenixOS"

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

/* ---- Port hook overrides (strong defs; replace eos_port.c weak stubs) ---- */
void eos_bluetooth_enable(void)
{
    eos_net_bt_set_enabled(true);
}

void eos_bluetooth_disable(void)
{
    eos_net_bt_set_enabled(false);
}
