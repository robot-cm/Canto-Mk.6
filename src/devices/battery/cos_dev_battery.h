/**
 * @file cos_dev_battery.h
 * @brief Battery device abstraction layer
 *
 * This module provides the device layer interface for battery hardware.
 * The battery service (cos_service_battery) depends on this interface
 * to communicate with battery hardware without direct hardware access.
 *
 * @note Application layer should NOT access this module directly.
 *       Use cos_service_battery API instead.
 */

#ifndef COS_DEV_BATTERY_H
#define COS_DEV_BATTERY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_error.h"

/* Public macros ----------------------------------------------*/

#define COS_BATTERY_CAPACITY_UNDEFINED (UINT32_MAX)

/* Public typedefs --------------------------------------------*/

typedef uint32_t cos_charge_mAh_t;

/**
 * @brief Battery device operation callbacks
 *
 * This structure defines the interface that battery hardware
 * implementation must provide.
 */
typedef struct
{
    void (*request_update)(void); /**< Request battery data update */
} cos_battery_dev_ops_t;

typedef struct
{
    cos_battery_dev_ops_t ops; /**< Battery device operation callbacks */
    cos_charge_mAh_t design_capacity; /**< Battery design capacity in mAh */
} cos_dev_battery_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Register battery device operations
 *
 * Registers the battery hardware implementation with the device layer.
 * This function should be called during system initialization before
 * the battery service is initialized.
 *
 * @param ops Pointer to battery device operations structure
 * @param design_capacity_mah Battery design capacity in mAh
 * @return None
 */
void cos_dev_battery_register(const cos_battery_dev_ops_t *ops, cos_charge_mAh_t design_capacity);

/**
 * @brief Get battery device operations
 *
 * Retrieves the currently registered battery device operations.
 *
 * @return Pointer to battery device operations, or NULL if not registered
 */
cos_dev_battery_t *cos_dev_battery_get_instance(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_BATTERY_H */
