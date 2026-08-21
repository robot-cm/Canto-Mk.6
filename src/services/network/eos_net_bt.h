/**
 * @file eos_net_bt.h
 * @brief Bluetooth (BLE) service (Core system service)
 *
 * Bluetooth is a Core / System Service. On real hardware it drives the
 * ESP-IDF Bluedroid/NimBLE stack; on the simulator it runs a stub that emulates
 * scan/pair so the service logic and the Shell integration can be verified on PC.
 *
 * Enabled state is persisted under the existing system key `bluetooth`
 * (EOS_CONFIG_KEY_BLUETOOTH_BOOL) — the Settings app and the control center
 * already toggle it. This service provides STRONG definitions of
 * eos_bluetooth_enable() / eos_bluetooth_disable() (declared in eos_port.h),
 * overriding the weak stubs in eos_port.c, so the existing UI routes here
 * automatically.
 *
 *   bluetooth        (bool)   — radio on/off  (EOS_CONFIG_KEY_BLUETOOTH_BOOL)
 *   bluetooth.name   (string) — local device name
 *   bluetooth.discoverable (bool)
 */

#ifndef EOS_NET_BT_H
#define EOS_NET_BT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "eos_core.h"

/* Public macros ----------------------------------------------*/
#define EOS_NET_BT_KEY_NAME         "bluetooth.name"
#define EOS_NET_BT_KEY_DISCOVERABLE "bluetooth.discoverable"

#define EOS_NET_BT_NAME_MAX  32
#define EOS_NET_BT_ADDR_MAX  18   /* "XX:XX:XX:XX:XX:XX\0" */
#define EOS_NET_BT_SCAN_MAX  16   /* max devices returned per scan */

/* Public typedefs --------------------------------------------*/
typedef enum
{
    EOS_BT_DISABLED = 0,  /**< radio off */
    EOS_BT_IDLE,          /**< enabled, idle */
    EOS_BT_SCANNING,      /**< discovery in progress */
    EOS_BT_CONNECTED,     /**< connected to a device */
    EOS_BT_ERROR          /**< last operation failed */
} eos_bt_state_t;

typedef enum
{
    EOS_BT_DEV_UNKNOWN = 0,
    EOS_BT_DEV_CLASSIC,
    EOS_BT_DEV_LE
} eos_bt_dev_type_t;

typedef struct
{
    char            name[EOS_NET_BT_NAME_MAX + 1];
    char            addr[EOS_NET_BT_ADDR_MAX];  /* XX:XX:XX:XX:XX:XX */
    int8_t          rssi;
    uint8_t         type;   /* eos_bt_dev_type_t */
    bool            paired;
} eos_bt_device_t;

/* Public function prototypes --------------------------------*/
void eos_net_bt_init(void);

eos_bt_state_t eos_net_bt_state(void);
const char *eos_net_bt_state_str(eos_bt_state_t s);
bool eos_net_bt_is_enabled(void);

eos_result_t eos_net_bt_set_enabled(bool enabled);
eos_result_t eos_net_bt_set_name(const char *name);
eos_result_t eos_net_bt_set_discoverable(bool discoverable);
eos_result_t eos_net_bt_save(void);

eos_result_t eos_net_bt_scan(eos_bt_device_t *out_devs,
                             uint32_t max_devs, uint32_t *out_count);
eos_result_t eos_net_bt_connect(const char *addr);
eos_result_t eos_net_bt_disconnect(void);
eos_result_t eos_net_bt_get_paired(eos_bt_device_t *out_devs,
                                   uint32_t max_devs, uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* EOS_NET_BT_H */
