/**
 * @file eos_net_vpn.c
 * @brief VPN service - WireGuard/Tailscale via the microlink component
 *
 * Design:
 * - Config persisted under vpn_enabled / vpn_auth_key / vpn_device_name.
 *   The auth key is stored with the silent API (no plaintext logging).
 * - Real device (EOS_HAVE_MICROLINK defined by port/esp32s3 build):
 *   drives microlink_init/start/stop and mirrors its state.
 * - Simulator / other builds: config-only stub (state IDLE/DISABLED),
 *   so the Settings UI stays fully functional everywhere.
 */

#include <string.h>
#include "eos_net_vpn.h"
#include "eos_service_config.h"
#include "eos_log.h"

#ifdef EOS_HAVE_MICROLINK
#include "microlink.h"
#endif

/* Private macros ----------------------------------------------*/
#ifdef EOS_HAVE_MICROLINK
#define VPN_TASK_STACK 4096
#endif

/* Private variables -------------------------------------------*/
#ifdef EOS_HAVE_MICROLINK
static microlink_t *s_ml = NULL;
#endif
static eos_vpn_state_t s_state = EOS_VPN_STATE_DISABLED;

/* Forward declarations -----------------------------------------*/
#ifdef EOS_HAVE_MICROLINK
static void _vpn_ml_state_cb(microlink_t *ml, microlink_state_t st, void *user_data);
#endif

/* Private functions --------------------------------------------*/
#ifdef EOS_HAVE_MICROLINK
/* Map microlink state to our UI-facing state */
static eos_vpn_state_t _vpn_map_state(microlink_state_t st)
{
    switch (st)
    {
    case ML_STATE_CONNECTING:
    case ML_STATE_REGISTERING:
    case ML_STATE_RECONNECTING:
        return EOS_VPN_STATE_CONNECTING;
    case ML_STATE_CONNECTED:
        return EOS_VPN_STATE_CONNECTED;
    case ML_STATE_ERROR:
        return EOS_VPN_STATE_ERROR;
    case ML_STATE_WIFI_WAIT:
    case ML_STATE_IDLE:
    default:
        return EOS_VPN_STATE_IDLE;
    }
}

static void _vpn_ml_state_cb(microlink_t *ml, microlink_state_t st, void *user_data)
{
    (void)ml;
    (void)user_data;
    s_state = _vpn_map_state(st);
    EOS_LOG_I("VPN state -> %d", (int)s_state);
}
#endif /* EOS_HAVE_MICROLINK */

/* Public functions ---------------------------------------------*/
void eos_net_vpn_init(void)
{
    /* Read persisted state only - actual connect happens in set_enabled */
    if (eos_net_vpn_is_enabled())
    {
        s_state = EOS_VPN_STATE_IDLE;
    }
    else
    {
        s_state = EOS_VPN_STATE_DISABLED;
    }
    EOS_LOG_I("VPN service init: enabled=%d", eos_net_vpn_is_enabled());
}

bool eos_net_vpn_is_enabled(void)
{
    return eos_config_get_bool(EOS_CONFIG_KEY_VPN_ENABLED_BOOL, false);
}

eos_result_t eos_net_vpn_set_enabled(bool enabled)
{
    eos_result_t ret = EOS_OK;
    eos_config_set_bool(EOS_CONFIG_KEY_VPN_ENABLED_BOOL, enabled);

#ifdef EOS_HAVE_MICROLINK
    if (enabled)
    {
        if (s_ml)
        {
            /* Already running */
            return EOS_OK;
        }
        /* Need an auth key to start */
        char *auth_key = eos_net_vpn_get_auth_key();
        char *device_name = eos_net_vpn_get_device_name();
        if (!auth_key || !auth_key[0])
        {
            s_state = EOS_VPN_STATE_IDLE;
            EOS_LOG_W("VPN enable requested without auth key");
            if (auth_key) eos_free(auth_key);
            if (device_name) eos_free(device_name);
            return EOS_OK; /* config kept, state IDLE */
        }

        microlink_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.auth_key = auth_key;
        cfg.device_name = (device_name && device_name[0]) ? device_name : microlink_default_device_name();
        cfg.enable_derp = true;
        cfg.enable_stun = true;
        cfg.enable_disco = true;
        cfg.max_peers = 16;
        cfg.priority_peer_ip = 0;

        s_ml = microlink_init(&cfg);
        eos_free(auth_key);
        eos_free(device_name);
        if (!s_ml)
        {
            s_state = EOS_VPN_STATE_ERROR;
            eos_config_set_bool(EOS_CONFIG_KEY_VPN_ENABLED_BOOL, false);
            EOS_LOG_E("VPN: microlink_init failed");
            return EOS_ERR_MEM;
        }
        microlink_set_state_callback(s_ml, _vpn_ml_state_cb, NULL);
        if (microlink_start(s_ml) != ESP_OK)
        {
            s_state = EOS_VPN_STATE_ERROR;
            microlink_stop(s_ml);
            microlink_destroy(s_ml);
            s_ml = NULL;
            eos_config_set_bool(EOS_CONFIG_KEY_VPN_ENABLED_BOOL, false);
            EOS_LOG_E("VPN: microlink_start failed");
            return EOS_FAILED;
        }
        s_state = EOS_VPN_STATE_CONNECTING;
        EOS_LOG_I("VPN started (Tailscale/WireGuard)");
    }
    else
    {
        if (s_ml)
        {
            microlink_stop(s_ml);
            microlink_destroy(s_ml);
            s_ml = NULL;
        }
        s_state = EOS_VPN_STATE_DISABLED;
        EOS_LOG_I("VPN stopped");
    }
#else
    /* Non-ESP32 build (simulator): config-only stub */
    s_state = enabled ? EOS_VPN_STATE_IDLE : EOS_VPN_STATE_DISABLED;
    EOS_LOG_I("VPN %s (microlink not compiled in this build)", enabled ? "enabled" : "disabled");
#endif /* EOS_HAVE_MICROLINK */

    return ret;
}

eos_result_t eos_net_vpn_set_auth_key(const char *auth_key)
{
    if (!auth_key)
    {
        return EOS_ERR_INVALID_ARG;
    }
    /* Silent write: never log the secret */
    return eos_config_set_string_silent(EOS_CONFIG_KEY_VPN_AUTH_KEY_STR, auth_key);
}

eos_result_t eos_net_vpn_set_device_name(const char *name)
{
    if (!name)
    {
        return EOS_ERR_INVALID_ARG;
    }
    return eos_config_set_string(EOS_CONFIG_KEY_VPN_DEVICE_NAME_STR, name);
}

char *eos_net_vpn_get_auth_key(void)
{
    return eos_config_get_string(EOS_CONFIG_KEY_VPN_AUTH_KEY_STR, NULL);
}

char *eos_net_vpn_get_device_name(void)
{
    return eos_config_get_string(EOS_CONFIG_KEY_VPN_DEVICE_NAME_STR, NULL);
}

const char *eos_net_vpn_state_str(void)
{
    switch (s_state)
    {
    case EOS_VPN_STATE_DISABLED:
        return "Disabled";
    case EOS_VPN_STATE_CONNECTING:
        return "Connecting";
    case EOS_VPN_STATE_CONNECTED:
        return "Connected";
    case EOS_VPN_STATE_ERROR:
        return "Error";
    case EOS_VPN_STATE_IDLE:
    default:
        return "Idle";
    }
}
