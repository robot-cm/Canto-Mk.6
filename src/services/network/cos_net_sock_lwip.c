/**
 * @file cos_net_sock_lwip.c
 * @brief Real-hardware socket backend for the SOCKS5 client, backed by LWIP.
 *
 * On the simulator, cos_net_sock_sim.c (a loopback SOCKS5 server) is used
 * instead. Exactly ONE of the two backends is compiled, selected by the
 * COS_PLATFORM_ESP32 compile flag (set by the ESP-IDF / MCU build in
 * CMakeLists.txt). This file is therefore excluded from the Native simulator
 * build and is only exercised on real hardware.
 *
 * The SOCKS5 *client* logic lives in cos_net_proxy.c and is platform-agnostic;
 * this thin transport just opens a real TCP socket to the proxy and shuttles
 * bytes using LWIP's BSD-socket API.
 */

#if defined(COS_PLATFORM_ESP32)

#include "cos_net_sock.h"
#include "cos_error.h"
#include "cos_log.h"
#include "cos_mem.h"

#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"

struct cos_net_sock_t
{
    int fd;
};

cos_result_t cos_net_sock_connect(const char *host, uint16_t port, cos_net_sock_t **out)
{
    if (!host || !out)
        return COS_ERR_VAR_NULL;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return COS_ERR_NET_SOCK;

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    dest_addr.sin_addr.s_addr = inet_addr(host); /* IPv4 literal; TODO: DNS */

    if (connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0)
    {
        close(fd);
        return COS_ERR_NET_SOCK;
    }

    cos_net_sock_t *s = (cos_net_sock_t *)cos_malloc(sizeof(cos_net_sock_t));
    if (!s)
    {
        close(fd);
        return COS_ERR_MEM;
    }
    s->fd = fd;
    *out = s;
    return COS_OK;
}

cos_result_t cos_net_sock_send(cos_net_sock_t *s, const uint8_t *data, size_t len)
{
    if (!s || !data)
        return COS_ERR_VAR_NULL;

    while (len > 0)
    {
        ssize_t n = send(s->fd, data, len, 0);
        if (n < 0)
            return COS_ERR_NET_SOCK;
        data += n;
        len -= (size_t)n;
    }
    return COS_OK;
}

cos_result_t cos_net_sock_recv(cos_net_sock_t *s, uint8_t *buf, size_t buflen, size_t *out_len)
{
    if (!s || !buf || !out_len)
        return COS_ERR_VAR_NULL;

    ssize_t n = recv(s->fd, buf, buflen, 0);
    if (n < 0)
        return COS_ERR_NET_SOCK;

    *out_len = (size_t)n;
    return COS_OK;
}

void cos_net_sock_close(cos_net_sock_t *s)
{
    if (!s)
        return;
    close(s->fd);
    cos_free(s);
}

#endif /* COS_PLATFORM_ESP32 */
