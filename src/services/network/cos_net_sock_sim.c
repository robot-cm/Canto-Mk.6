/**
 * @file cos_net_sock_sim.c
 * @brief Simulator socket backend: a REAL TCP transport to the proxy server.
 *
 * Previously this backend was a loopback SOCKS5 *server* that synthesized the
 * handshake replies in-process (no real network), so only the protocol logic
 * in cos_net_proxy.c was exercised. It is now a genuine TCP client on the host
 * (Winsock2 on Windows, POSIX sockets elsewhere), so `proxy connect <host>
 * <port>` actually opens a socket to the configured SOCKS5 proxy and performs
 * the full handshake over the real network.
 *
 * The SOCKS5 *client* logic in cos_net_proxy.c is unchanged and platform
 * agnostic; only this thin transport differs between targets.
 */

#include "cos_net_sock.h"
#include "cos_error.h"
#include "cos_log.h"
#include "cos_mem.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET cos_sockfd_t;
#define COS_SOCK_INVALID INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
typedef int cos_sockfd_t;
#define COS_SOCK_INVALID (-1)
#endif

#define COS_LOG_TAG "NetSock"

struct cos_net_sock_t
{
    cos_sockfd_t fd;
};

#ifdef _WIN32
static bool g_wsa_ready = false;

static bool _ensure_wsa(void)
{
    if (g_wsa_ready)
        return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        COS_LOG_E("WSAStartup failed");
        return false;
    }
    g_wsa_ready = true;
    return true;
}
#endif

static void _sock_close_fd(cos_sockfd_t fd)
{
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

cos_result_t cos_net_sock_connect(const char *host, uint16_t port, cos_net_sock_t **out)
{
    if (!host || !out)
        return COS_ERR_VAR_NULL;

#ifdef _WIN32
    if (!_ensure_wsa())
        return COS_ERR_NET_SOCK;
#endif

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;   /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0)
    {
        COS_LOG_E("getaddrinfo failed for %s:%u", host, (unsigned)port);
        return COS_ERR_NET_SOCK;
    }

    cos_sockfd_t fd = COS_SOCK_INVALID;
    struct addrinfo *ai;
    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == COS_SOCK_INVALID)
            continue;
        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0)
            break; /* connected */
        _sock_close_fd(fd);
        fd = COS_SOCK_INVALID;
    }
    freeaddrinfo(res);

    if (fd == COS_SOCK_INVALID)
    {
        COS_LOG_E("connect failed to %s:%u", host, (unsigned)port);
        return COS_ERR_NET_SOCK;
    }

    cos_net_sock_t *s = (cos_net_sock_t *)cos_malloc(sizeof(*s));
    if (!s)
    {
        _sock_close_fd(fd);
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
        int n = send(s->fd, (const char *)data, (int)len, 0);
        if (n <= 0)
            return COS_ERR_NET_SOCK; /* 0 = peer closed; guard against spin */
        data += n;
        len -= (size_t)n;
    }
    return COS_OK;
}

cos_result_t cos_net_sock_recv(cos_net_sock_t *s, uint8_t *buf, size_t buflen, size_t *out_len)
{
    if (!s || !buf || !out_len)
        return COS_ERR_VAR_NULL;

    int n = recv(s->fd, (char *)buf, (int)buflen, 0);
    if (n <= 0)
        return COS_ERR_NET_SOCK;

    *out_len = (size_t)n;
    return COS_OK;
}

void cos_net_sock_close(cos_net_sock_t *s)
{
    if (!s)
        return;
    _sock_close_fd(s->fd);
    cos_free(s);
}
