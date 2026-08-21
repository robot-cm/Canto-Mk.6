/**
 * @file eos_net_sock_lwip.c
 * @brief Real-hardware socket backend for the SOCKS5 client, backed by LWIP.
 *
 * On the simulator, eos_net_sock_sim.c (a loopback SOCKS5 server) is used
 * instead. Exactly ONE of the two backends is compiled, selected by the
 * EOS_PLATFORM_ESP32 compile flag (set by the ESP-IDF / MCU build in
 * CMakeLists.txt). This file is therefore excluded from the Native simulator
 * build and is only exercised on real hardware.
 *
 * The SOCKS5 *client* logic lives in eos_net_proxy.c and is platform-agnostic;
 * this thin transport just opens a real TCP socket to the proxy and shuttles
 * bytes using LWIP's BSD-socket API.
 */

#if defined(EOS_PLATFORM_ESP32)

#include "eos_net_sock.h"
#include "eos_error.h"
#include "eos_log.h"
#include "eos_mem.h"

#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"

struct eos_net_sock_t
{
    int fd;
};

eos_result_t eos_net_sock_connect(const char *host, uint16_t port, eos_net_sock_t **out)
{
    if (!host || !out)
        return EOS_ERR_VAR_NULL;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return EOS_ERR_NET_SOCK;

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    dest_addr.sin_addr.s_addr = inet_addr(host); /* IPv4 literal; TODO: DNS */

    if (connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0)
    {
        close(fd);
        return EOS_ERR_NET_SOCK;
    }

    eos_net_sock_t *s = (eos_net_sock_t *)eos_malloc(sizeof(eos_net_sock_t));
    if (!s)
    {
        close(fd);
        return EOS_ERR_MEM;
    }
    s->fd = fd;
    *out = s;
    return EOS_OK;
}

eos_result_t eos_net_sock_send(eos_net_sock_t *s, const uint8_t *data, size_t len)
{
    if (!s || !data)
        return EOS_ERR_VAR_NULL;

    while (len > 0)
    {
        ssize_t n = send(s->fd, data, len, 0);
        if (n < 0)
            return EOS_ERR_NET_SOCK;
        data += n;
        len -= (size_t)n;
    }
    return EOS_OK;
}

eos_result_t eos_net_sock_recv(eos_net_sock_t *s, uint8_t *buf, size_t buflen, size_t *out_len)
{
    if (!s || !buf || !out_len)
        return EOS_ERR_VAR_NULL;

    ssize_t n = recv(s->fd, buf, buflen, 0);
    if (n < 0)
        return EOS_ERR_NET_SOCK;

    *out_len = (size_t)n;
    return EOS_OK;
}

void eos_net_sock_close(eos_net_sock_t *s)
{
    if (!s)
        return;
    close(s->fd);
    eos_free(s);
}

#endif /* EOS_PLATFORM_ESP32 */
