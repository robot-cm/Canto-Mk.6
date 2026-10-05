/**
 * @file freertos_shim.c
 * @brief Host implementation of the FreeRTOS shim (see freertos/FreeRTOS.h).
 *
 * CPU-time source:
 *  - Windows: GetProcessTimes (kernel+user time of the simulator process)
 *  - POSIX:   getrusage(RUSAGE_SELF) (ru_utime + ru_stime)
 *
 * The synthetic task table is built on demand from busy/wall milliseconds:
 *   busy_ms  = 100 * (kernel+user seconds) rounded
 *   wall_ms  = monotonic clock
 *   IDLE0.ulRunTimeCounter = wall_ms - busy_ms
 *   SIM.ulRunTimeCounter   = busy_ms
 */
#include "freertos/FreeRTOS.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>   /* clock_gettime / CLOCK_MONOTONIC */
#endif

/* ── busy CPU milliseconds of the whole process ─────────────────────── */
static uint64_t _sim_busy_ms(void)
{
#ifdef _WIN32
    FILETIME ct, et, kt, ut;
    if (!GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut))
        return 0;
    uint64_t k = ((uint64_t)kt.dwHighDateTime << 32) | kt.dwLowDateTime;
    uint64_t u = ((uint64_t)ut.dwHighDateTime << 32) | ut.dwLowDateTime;
    /* FILETIME is 100 ns units; /10000 → ms (rounding: +50 units) */
    return ((k + u) + 5000u) / 10000u;
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    uint64_t us = (uint64_t)ru.ru_utime.tv_sec * 1000000u
                + (uint64_t)ru.ru_utime.tv_usec
                + (uint64_t)ru.ru_stime.tv_sec * 1000000u
                + (uint64_t)ru.ru_stime.tv_usec;
    return (us + 500u) / 1000u; /* µs → ms */
#endif
}

/* ── monotonic wall milliseconds ────────────────────────────────────── */
static uint64_t _sim_wall_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

UBaseType_t uxTaskGetSystemState(TaskStatus_t *pxTaskStatusArray,
                                 UBaseType_t uxArraySize,
                                 uint32_t *pulTotalRunTime)
{
    if (pxTaskStatusArray == NULL || uxArraySize < 2)
        return 0;

    uint64_t busy = _sim_busy_ms();
    uint64_t wall = _sim_wall_ms();
    uint64_t idle = (wall > busy) ? (wall - busy) : 0;

    pxTaskStatusArray[0].pcTaskName       = "IDLE0";
    pxTaskStatusArray[0].ulRunTimeCounter = (configRUN_TIME_COUNTER_TYPE)idle;
    pxTaskStatusArray[1].pcTaskName       = "SIM";
    pxTaskStatusArray[1].ulRunTimeCounter = (configRUN_TIME_COUNTER_TYPE)busy;

    if (pulTotalRunTime)
        *pulTotalRunTime = (uint32_t)wall;

    return 2;
}
