/**
 * @file cos_port_critical_rtthread.c
 * @brief RT-Thread critical section implementation
 *
 * Uses rt_hw_interrupt_disable() / rt_hw_interrupt_enable()
 * to protect critical sections.
 *
 * NOTE: The return value of rt_hw_interrupt_disable() is the
 * previous interrupt mask level, which is passed to
 * rt_hw_interrupt_enable() for proper nesting support.
 */

#include "cos_config.h"

#if COS_RTOS_TYPE == COS_RTOS_RTTHREAD

#include "cos_port_critical.h"

/* Includes ---------------------------------------------------*/
#include <rtthread.h>
#include "cos_port.h"

cos_critical_ctx_t cos_critical_enter(void)
{
    return (cos_critical_ctx_t)rt_hw_interrupt_disable();
}

void cos_critical_leave(cos_critical_ctx_t ctx)
{
    rt_hw_interrupt_enable((rt_base_t)ctx);
}

#endif /* COS_RTOS_TYPE == COS_RTOS_RTTHREAD */
