/**
 * @file eos_net_proxy.c
 * @brief SOCKS5 client implementation (Core system service).
 *
 * Implements the SOCKS5 protocol client side:
 *   - greeting (methods: no-auth 0x00, user/pass 0x02)
 *   - optional username/password auth (RFC 1929)
 *   - CONNECT request (IPv4 / domain name) and reply parsing
 *
 * The transport is abstracted by eos_net_sock (sim backend = loopback
 * SOCKS5 server, so the handshake is fully verifiable on PC). The protocol
 * logic here is platform-agnostic and identical on real hardware.
 */

#include "eos_net_proxy.h"
#include "eos_net_sock.h"
#include "eos_service_config.h"
#include "eos_error.h"
#include "eos_log.h"
#include "eos_mem.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#define EOS_NET_PROXY_LOG_TAG "SOCKS5"

/* Config keys (proxy.*) */
#define K_ENABLED  "proxy.enabled"
#define K_HOST     "proxy.host"
#define K_PORT     "proxy.port"
#define K_USER     "proxy.username"
#define K_PASS     "proxy.password"

#define HOST_MAX   128
#define USER_MAX   64
#define PASS_MAX   64
#define TUNNEL_MAX 8

/* Module state -------------------------------------------------*/
static bool                     g_enabled = false;
static char                     g_host[HOST_MAX] = {0};
static uint16_t                 g_port = 1080;
static char                     g_user[USER_MAX] = {0};
static char                     g_pass[PASS_MAX] = {0};
static bool                     g_auth = false;
static eos_net_proxy_status_t   g_status = EOS_NET_PROXY_DISABLED;
static int                      g_tunnels = 0;

/* SOCKS5 protocol helpers -------------------------------------*/

static size_t build_greeting(uint8_t *buf, bool with_auth)
{
    buf[0] = 0x05; /* VER */
    if (with_auth)
    {
        buf[1] = 0x02;       /* NMETHODS */
        buf[2] = 0x00;       /* no auth */
        buf[3] = 0x02;       /* user/pass */
        return 4;
    }
    buf[1] = 0x01;
    buf[2] = 0x00;
    return 3;
}

static eos_result_t parse_method(const uint8_t *buf, size_t len, uint8_t *method)
{
    if (len < 2 || buf[0] != 0x05)
        return EOS_ERR_NET_PROTOCOL;
    *method = buf[1];
    return EOS_OK;
}

static size_t build_auth(uint8_t *buf, const char *user, const char *pass)
{
    size_t ulen = strlen(user);
    size_t plen = strlen(pass);
    if (ulen > 255) ulen = 255;
    if (plen > 255) plen = 255;
    buf[0] = 0x01;                 /* VER (RFC 1929) */
    buf[1] = (uint8_t)ulen;
    memcpy(buf + 2, user, ulen);
    buf[2 + ulen] = (uint8_t)plen;
    memcpy(buf + 3 + ulen, pass, plen);
    return 3 + ulen + plen;
}

static eos_result_t parse_auth_status(const uint8_t *buf, size_t len, bool *ok)
{
    if (len < 2 || buf[0] != 0x01)
        return EOS_ERR_NET_PROTOCOL;
    *ok = (buf[1] == 0x00);
    return EOS_OK;
}

static bool parse_ipv4(const char *s, uint8_t out[4])
{
    int parts[4] = {0};
    int n = 0;
    const char *p = s;
    for (int i = 0; i < 4; i++)
    {
        if (!isdigit((unsigned char)*p))
            return false;
        long v = strtol(p, (char **)&p, 10);
        if (v < 0 || v > 255)
            return false;
        parts[i] = (int)v;
        n++;
        if (i < 3)
        {
            if (*p != '.')
                return false;
            p++;
        }
    }
    if (*p != '\0')
        return false;
    for (int i = 0; i < 4; i++)
        out[i] = (uint8_t)parts[i];
    return true;
}

/* Build a CONNECT request; returns total length. */
static size_t build_connect(uint8_t *buf, const char *host, uint16_t port)
{
    buf[0] = 0x05; /* VER */
    buf[1] = 0x01; /* CMD = CONNECT */
    buf[2] = 0x00; /* RSV */

    uint8_t ip[4];
    size_t pos;
    if (parse_ipv4(host, ip))
    {
        buf[3] = 0x01; /* ATYP = IPv4 */
        memcpy(buf + 4, ip, 4);
        pos = 8;
    }
    else
    {
        size_t dlen = strlen(host);
        if (dlen > 255) dlen = 255;
        buf[3] = 0x03; /* ATYP = DOMAIN */
        buf[4] = (uint8_t)dlen;
        memcpy(buf + 5, host, dlen);
        pos = 5 + dlen;
    }
    buf[pos++] = (uint8_t)((port >> 8) & 0xFF);
    buf[pos++] = (uint8_t)(port & 0xFF);
    return pos;
}

