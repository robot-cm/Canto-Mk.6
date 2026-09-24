/**
 * @file cos_net_bt.h
 * @brief Bluetooth (BLE) service (Core system service)
 *
 * Bluetooth is a Core / System Service. On real hardware it drives the
 * ESP-IDF Bluedroid/NimBLE stack; on the simulator it runs a stub that emulates
 * scan/pair so the service logic and the Shell integration can be verified on PC.
 *
 * Enabled state is persisted under the existing system key `bluetooth`
 * (COS_CONFIG_KEY_BLUETOOTH_BOOL) — the Settings app and the control center
 * already toggle it. This service provides STRONG definitions of
 * cos_bluetooth_enable() / cos_bluetooth_disable() (declared in cos_port.h),
 * overriding the weak stubs in cos_port.c, so the existing UI routes here
 * automatically.
 *
 *   bluetooth        (bool)   — radio on/off  (COS_CONFIG_KEY_BLUETOOTH_BOOL)
 *   bluetooth.name   (string) — local device name
 *   bluetooth.discoverable (bool)
 */

#ifndef COS_NET_BT_H
#define COS_NET_BT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cos_core.h"

/* Public macros ----------------------------------------------*/
#define COS_NET_BT_KEY_NAME         "bluetooth.name"
#define COS_NET_BT_KEY_DISCOVERABLE "bluetooth.discoverable"

#define COS_NET_BT_NAME_MAX  32
#define COS_NET_BT_ADDR_MAX  18   /* "XX:XX:XX:XX:XX:XX\0" */
#define COS_NET_BT_SCAN_MAX  16   /* max devices returned per scan */

/* Public typedefs --------------------------------------------*/
typedef enum
{
    COS_BT_DISABLED = 0,  /**< radio off */
    COS_BT_IDLE,          /**< enabled, idle */
    COS_BT_SCANNING,      /**< discovery in progress */
    COS_BT_CONNECTED,     /**< connected to a device */
    COS_BT_ERROR          /**< last operation failed */
} cos_bt_state_t;

typedef enum
{
    COS_BT_DEV_UNKNOWN = 0,
    COS_BT_DEV_CLASSIC,
    COS_BT_DEV_LE
} cos_bt_dev_type_t;

typedef struct
{
    char            name[COS_NET_BT_NAME_MAX + 1];
    char            addr[COS_NET_BT_ADDR_MAX];  /* XX:XX:XX:XX:XX:XX */
    int8_t          rssi;
    uint8_t         type;   /* cos_bt_dev_type_t */
    bool            paired;
} cos_bt_device_t;

/* Public function prototypes --------------------------------*/
void cos_net_bt_init(void);

cos_bt_state_t cos_net_bt_state(void);
const char *cos_net_bt_state_str(cos_bt_state_t s);
bool cos_net_bt_is_enabled(void);

cos_result_t cos_net_bt_set_enabled(bool enabled);
cos_result_t cos_net_bt_set_name(const char *name);
cos_result_t cos_net_bt_set_discoverable(bool discoverable);
cos_result_t cos_net_bt_save(void);

cos_result_t cos_net_bt_scan(cos_bt_device_t *out_devs,
                             uint32_t max_devs, uint32_t *out_count);
cos_result_t cos_net_bt_connect(const char *addr);
cos_result_t cos_net_bt_disconnect(void);
cos_result_t cos_net_bt_get_paired(cos_bt_device_t *out_devs,
                                   uint32_t max_devs, uint32_t *out_count);

/* 设备连接记忆(保存到 SD /history/bt/history.txt;无 SD 则忽略) */
cos_result_t cos_net_bt_save_history(const char *addr, const char *name);
/* 扫描后自动连接记忆中最频繁连接的可见设备,成功后刷新记忆 */
cos_result_t cos_net_bt_connect_from_history(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_NET_BT_H */
