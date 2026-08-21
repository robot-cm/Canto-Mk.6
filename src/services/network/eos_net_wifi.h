/**
 * @file eos_net_wifi.h
 * @brief Wi-Fi service (Core system service)
 *
 * Wi-Fi is a Core / System Service. On real hardware it drives the ESP-IDF
 * Wi-Fi stack; on the simulator it runs a stub that emulates scan/connect so
 * the service logic and the Shell integration can be verified on PC.
 *
 * Configuration is persisted under `wifi.*` in the system config:
 *   wifi.enabled     (bool)
 *   wifi.ssid        (string)
 *   wifi.password    (string, stored via silent setter — never logged)
 *   wifi.auto_connect(bool)
 *
 * Apps must NOT talk to the Wi-Fi driver directly; they go through the
 * Network API (this service), exactly as required by AGENTS.md.
 */

#ifndef EOS_NET_WIFI_H
#define EOS_NET_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "eos_core.h"

/* Public macros ----------------------------------------------*/
#define EOS_NET_WIFI_KEY_ENABLED     "wifi.enabled"
#define EOS_NET_WIFI_KEY_SSID        "wifi.ssid"
#define EOS_NET_WIFI_KEY_PASSWORD    "wifi.password"
#define EOS_NET_WIFI_KEY_AUTO        "wifi.auto_connect"

#define EOS_NET_WIFI_SSID_MAX  32
#define EOS_NET_WIFI_PASS_MAX  64
#define EOS_NET_WIFI_IP_MAX    16   /* "255.255.255.255\0" */
#define EOS_NET_WIFI_SCAN_MAX  16   /* max APs returned per scan */

/* Public typedefs --------------------------------------------*/
typedef enum
{
    EOS_WIFI_DISABLED = 0,   /**< radio off */
    EOS_WIFI_IDLE,           /**< enabled, not connected */
    EOS_WIFI_SCANNING,       /**< scan in progress */
    EOS_WIFI_CONNECTING,     /**< association in progress */
    EOS_WIFI_CONNECTED,      /**< associated + IP obtained */
    EOS_WIFI_DISCONNECTED,   /**< was connected, now dropped */
    EOS_WIFI_ERROR           /**< last operation failed */
} eos_wifi_state_t;

/* Authentication mode (simplified) */
typedef enum
{
    EOS_WIFI_AUTH_OPEN = 0,
    EOS_WIFI_AUTH_WEP,
    EOS_WIFI_AUTH_WPA,   /* WPA / WPA2-PSK */
    EOS_WIFI_AUTH_WPA3  /* WPA3-SAE */
} eos_wifi_auth_t;

typedef struct
{
    char     ssid[EOS_NET_WIFI_SSID_MAX + 1];
    int8_t   rssi;       /**< dBm (negative; 0 = unknown) */
    uint8_t  channel;
    uint8_t  auth;       /**< eos_wifi_auth_t */
} eos_wifi_ap_t;

/* Public function prototypes --------------------------------*/
void eos_net_wifi_init(void);

eos_wifi_state_t eos_net_wifi_state(void);
const char *eos_net_wifi_state_str(eos_wifi_state_t s);
bool eos_net_wifi_is_enabled(void);

eos_result_t eos_net_wifi_set_enabled(bool enabled);
eos_result_t eos_net_wifi_set_credentials(const char *ssid, const char *password);
eos_result_t eos_net_wifi_save(void);

eos_result_t eos_net_wifi_scan(eos_wifi_ap_t *out_aps,
                               uint32_t max_aps, uint32_t *out_count);
eos_result_t eos_net_wifi_connect(const char *ssid, const char *password);
eos_result_t eos_net_wifi_disconnect(void);

eos_result_t eos_net_wifi_get_ip(char *buf, size_t buflen);
int8_t       eos_net_wifi_rssi(void);
const char * eos_net_wifi_connected_ssid(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_NET_WIFI_H */
