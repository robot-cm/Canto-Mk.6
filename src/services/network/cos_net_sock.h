/**
 * @file eos_net_sock.h
 * @brief Minimal TCP socket abstraction for the SOCKS5 client.
 *
 * On real hardware this is backed by LWIP (see eos_net_sock_lwip.c, TODO).
 * On the simulator it is a loopback backend (eos_net_sock_sim.c) that behaves
 * like a SOCKS5 server, so the proxy handshake can be verified end-to-end
 * without any actual network.
 *
 * The SOCKS5 *client* logic lives in eos_net_proxy.c and is platform-agnostic;
 * only this thin transport differs between targets.
 */

#ifndef EOS_NET_SOCK_H
#define EOS_NET_SOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "eos_core.h"

/* Opaque socket handle. */
typedef struct eos_net_sock_t eos_net_sock_t;

/**
 * @brief Open a TCP connection to host:port.
 * @return EOS_OK on success, error code otherwise.
 */
eos_result_t eos_net_sock_connect(const char *host, uint16_t port, eos_net_sock_t **out);

/**
 * @brief Send raw bytes.
 */
eos_result_t eos_net_sock_send(eos_net_sock_t *s, const uint8_t *data, size_t len);

/**
 * @brief Receive raw bytes (fills up to buflen, writes actual length to *out_len).
 */
eos_result_t eos_net_sock_recv(eos_net_sock_t *s, uint8_t *buf, size_t buflen, size_t *out_len);

/**
 * @brief Close and free the socket.
 */
void eos_net_sock_close(eos_net_sock_t *s);

#ifdef __cplusplus
}
#endif

#endif /* EOS_NET_SOCK_H */
