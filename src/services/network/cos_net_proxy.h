/**
 * @file cos_net_proxy.h
 * @brief SOCKS5 client — a Core / System Service.
 *
 * Canto Mk.6 acts as a SOCKS5 *client*: it connects to an external SOCKS5 proxy
 * server and tunnels outbound TCP through it. This is NOT a SOCKS5 server and
 * is NOT an app — it belongs to Core so that every network app routes through
 * one unified Network API instead of re-implementing SOCKS5.
 *
 *   APP -> Network API -> Network Service -> SOCKS5 Client -> TCP/IP -> Wi-Fi
 *                                                          -> SOCKS5 Server -> Internet
 *
 * Configuration is persisted under `proxy.*` in the system config:
 *   proxy.enabled  (bool)
 *   proxy.host     (string)
 *   proxy.port     (number)
 *   proxy.username (string)
 *   proxy.password (string, stored via silent setter — never logged)
 *
 * The password is NEVER exposed by any status/string accessor.
 */

#ifndef COS_NET_PROXY_H
#define COS_NET_PROXY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cos_core.h"

/* Service lifecycle state (config-driven, not per-tunnel). */
typedef enum
{
    COS_NET_PROXY_DISABLED = 0, /**< proxy.enabled == false */
    COS_NET_PROXY_IDLE,         /**< enabled, no active tunnel */
    COS_NET_PROXY_CONNECTING,   /**< a dial is in progress */
    COS_NET_PROXY_CONNECTED,    /**< at least one tunnel open */
    COS_NET_PROXY_ERROR         /**< last dial failed */
} cos_net_proxy_status_t;

/* Opaque tunnel returned by cos_net_proxy_dial(). */
typedef struct cos_net_proxy_tunnel_t cos_net_proxy_tunnel_t;

/* Lifecycle ---------------------------------------------------*/
void cos_net_proxy_init(void);

/* Configuration (persisted under proxy.*) ---------------------*/
bool cos_net_proxy_is_enabled(void);
cos_result_t cos_net_proxy_set_enabled(bool enabled);
cos_result_t cos_net_proxy_set_server(const char *host, uint16_t port);
cos_result_t cos_net_proxy_set_credentials(const char *user, const char *pass);
cos_result_t cos_net_proxy_save(void);

/* Accessors (safe to print) -----------------------------------*/
cos_net_proxy_status_t cos_net_proxy_status(void);
bool cos_net_proxy_auth_configured(void);
const char *cos_net_proxy_host(void);
uint16_t cos_net_proxy_port(void);
/* Writes "host:port" into buf (buf must be >= 32). Returns buf, or "unset". */
const char *cos_net_proxy_server_str(char *buf, size_t buflen);
const char *cos_net_proxy_status_str(cos_net_proxy_status_t s);

/* Dial a target through the SOCKS5 proxy (full handshake).
 * On the simulator the tunnel is a loopback, proving the handshake logic.
 * Returns COS_OK and *out on success. */
cos_result_t cos_net_proxy_dial(const char *target_host, uint16_t target_port,
                                 cos_net_proxy_tunnel_t **out);
cos_result_t cos_net_proxy_tunnel_send(cos_net_proxy_tunnel_t *t,
                                       const uint8_t *data, size_t len);
cos_result_t cos_net_proxy_tunnel_recv(cos_net_proxy_tunnel_t *t,
                                       uint8_t *buf, size_t buflen, size_t *out_len);
void cos_net_proxy_tunnel_close(cos_net_proxy_tunnel_t *t);

#ifdef __cplusplus
}
#endif

#endif /* COS_NET_PROXY_H */