static eos_result_t parse_connect_reply(const uint8_t *buf, size_t len, uint8_t *rep)
{
    if (len < 2 || buf[0] != 0x05)
        return EOS_ERR_NET_PROTOCOL;
    *rep = buf[1];
    return EOS_OK;
}

/* Tunnel type -------------------------------------------------*/
struct eos_net_proxy_tunnel_t
{
    eos_net_sock_t *sock;
    char target[256];
    uint16_t target_port;
};

/* Lifecycle ---------------------------------------------------*/
void eos_net_proxy_init(void)
{
    g_enabled = eos_config_get_bool(K_ENABLED, false);

    char *h = eos_config_get_string(K_HOST, "");
    if (h)
    {
        strncpy(g_host, h, HOST_MAX - 1);
        eos_free(h);
    }
    g_port = (uint16_t)eos_config_get_number(K_PORT, 1080);

    char *u = eos_config_get_string(K_USER, "");
    if (u)
    {
        strncpy(g_user, u, USER_MAX - 1);
        eos_free(u);
    }
    char *p = eos_config_get_string(K_PASS, "");
    if (p)
    {
        strncpy(g_pass, p, PASS_MAX - 1);
        eos_free(p);
    }

    g_auth = (g_user[0] != '\0');
    g_tunnels = 0;
    g_status = g_enabled ? EOS_NET_PROXY_IDLE : EOS_NET_PROXY_DISABLED;

    EOS_LOG_I("[%s] init: enabled=%d server=%s:%u auth=%d",
              EOS_NET_PROXY_LOG_TAG, g_enabled, g_host, g_port, g_auth);
}

/* Configuration -----------------------------------------------*/
bool eos_net_proxy_is_enabled(void) { return g_enabled; }

eos_result_t eos_net_proxy_set_enabled(bool enabled)
{
    g_enabled = enabled;
    g_status = enabled ? EOS_NET_PROXY_IDLE : EOS_NET_PROXY_DISABLED;
    return eos_config_set_bool(K_ENABLED, enabled);
}

eos_result_t eos_net_proxy_set_server(const char *host, uint16_t port)
{
    if (!host)
        return EOS_ERR_VAR_NULL;
    strncpy(g_host, host, HOST_MAX - 1);
    g_host[HOST_MAX - 1] = '\0';
    g_port = port;
    eos_result_t r = eos_config_set_string(K_HOST, g_host);
    if (r == EOS_OK)
        r = eos_config_set_number(K_PORT, g_port);
    return r;
}

eos_result_t eos_net_proxy_set_credentials(const char *user, const char *pass)
{
    if (user)
    {
        strncpy(g_user, user, USER_MAX - 1);
        g_user[USER_MAX - 1] = '\0';
    }
    if (pass)
    {
        strncpy(g_pass, pass, PASS_MAX - 1);
        g_pass[PASS_MAX - 1] = '\0';
    }
    g_auth = (g_user[0] != '\0');
    eos_result_t r = eos_config_set_string(K_USER, g_user);
    if (r == EOS_OK)
        r = eos_config_set_string_silent(K_PASS, g_pass); /* never log password */
    return r;
}

eos_result_t eos_net_proxy_save(void)
{
    eos_result_t r = eos_config_set_bool(K_ENABLED, g_enabled);
    if (r == EOS_OK) r = eos_config_set_string(K_HOST, g_host);
    if (r == EOS_OK) r = eos_config_set_number(K_PORT, g_port);
    if (r == EOS_OK) r = eos_config_set_string(K_USER, g_user);
    if (r == EOS_OK) r = eos_config_set_string_silent(K_PASS, g_pass);
    return r;
}

/* Status / accessors ------------------------------------------*/
eos_net_proxy_status_t eos_net_proxy_status(void) { return g_status; }

bool eos_net_proxy_auth_configured(void) { return g_auth; }

const char *eos_net_proxy_host(void) { return g_host; }

uint16_t eos_net_proxy_port(void) { return g_port; }

const char *eos_net_proxy_server_str(char *buf, size_t buflen)
{
    if (!buf || buflen == 0)
        return "";
    if (g_host[0] == '\0')
    {
        strncpy(buf, "unset", buflen - 1);
        buf[buflen - 1] = '\0';
        return buf;
    }
    snprintf(buf, buflen, "%s:%u", g_host, g_port);
    return buf;
}

