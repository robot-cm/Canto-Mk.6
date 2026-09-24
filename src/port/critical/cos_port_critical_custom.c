/**
 * @file cos_port_critical_custom.c
 * @brief Custom RTOS critical section implementation (default fallback)
 *
 * Users must implement cos_critical_enter() and cos_critical_leave()
 * according to their RTOS or hardware:
 *
 *   - On bare metal:     disable/enable interrupts
 *   - On an RTOS:        use the RTOS's scheduler lock / interrupt control
 *   - On other systems:  use the appropriate synchronization primitive
 *
 * The default stubs below do nothing and provide NO protection.
 */

#include "cos_config.h"

#if COS_RTOS_TYPE == COS_RTOS_CUSTOM

#include "cos_port_critical.h"

/* Includes ---------------------------------------------------*/
#include "cos_port.h"

COS_WEAK cos_critical_ctx_t cos_critical_enter(void)
{
    return 0;
}

COS_WEAK void cos_critical_leave(cos_critical_ctx_t ctx)
{
    (void)ctx;
}

#endif /* COS_RTOS_TYPE == COS_RTOS_CUSTOM */
