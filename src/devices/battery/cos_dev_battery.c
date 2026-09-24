/**
 * @file cos_dev_battery.c
 * @brief Battery device abstraction layer implementation
 */

#include "cos_dev_battery.h"

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#define COS_LOG_TAG "DevBattery"
#include "cos_log.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/
/**
 * @brief Battery device singleton instance
 */
static cos_dev_battery_t _battery_dev = {0};

/* Function Implementations -----------------------------------*/

void cos_dev_battery_register(const cos_battery_dev_ops_t *ops, cos_charge_mAh_t design_capacity)
{
    if (!ops)
    {
        COS_LOG_E("Register failed: ops is NULL");
        return;
    }

    if (!ops->request_update)
    {
        COS_LOG_E("Register failed: request_update is required");
        return;
    }

    _battery_dev.ops = *ops;
    _battery_dev.design_capacity = design_capacity;

    if (design_capacity != COS_BATTERY_CAPACITY_UNDEFINED)
    {
        COS_LOG_I("Battery device registered, design capacity: %d mAh", design_capacity);
    }
    else
    {
        COS_LOG_E("Battery device registered with undefined design capacity");
    }
}

cos_dev_battery_t *cos_dev_battery_get_instance(void)
{
    return &_battery_dev;
}
