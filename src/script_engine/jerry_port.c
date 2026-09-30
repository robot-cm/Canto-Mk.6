/**
 * @file jerry_port.c
 * @brief JerryScript Port for ElenixOS
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "jerryscript-port.h"
#include "script_engine_core.h"

/* ── JerryScript Context 堆:512KB 整体放 PSRAM ─────────────
 * JERRY_SYSTEM_ALLOCATOR=0(内部堆模式)时,JS 引擎的所有对象分配
 * 都来自 jerry_port_context_alloc 一次性分配的大块。
 * 用 heap_caps_malloc(MALLOC_CAP_SPIRAM) 把整个 JS 堆放到 PSRAM,
 * 避免 SYSTEM_ALLOCATOR=1 时小对象 malloc 碎片化内部 DMA RAM
 * (内部 DMA largest 30KB→32B,导致 SD/FATFS 写路径分配失败的根因)。
 * 覆盖 jerry-port-context.c 中的 weak 版本。
 *
 * 256KB→512KB:Calendar 等大 App(阴历表+42天网格)启动时 JS 堆
 * 在 256KB 下 OOM(code=10),且在 timer/event 回调阶段无 setjmp
 * 恢复点,fatal 直接 abort 导致整机重启。512KB 在 8MB PSRAM 上
 * 占用可忽略,同时配合 script_engine_call_raw 的回调级 fatal 恢复。 */
#ifndef JERRY_GLOBAL_HEAP_SIZE
#define JERRY_GLOBAL_HEAP_SIZE 512
#endif

static void *s_jerry_context_p = NULL;

size_t jerry_port_context_alloc(size_t context_size)
{
    size_t total_size = context_size + JERRY_GLOBAL_HEAP_SIZE * 1024;

    /* JerryScript 要求堆基址按 JMEM_ALIGNMENT(8) 对齐(jmem_heap_init 断言),
     * heap_caps_malloc 只保证 4 字节对齐,必须用 aligned_alloc。
     * 260KB+ 大块整体落 PSRAM,保护内部 DMA RAM 不被切割。 */
#ifndef JMEM_ALIGNMENT
#define JMEM_ALIGNMENT 8
#endif
    s_jerry_context_p = heap_caps_aligned_alloc(JMEM_ALIGNMENT, total_size,
                                                MALLOC_CAP_SPIRAM);
    if (s_jerry_context_p == NULL)
    {
        /* PSRAM 不足时的兜底:退回常规 8BIT 堆(内部 RAM + PSRAM) */
        s_jerry_context_p = heap_caps_aligned_alloc(JMEM_ALIGNMENT, total_size,
                                                    MALLOC_CAP_8BIT);
    }
    if (s_jerry_context_p == NULL)
    {
        printf("[JerryScript] context alloc failed: %u bytes\r\n",
               (unsigned)total_size);
        abort();
    }
    return total_size;
}

jerry_context_t *jerry_port_context_get(void)
{
    return (jerry_context_t *)s_jerry_context_p;
}

void jerry_port_context_free(void)
{
    if (s_jerry_context_p != NULL)
    {
        heap_caps_free(s_jerry_context_p);
        s_jerry_context_p = NULL;
    }
}

void JERRY_ATTR_NORETURN jerry_port_fatal(jerry_fatal_code_t code)
{
    printf("[JerryScript] Fatal error: code=%d\r\n", (int)code);

    /* ── 崩溃现场内存报告(无论 code 都打印,便于定位瓶颈) ── */
    static int s_fatal_count = 0;
    s_fatal_count++;
    multi_heap_info_t dram_info, psram_info;
    heap_caps_get_info(&dram_info, MALLOC_CAP_INTERNAL);
    heap_caps_get_info(&psram_info, MALLOC_CAP_SPIRAM);
    printf("[JerryScript] fatal#%d heap: DRAM free=%u largest=%u | PSRAM free=%u largest=%u\r\n",
           s_fatal_count,
           (unsigned)dram_info.total_free_bytes, (unsigned)dram_info.largest_free_block,
           (unsigned)psram_info.total_free_bytes, (unsigned)psram_info.largest_free_block);

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
