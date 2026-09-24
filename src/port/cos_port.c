/**
 * @file cos_port.c
 * @brief Canto Mk.6 porting
 */

#include "cos_port.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_log.h"
#include "cos_mem.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/
COS_WEAK void cos_delay(uint32_t ms)
{
    LV_UNUSED(ms);
    return;
}

COS_WEAK void cos_cpu_reset(void)
{
    return;
}

/* cos_bluetooth_enable / cos_bluetooth_disable are provided by the Bluetooth
 * system service (src/services/network/cos_net_bt.c) as strong definitions. */

/* Simulator stub: no real radio. The ESP32 backend (port/esp32s3/main/
 * cos_bt_esp32.c) overrides this with a strong NimBLE implementation. */
COS_WEAK cos_result_t cos_net_bt_backend_set_enabled(bool enabled, const char *name)
{
    LV_UNUSED(enabled);
    LV_UNUSED(name);
    return COS_OK;
}

COS_WEAK cos_result_t cos_net_bt_backend_power_down(void)
{
    return COS_OK;
}

COS_WEAK void cos_locate_phone(void)
{
    return;
}
