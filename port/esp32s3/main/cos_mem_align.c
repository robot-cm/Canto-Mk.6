/**
 * @file eos_mem_align.c
 * @brief 8-byte aligned malloc/free/realloc/calloc via linker --wrap
 *
 * ESP-IDF's default malloc() only guarantees 4-byte alignment, but JerryScript
 * with JERRY_CPOINTER_32_BIT requires JMEM_ALIGNMENT (8) aligned pointers
 * (see jmem_compress_pointer assert "uint_ptr % JMEM_ALIGNMENT == 0").
 * Because JERRY_SYSTEM_ALLOCATOR=1 makes JerryScript call malloc() directly,
 * the whole binary is wrapped here: every malloc/free/realloc/calloc call is
 * redirected to these aligned implementations. A higher alignment is safe for
 * all other consumers (LVGL, lwIP, newlib).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"

#define EOS_MEM_ALIGNMENT 8 /* = JMEM_ALIGNMENT (jmem.h: JMEM_ALIGNMENT_LOG 3) */

void *__wrap_malloc(size_t size)
{
    return heap_caps_aligned_alloc(EOS_MEM_ALIGNMENT, size, MALLOC_CAP_8BIT);
}

void __wrap_free(void *ptr)
{
    heap_caps_free(ptr);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
    if (size != 0 && nmemb > SIZE_MAX / size)
    {
        return NULL;
    }
    size_t total = nmemb * size;
    void *ptr = heap_caps_aligned_alloc(EOS_MEM_ALIGNMENT, total, MALLOC_CAP_8BIT);
    if (ptr != NULL)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}
