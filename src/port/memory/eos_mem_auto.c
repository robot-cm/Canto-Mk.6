/**
 * @file eos_mem_auto.c
 * @brief Auto memory allocation
 */

#include "eos_config.h"

#if EOS_MEM_ALLOC_PROVIDER == EOS_MEM_PROVIDER_AUTO

#include "eos_mem_port.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eos_port.h"
#define EOS_LOG_TAG "MemAuto"
#include "eos_log.h"
#include "eos_config.h"
#include EOS_MEM_PROVIDER_AUTO_INCLUDE
#if EOS_PLATFORM_ESP32
#include "esp_debug_helpers.h"
#endif
/* Macros and Definitions -------------------------------------*/

typedef enum
{
    EOS_MEM_POOL_FAST = 0,
    EOS_MEM_POOL_LARGE = 1
} eos_mem_type_t;

/* 魔数:分配时写入 header,释放/realloc 时校验。外部分配器(heap_caps/cJSON/
   jerry/LVGL 等)的指针前 8 字节内容恰好等于该值概率 ~1/65536,可近乎
   100% 识别外来指针,避免对错位指针执行 free 而破坏系统堆(tlsf)。 */
#define EOS_MEM_HEADER_MAGIC 0xE5A0u

typedef struct
{
    uint16_t magic; /**< 魔数(识别外来指针) */
    uint8_t type;   /**< Memory type */
    uint8_t pad;    /**< Memory alignment (总大小保持 8 字节 → 用户指针 8 对齐) */
    size_t size;    /**< User-requested size (header excluded), used by realloc fallback copy */
} eos_mem_header_t;

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

/* Pool choice for a requested size (below threshold -> internal DRAM FAST,
   >= threshold -> PSRAM LARGE). */
static eos_mem_type_t eos_mem_pool_for(size_t size)
{
    return (size < EOS_MEM_POOL_ALLOC_THRESHOLD) ? EOS_MEM_POOL_FAST : EOS_MEM_POOL_LARGE;
}

void *eos_malloc_core(size_t size)
{
    size_t total = size + sizeof(eos_mem_header_t);
    eos_mem_header_t *hdr;
    eos_mem_type_t want = eos_mem_pool_for(size);
    eos_mem_type_t got = want;

    hdr = (want == EOS_MEM_POOL_FAST) ? EOS_MEM_ALLOC_FAST(total)
                                      : EOS_MEM_ALLOC_LARGE(total);
    if (!hdr && want == EOS_MEM_POOL_FAST)
    {
        /* Internal DRAM exhausted (steady state free is only ~12KB on device).
           Falling back to the PSRAM pool keeps the allocation alive instead of
           returning NULL - an unchecked NULL here propagates into LVGL/SNI and
           corrupts memory, which shows up as JerryScript assert fatal code=120. */
        hdr = EOS_MEM_ALLOC_LARGE(total);
        if (hdr)
        {
            got = EOS_MEM_POOL_LARGE;
            EOS_LOG_W("DRAM tight: %zuB fell back to PSRAM pool", total);
        }
    }

    if (!hdr)
        return NULL;

    hdr->magic = EOS_MEM_HEADER_MAGIC;
    hdr->type = got;
    hdr->size = size;

    EOS_LOG_D("Auto memory alloc: [%s]%d",
              hdr->type == EOS_MEM_POOL_FAST ? "EOS_MEM_POOL_FAST" : "EOS_MEM_POOL_LARGE",
              total);

    return (void *)(hdr + 1);
}

void *eos_malloc_zeroed_core(size_t size)
{
    size_t total = size + sizeof(eos_mem_header_t);
    eos_mem_header_t *hdr;
    eos_mem_type_t want = eos_mem_pool_for(size);
    eos_mem_type_t got = want;

    hdr = (want == EOS_MEM_POOL_FAST) ? EOS_MEM_CALLOC_FAST(1, total)
                                      : EOS_MEM_CALLOC_LARGE(1, total);
    if (!hdr && want == EOS_MEM_POOL_FAST)
    {
        hdr = EOS_MEM_CALLOC_LARGE(1, total);
        if (hdr)
        {
            got = EOS_MEM_POOL_LARGE;
            memset(hdr, 0, total);
            EOS_LOG_W("DRAM tight: %zuB calloc fell back to PSRAM pool", total);
        }
    }

    if (!hdr)
        return NULL;

    hdr->magic = EOS_MEM_HEADER_MAGIC;
    hdr->type = got;
    hdr->size = size;

    return (void *)(hdr + 1);
}

