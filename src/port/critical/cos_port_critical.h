/**
 * @file cos_port_critical.h
 * @brief Critical section porting abstraction
 *
 * Provides enter/leave critical section API across different platforms.
 *
 * Bare metal:    Disable/enable interrupts (saves/restores previous state)
 * FreeRTOS:      taskENTER_CRITICAL / taskEXIT_CRITICAL
 * RT-Thread:     rt_hw_interrupt_disable / rt_hw_interrupt_enable
 * POSIX:         pthread_mutex_lock / pthread_mutex_unlock (simulated)
 * Custom:        User-defined implementation
 */

#ifndef COS_PORT_CRITICAL_H
#define COS_PORT_CRITICAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>

/* Public typedefs --------------------------------------------*/

/**
 * @brief Context type for saving interrupt/CPU state on entry
 *
 * - Bare metal: stores the previous interrupt mask state
 * - RTOSes with interrupt control: stores the previous interrupt mask state
 * - POSIX: unused (0), but kept for API uniformity
 */
typedef uint32_t cos_critical_ctx_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Enter critical section
 *
 * Disables interrupts (or acquires scheduler lock) to protect
 * the following code from being preempted.
 *
 * @return Previous CPU/interrupt state context, must be passed to cos_critical_leave()
 */
cos_critical_ctx_t cos_critical_enter(void);

/**
 * @brief Leave critical section
 *
 * Restores the CPU/interrupt state saved by cos_critical_enter().
 *
 * @param ctx Context returned from cos_critical_enter()
 */
void cos_critical_leave(cos_critical_ctx_t ctx);

#ifdef __cplusplus
}
#endif

#endif /* COS_PORT_CRITICAL_H */
