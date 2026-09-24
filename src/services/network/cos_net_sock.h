/**
 * @file cos_net_sock.h
 * @brief Minimal TCP socket abstraction for the SOCKS5 client.
 *
 * On real hardware this is backed by LWIP (see cos_net_sock_lwip.c, TODO).
 * On the simulator it is a loopback backend (cos_net_sock_sim.c) that behaves
 * like a SOCKS5 server, so the proxy handshake can be verified end-to-end
 * without any actual network.
 *
 * The SOCKS5 *client* logic lives in cos_net_proxy.c and is platform-agnostic;
 * only this thin transport differs between targets.
 */

#ifndef COS_NET_SOCK_H
#define COS_NET_SOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "cos_core.h"

/* Opaque socket handle. */
typedef struct cos_net_sock_t cos_net_sock_t;

/**
 * @brief Open a TCP connection to host:port.
 * @return COS_OK on success, error code otherwise.
 */
cos_result_t cos_net_sock_connect(const char *host, uint16_t port, cos_net_sock_t **out);

/**
 * @brief Send raw bytes.
 */
cos_result_t cos_net_sock_send(cos_net_sock_t *s, const uint8_t *data, size_t len);

/**
 * @brief Receive raw bytes (fills up to buflen, writes actual length to *out_len).
 */
cos_result_t cos_net_sock_recv(cos_net_sock_t *s, uint8_t *buf, size_t buflen, size_t *out_len);

/**
 * @brief Close and free the socket.
 */
void cos_net_sock_close(cos_net_sock_t *s);

#ifdef __cplusplus
}
#endif

#endif /* COS_NET_SOCK_H */
