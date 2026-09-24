/**
 * @file cos_port.h
 * @brief Canto Mk.6 porting
 */

#ifndef COS_PORT_H
#define COS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cos_core.h"
#include "cos_fs_port.h"
#include "cos_port_critical.h"

/* Public macros ----------------------------------------------*/
/**
 * @brief Function weak definition macro
 */
#ifdef __CC_ARM /* ARM Compiler */
#define COS_WEAK __weak
#elif defined(__IAR_SYSTEMS_ICC__) /* for IAR Compiler */
#define COS_WEAK __weak
#elif defined(__GNUC__) && !defined(__MINGW32__) /* GNU GCC Compiler */
#define COS_WEAK __attribute__((weak))
#elif defined(__ADSPBLACKFIN__) /* for VisualDSP++ Compiler */
#define COS_WEAK __attribute__((weak))
#elif defined(_MSC_VER) || defined(__MINGW32__)
#define COS_WEAK
#elif defined(__TI_COMPILER_VERSION__)
#define COS_WEAK
#else
#error not supported tool chain
#endif

#ifndef COS_TIMEOUT_INFINITE
#define COS_TIMEOUT_INFINITE UINT32_MAX
#endif

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Delay for specified time (non-blocking)
 * @param ms Milliseconds
 */
void cos_delay(uint32_t ms);
/**
 * @brief System reset
 */
void cos_cpu_reset();
/**
 * @brief Enable Bluetooth
 * @warning To avoid blocking UI thread, do not initialize Bluetooth protocol stack here, only send Bluetooth start message to other threads.
 * @note Creating threads also belongs to operations that easily block threads
 */
void cos_bluetooth_enable(void);
/**
 * @brief Disable Bluetooth
 */
void cos_bluetooth_disable(void);
/**
 * @brief Platform Bluetooth radio backend (advertising) control
 *
 * Called by the Bluetooth service (cos_net_bt.c) when the radio is toggled.
 * The simulator provides an empty weak stub; the ESP32 backend implements it
 * with NimBLE GAP advertising so the device is discoverable as @p name.
 *
 * @param enabled true to start the radio/advertising, false to stop it
 * @param name    BLE advertisement name (e.g. "Canto Mk.6")
 * @return COS_OK on success, COS_ERR_NET_BT on backend failure
 */
cos_result_t cos_net_bt_backend_set_enabled(bool enabled, const char *name);
/**
 * @brief Fully deinitialize the BLE controller for a long-lived power-save
 * mode. Unlike disabling advertising, this releases the PM locks held by the
 * active controller. The next enable performs a clean lazy initialization.
 */
cos_result_t cos_net_bt_backend_power_down(void);
/**
 * @brief Locate phone
 *
 * Make phone ring via Bluetooth or other methods to locate phone.
 */
void cos_locate_phone(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_PORT_H */
