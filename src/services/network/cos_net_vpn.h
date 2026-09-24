/**
 * @file cos_net_vpn.h
 * @brief VPN service - WireGuard/Tailscale via the microlink component
 *
 * Replaces the old SOCKS5 proxy UI in Settings. On the real device
 * (COS_HAVE_MICROLINK) this drives the microlink (Tailscale client)
 * component; on the simulator / non-ESP32 builds it is a config-only
 * stub so the UI and config persistence stay fully functional.
 *
 * Auth key and device name are persisted in the system config
 * (vpn_enabled / vpn_auth_key / vpn_device_name). The auth key is
 * written with the silent API so it never appears in plaintext logs.
 */

#ifndef COS_NET_VPN_H
#define COS_NET_VPN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>
#include "cos_core.h"

/* Public typedefs --------------------------------------------*/
/* VPN connection state (UI-facing) */
typedef enum
{
    COS_VPN_STATE_DISABLED = 0,   /* VPN disabled by user */
    COS_VPN_STATE_IDLE,           /* enabled but not connected (no key / wifi) */
    COS_VPN_STATE_CONNECTING,     /* handshaking with control plane */
    COS_VPN_STATE_CONNECTED,      /* tunnel up */
    COS_VPN_STATE_ERROR,          /* auth key rejected / init failed */
} cos_vpn_state_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize the VPN service (load config). Safe to call multiple times.
 */
void cos_net_vpn_init(void);

/**
 * @brief Check whether the VPN is currently enabled
 * @return true if enabled
 */
bool cos_net_vpn_is_enabled(void);

/**
 * @brief Enable or disable the VPN
 * @param enabled New state
 * @return COS_OK on success
 *
 * On the real device this starts/stops microlink (microlink_start/stop).
 * Enabling without an auth key falls back to IDLE (config kept).
 */
cos_result_t cos_net_vpn_set_enabled(bool enabled);

/**
 * @brief Set the Tailscale auth key (tskey-...)
 * @param auth_key Auth key string (copied silently, never logged)
 * @return COS_OK on success
 */
cos_result_t cos_net_vpn_set_auth_key(const char *auth_key);

/**
 * @brief Set the device hostname shown on the tailnet
 * @param name Device name string
 * @return COS_OK on success
 */
cos_result_t cos_net_vpn_set_device_name(const char *name);

/**
 * @brief Get the current auth key (copy; caller frees with cos_free)
 * @return Auth key string or NULL
 */
char *cos_net_vpn_get_auth_key(void);

/**
 * @brief Get the current device name (copy; caller frees with cos_free)
 * @return Device name string or NULL
 */
char *cos_net_vpn_get_device_name(void);

/**
 * @brief Get a short human-readable state string for the UI
 * @return Static English string ("Disabled"/"Idle"/"Connecting"/"Connected"/"Error")
 */
const char *cos_net_vpn_state_str(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_NET_VPN_H */
