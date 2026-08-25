/**
 * @file jerry_port.c
 * @brief JerryScript Port for ElenixOS
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "jerryscript-port.h"
#include "script_engine_core.h"

void JERRY_ATTR_NORETURN jerry_port_fatal(jerry_fatal_code_t code)
{
    printf("[JerryScript] Fatal error: code=%d\r\n", (int)code);

    if (!script_engine_is_fatal_scope_active())
    {
        /*
         * longjmp to an invalid setjmp frame is undefined behavior.
         * After script_engine_run() returns, its stack frame is gone.
         * This should never happen, but if it does, abort is safer
         * than corrupting memory via longjmp to a dead frame.
         */
        abort();
    }

    if (script_engine_is_fatal_recovering())
    {
        /* Prevent infinite longjmp loop if setjmp recovery itself faults */
        abort();
    }
    script_engine_set_fatal_recovering(true);

    script_engine_fatal_longjmp((int)(code != 0 ? code : -1));
    /* unreachable */
    while (1)
    {
    }
}

double jerry_port_current_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((double)tv.tv_sec) * 1000.0 + ((double)tv.tv_usec) / 1000.0;
}

int32_t jerry_port_local_tza(double unix_ms)
{
    time_t time = (time_t)(unix_ms / 1000);
    struct tm gmt_tm;
    struct tm local_tm;

    gmtime_r(&time, &gmt_tm);
    localtime_r(&time, &local_tm);

    time_t gmt = mktime(&gmt_tm);

    /* mktime 会剔除夏令时,这里保留 */
    local_tm.tm_isdst = 0;
    time_t local = mktime(&local_tm);

    return (int32_t)difftime(local, gmt) * 1000;
}