void eos_free_core(void *ptr)
{
    EOS_CHECK_PTR_RETURN(ptr);

    eos_mem_header_t *hdr = (eos_mem_header_t *)ptr - 1;

    /* 外来指针防护:非本池魔数一律不碰(泄漏+日志),绝不 free 错位地址 */
    if (hdr->magic != EOS_MEM_HEADER_MAGIC)
    {
        EOS_LOG_E("Free: foreign pointer %p magic=0x%04x - ignored(leak) caller=%p",
                  (void *)ptr, (unsigned)hdr->magic, __builtin_return_address(0));
#if EOS_PLATFORM_ESP32
        EOS_LOG_E("Foreign pointer backtrace (PC stack):");
        esp_backtrace_print(6);
#else
        for (int i = 1; i <= 5; i++)
        {
            void *ra = __builtin_return_address(i);
            if (!ra)
                break;
            EOS_LOG_E("  caller[%d]=%p", i, ra);
        }
#endif
        return;
    }

    switch (hdr->type)
    {
        case EOS_MEM_POOL_FAST:
            EOS_MEM_FREE_FAST(hdr);
            break;
        case EOS_MEM_POOL_LARGE:
            EOS_MEM_FREE_LARGE(hdr);
            break;
        default:
            EOS_LOG_E("Unknown memory type hdr->type=%d ptr=%p caller=%p",
                      (int)hdr->type, (void *)ptr, __builtin_return_address(0));
            break;
    }
}

void *eos_realloc_core(void *ptr, size_t new_size)
{
    if (!ptr)
        return eos_malloc_core(new_size); /* realloc(NULL,n) 合法 */

    // Get original memory block info
    eos_mem_header_t *old_hdr = (eos_mem_header_t *)ptr - 1;

    /* Foreign-pointer guard: only blocks allocated by eos_mem_auto may
       be realloc'd here.  A pointer from another allocator (heap_caps
       via --wrap malloc, jerry context heap, cJSON, LVGL stdlib ...)
       has no eos magic in front - trust the magic over the type byte,
       which an external block header can coincidentally equal (0/1).
       Allocate fresh instead - leaks the old block but keeps both
       pools intact - and report the caller. */
    if (old_hdr->magic != EOS_MEM_HEADER_MAGIC ||
        (old_hdr->type != EOS_MEM_POOL_FAST && old_hdr->type != EOS_MEM_POOL_LARGE))
    {
        EOS_LOG_E("Realloc: foreign pointer %p magic=0x%04x type=%d caller=%p - alloc new, leak old",
                  (void *)ptr, (unsigned)old_hdr->magic, (int)old_hdr->type,
                  __builtin_return_address(0));
        return eos_malloc_core(new_size);
    }

    eos_mem_type_t original_type = old_hdr->type; // Keep original memory type
    size_t old_size = old_hdr->size;

    EOS_LOG_D("Realloc:  %zu, keep pool: %s",
              new_size,
              original_type == EOS_MEM_POOL_FAST ? "EOS_MEM_POOL_FAST" : "EOS_MEM_POOL_LARGE");

    // Always realloc in the original memory pool
    size_t total = new_size + sizeof(eos_mem_header_t);
    eos_mem_header_t *new_hdr = (original_type == EOS_MEM_POOL_FAST)
                                    ? EOS_MEM_REALLOC_FAST(old_hdr, total)
                                    : EOS_MEM_REALLOC_LARGE(old_hdr, total);

    if (!new_hdr)
    {
        /* In-place realloc failed (internal DRAM fragmented/exhausted).
           Fall back to alloc+copy+free, trying the same pool first and the
           PSRAM pool when DRAM is out - never return NULL silently, since an
           unchecked NULL corrupts memory upstream (jerry assert fatal 120). */
        new_hdr = EOS_MEM_ALLOC_FAST(total);
        eos_mem_type_t new_type = EOS_MEM_POOL_FAST;
        if (!new_hdr)
        {
            new_hdr = EOS_MEM_ALLOC_LARGE(total);
            new_type = EOS_MEM_POOL_LARGE;
        }
        if (new_hdr)
        {
            size_t copy = (old_size < new_size) ? old_size : new_size;
            if (copy)
                memcpy((void *)(new_hdr + 1), ptr, copy);
            if (original_type == EOS_MEM_POOL_FAST)
                EOS_MEM_FREE_FAST(old_hdr);
            else
                EOS_MEM_FREE_LARGE(old_hdr);
            EOS_LOG_W("Realloc %zu->%zu: in-place failed, moved to %s pool",
                      old_size, new_size,
                      new_type == EOS_MEM_POOL_FAST ? "FAST" : "PSRAM(LARGE)");
            new_hdr->magic = EOS_MEM_HEADER_MAGIC;
            new_hdr->type = new_type;
            new_hdr->size = new_size;
            return (void *)(new_hdr + 1);
        }
        EOS_LOG_E("Realloc failed for pool: %s",
                  original_type == EOS_MEM_POOL_FAST ? "EOS_MEM_POOL_FAST" : "EOS_MEM_POOL_LARGE");
        return NULL;
    }

    // Keep the original memory type
    new_hdr->magic = EOS_MEM_HEADER_MAGIC;
    new_hdr->type = original_type;
    new_hdr->size = new_size;

    EOS_LOG_D("Realloc success in original pool");
    return (void *)(new_hdr + 1);
}

#endif /* EOS_MEM_ALLOC_PROVIDER */
