/**
 * @file cos_port_critical_freertos.c
 * @brief FreeRTOS critical section implementation
 *
 * Uses taskENTER_CRITICAL() / taskEXIT_CRITICAL() to protect
 * critical sections from being interrupted by other tasks
 * and ISRs.
 *
 * NOTE: taskENTER_CRITICAL() can only be called from task context,
 * not from ISRs. For ISR protection, use:
 *   UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
 *   taskEXIT_CRITICAL_FROM_ISR(saved);
 */

#include "cos_config.h"

#if COS_RTOS_TYPE == COS_RTOS_FREERTOS

#include "cos_port_critical.h"

/* Includes ---------------------------------------------------*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cos_port.h"

/* Spinlock serializing critical sections across cores/ISRs.
 * ESP-IDF (Xtensa) requires a portMUX_TYPE argument. */
static portMUX_TYPE s_critical_mux = portMUX_INITIALIZER_UNLOCKED;

cos_critical_ctx_t cos_critical_enter(void)
{
    taskENTER_CRITICAL(&s_critical_mux);
    return 1;
}

void cos_critical_leave(cos_critical_ctx_t ctx)
{
    (void)ctx;
    taskEXIT_CRITICAL(&s_critical_mux);
}

#endif /* COS_RTOS_TYPE == COS_RTOS_FREERTOS */
