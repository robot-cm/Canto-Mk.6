/**
 * @file eos_net_sock_sim.c
 * @brief Simulator socket backend: a REAL TCP transport to the proxy server.
 *
 * Previously this backend was a loopback SOCKS5 *server* that synthesized the
 * handshake replies in-process (no real network), so only the protocol logic
 * in eos_net_proxy.c was exercised. It is now a genuine TCP client on the host
 * (Winsock2 on Windows, POSIX sockets elsewhere), so `proxy connect <host>
 * <port>` actually opens a socket to the configured SOCKS5 proxy and performs
 * the full handshake over the real network.
 *
 * The SOCKS5 *client* logic in eos_net_proxy.c is unchanged and platform
 * agnostic; only this thin transport differs between targets.
 */

#include "eos_net_sock.h"
#include "eos_error.h"
#include "eos_log.h"
#include "eos_mem.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET eos_sockfd_t;
#define EOS_SOCK_INVALID INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
typedef int eos_sockfd_t;
#define EOS_SOCK_INVALID (-1)
#endif

#define EOS_LOG_TAG "NetSock"

struct eos_net_sock_t
{
    eos_sockfd_t fd;
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
        EOS_LOG_E("WSAStartup failed");
        return false;
    }
    g_wsa_ready = true;
    return true;
}
#endif

static void _sock_close_fd(eos_sockfd_t fd)
{
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

eos_result_t eos_net_sock_connect(const char *host, uint16_t port, eos_net_sock_t **out)
{
    if (!host || !out)
        return EOS_ERR_VAR_NULL;

#ifdef _WIN32
    if (!_ensure_wsa())
        return EOS_ERR_NET_SOCK;
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
        EOS_LOG_E("getaddrinfo failed for %s:%u", host, (unsigned)port);
        return EOS_ERR_NET_SOCK;
    }

    eos_sockfd_t fd = EOS_SOCK_INVALID;
    struct addrinfo *ai;
    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == EOS_SOCK_INVALID)
            continue;
        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0)
            break; /* connected */
        _sock_close_fd(fd);
        fd = EOS_SOCK_INVALID;
    }
    freeaddrinfo(res);

    if (fd == EOS_SOCK_INVALID)
    {
        EOS_LOG_E("connect failed to %s:%u", host, (unsigned)port);
        return EOS_ERR_NET_SOCK;
    }

    eos_net_sock_t *s = (eos_net_sock_t *)eos_malloc(sizeof(*s));
    if (!s)
    {
        _sock_close_fd(fd);
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
        int n = send(s->fd, (const char *)data, (int)len, 0);
        if (n <= 0)
            return EOS_ERR_NET_SOCK; /* 0 = peer closed; guard against spin */
        data += n;
        len -= (size_t)n;
    }
    return EOS_OK;
}

eos_result_t eos_net_sock_recv(eos_net_sock_t *s, uint8_t *buf, size_t buflen, size_t *out_len)
{
    if (!s || !buf || !out_len)
        return EOS_ERR_VAR_NULL;

    int n = recv(s->fd, (char *)buf, (int)buflen, 0);
    if (n <= 0)
        return EOS_ERR_NET_SOCK;

    *out_len = (size_t)n;
    return EOS_OK;
}

void eos_net_sock_close(eos_net_sock_t *s)
{
    if (!s)
        return;
    _sock_close_fd(s->fd);
    eos_free(s);
}
