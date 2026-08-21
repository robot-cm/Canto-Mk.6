/**
 * @file eos_port.c
 * @brief ElenixOS porting
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

EOS_WEAK void eos_locate_phone(void)
{
    return;
}