const char *eos_net_proxy_status_str(eos_net_proxy_status_t s)
{
    switch (s)
    {
        case EOS_NET_PROXY_DISABLED:   return "disabled";
        case EOS_NET_PROXY_IDLE:       return "idle";
        case EOS_NET_PROXY_CONNECTING: return "connecting";
        case EOS_NET_PROXY_CONNECTED:  return "connected";
        case EOS_NET_PROXY_ERROR:      return "error";
        default:                       return "?";
    }
}

/* Dial --------------------------------------------------------*/
eos_result_t eos_net_proxy_dial(const char *target_host, uint16_t target_port,
                                eos_net_proxy_tunnel_t **out)
{
    if (!target_host || !out)
        return EOS_ERR_VAR_NULL;
    if (g_host[0] == '\0')
    {
        g_status = EOS_NET_PROXY_ERROR;
        return EOS_ERR_NET_NOT_CONFIGURED;
    }

    g_status = EOS_NET_PROXY_CONNECTING;

    eos_net_sock_t *sock = NULL;
    eos_result_t r = eos_net_sock_connect(g_host, g_port, &sock);
    if (r != EOS_OK)
    {
        g_status = EOS_NET_PROXY_ERROR;
        return r;
    }

    uint8_t buf[600];
    size_t len = 0;
    uint8_t method = 0;

    /* 1) greeting */
    len = build_greeting(buf, g_auth);
    eos_net_sock_send(sock, buf, len);
    len = 0;
    eos_net_sock_recv(sock, buf, sizeof(buf), &len);
    r = parse_method(buf, len, &method);
    if (r != EOS_OK)
    {
        eos_net_sock_close(sock);
        g_status = EOS_NET_PROXY_ERROR;
        return r;
    }
    if (method == 0xFF)
    {
        eos_net_sock_close(sock);
        g_status = EOS_NET_PROXY_ERROR;
        return EOS_ERR_NET_HANDSHAKE; /* no acceptable method */
    }

    /* 2) optional username/password auth */
    if (method == 0x02)
    {
        len = build_auth(buf, g_user, g_pass);
        eos_net_sock_send(sock, buf, len);
        len = 0;
        eos_net_sock_recv(sock, buf, sizeof(buf), &len);
        bool ok = false;
        r = parse_auth_status(buf, len, &ok);
        if (r != EOS_OK || !ok)
        {
            eos_net_sock_close(sock);
            g_status = EOS_NET_PROXY_ERROR;
            return (r != EOS_OK) ? r : EOS_ERR_NET_HANDSHAKE;
        }
    }

    /* 3) CONNECT */
    len = build_connect(buf, target_host, target_port);
    eos_net_sock_send(sock, buf, len);
    len = 0;
    eos_net_sock_recv(sock, buf, sizeof(buf), &len);
    uint8_t rep = 0;
    r = parse_connect_reply(buf, len, &rep);
    if (r != EOS_OK || rep != 0x00)
    {
        eos_net_sock_close(sock);
        g_status = EOS_NET_PROXY_ERROR;
        return (r != EOS_OK) ? r : EOS_ERR_NET_HANDSHAKE;
    }

    eos_net_proxy_tunnel_t *t = (eos_net_proxy_tunnel_t *)eos_malloc(sizeof(*t));
    if (!t)
    {
        eos_net_sock_close(sock);
        g_status = EOS_NET_PROXY_ERROR;
        return EOS_ERR_MEM;
    }
    memset(t, 0, sizeof(*t));
    t->sock = sock;
    strncpy(t->target, target_host, sizeof(t->target) - 1);
    t->target_port = target_port;

    g_tunnels++;
    g_status = EOS_NET_PROXY_CONNECTED;
    *out = t;
    return EOS_OK;
}

eos_result_t eos_net_proxy_tunnel_send(eos_net_proxy_tunnel_t *t,
                                       const uint8_t *data, size_t len)
{
    if (!t || !t->sock || !data)
        return EOS_ERR_VAR_NULL;
    return eos_net_sock_send(t->sock, data, len);
}

eos_result_t eos_net_proxy_tunnel_recv(eos_net_proxy_tunnel_t *t,
                                       uint8_t *buf, size_t buflen, size_t *out_len)
{
    if (!t || !t->sock || !buf || !out_len)
        return EOS_ERR_VAR_NULL;
    return eos_net_sock_recv(t->sock, buf, buflen, out_len);
}

void eos_net_proxy_tunnel_close(eos_net_proxy_tunnel_t *t)
{
    if (!t)
        return;
    if (t->sock)
        eos_net_sock_close(t->sock);
    eos_free(t);
    if (g_tunnels > 0)
        g_tunnels--;
    g_status = g_enabled ? (g_tunnels > 0 ? EOS_NET_PROXY_CONNECTED : EOS_NET_PROXY_IDLE)
                         : EOS_NET_PROXY_DISABLED;
}
