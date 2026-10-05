/**
 * @file FreeRTOS.h
 * @brief Minimal FreeRTOS shim for the desktop simulator (issue #3).
 *
 * Exists solely so CONFIG_PROFMONITOR_APP_ENABLE=1 can be enabled on the
 * simulator: src/apps/profmonitor/cos_profmonitor.c includes
 * "freertos/FreeRTOS.h" unguarded and derives CPU load from task run-time
 * counters via uxTaskGetSystemState(). There is no FreeRTOS on the host, so
 * the shim models a two-task single-core system driven by the simulator
 * process's own CPU time:
 *
 *   IDLE0 counter = wall ms - busy ms   (busy = process CPU time deltas)
 *   SIM  counter  = busy ms
 *
 * Feeding these into profmonitor's algorithm (load = (Σtasks - IDLEΔ)/ΣtasksΔ)
 * yields ~0% when the simulator process is idle and ~100% when it spins.
 *
 * Deviations from real FreeRTOS (documented, acceptable for a shim):
 *  - TaskStatus_t / uxTaskGetSystemState live here (profmonitor.c only
 *    includes FreeRTOS.h, not freertos/task.h).
 *  - portNUM_PROCESSORS = 1: the host model has no second core, so the
 *    CPU1 view in ProfMonitor reads a flat 0% (its sample path early-outs
 *    when core >= portNUM_PROCESSORS).
 *  - TaskStatus_t carries only the two fields profmonitor reads
 *    (pcTaskName / ulRunTimeCounter). Extend if future src/ code pulls
 *    more freertos headers onto the simulator path.
 *
 * Every other freertos include site in src/ is already guarded by
 * COS_PLATFORM_ESP32 / !COS_SIMULATOR / COS_RTOS_TYPE — verified before
 * adding this shim.
 */
#ifndef SIM_FREERTOS_SHIM_H
#define SIM_FREERTOS_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t UBaseType_t;
typedef uint32_t configRUN_TIME_COUNTER_TYPE;

#define portNUM_PROCESSORS 1

typedef struct
{
    const char *pcTaskName; /* profmonitor matches the "IDLE" prefix */
    configRUN_TIME_COUNTER_TYPE ulRunTimeCounter;
} TaskStatus_t;

/* Fills up to uxArraySize TaskStatus_t entries with the synthetic task set
 * (IDLE0 + SIM). Returns the number of entries written, or 0 on bad args.
 * pulTotalRunTime (optional) receives the synthetic wall-time counter. */
UBaseType_t uxTaskGetSystemState(TaskStatus_t *pxTaskStatusArray,
                                 UBaseType_t uxArraySize,
                                 uint32_t *pulTotalRunTime);

#ifdef __cplusplus
}
#endif

#endif /* SIM_FREERTOS_SHIM_H */
