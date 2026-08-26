/**
 * @file eos_port.c
 * @brief Canto Mk.6 porting
 */

#include "eos_port.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eos_log.h"
#include "eos_mem.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/
EOS_WEAK void eos_delay(uint32_t ms)
{
    LV_UNUSED(ms);
    return;
}

EOS_WEAK void eos_cpu_reset(void)
{
    return;
}

/* eos_bluetooth_enable / eos_bluetooth_disable are provided by the Bluetooth
 * system service (src/services/network/eos_net_bt.c) as strong definitions. */

/* Simulator stub: no real radio. The ESP32 backend (port/esp32s3/main/
 * eos_bt_esp32.c) overrides this with a strong NimBLE implementation. */
EOS_WEAK eos_result_t eos_net_bt_backend_set_enabled(bool enabled, const char *name)
{
    LV_UNUSED(enabled);
    LV_UNUSED(name);
    return EOS_OK;
}

EOS_WEAK void eos_locate_phone(void)
{
    return;
}
