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
#include "eos_log.h"
#include "eos_mem.h"
#include "eos_service_config.h"

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

/* Static simulated AP table (simulator only) */
static const eos_wifi_ap_t s_sim_aps[] = {
    { "ElenixOS_AP",   -38, 1,  EOS_WIFI_AUTH_OPEN },
    { "XIAO-Lab",      -45, 1,  EOS_WIFI_AUTH_WPA3 },
    { "HomeMesh_2.4G", -62, 6,  EOS_WIFI_AUTH_WPA  },
    { "StarbucksWiFi", -55, 3,  EOS_WIFI_AUTH_OPEN },
    { "CMCC-Test",     -70, 11, EOS_WIFI_AUTH_WPA  },
    { "Office-Guest",  -66, 9,  EOS_WIFI_AUTH_OPEN },
};

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

    uint32_t n = 0;
    uint32_t total = sizeof(s_sim_aps) / sizeof(s_sim_aps[0]);
    for (uint32_t i = 0; i < total && n < max_aps; i++)
    {
        out_aps[n] = s_sim_aps[i];
        n++;
    }
    *out_count = n;

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

    /* ---- SIM backend: always succeed, assign a deterministic fake IP ---- */
    strncpy(g_wifi.ssid, use_ssid, EOS_NET_WIFI_SSID_MAX);
    g_wifi.ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
    strncpy(g_wifi.ip, "192.168.1.42", EOS_NET_WIFI_IP_MAX - 1);
    g_wifi.ip[EOS_NET_WIFI_IP_MAX - 1] = '\0';
    g_wifi.rssi = -48;
    /* --------------------------------------------------------------------- */

    g_wifi.state = EOS_WIFI_CONNECTED;
    return EOS_OK;
}

eos_result_t eos_net_wifi_disconnect(void)
{
    if (!g_wifi.initialized)
        eos_net_wifi_init();
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
