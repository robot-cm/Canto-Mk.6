/**
 * @file mem_mgr.c
 * @brief Large-block memory allocation manager (PSRAM on ESP32)
 *
 * Implements the mem_mgr_* API consumed by the AUTO alloc provider
 * (COS_MEM_PROVIDER_AUTO, see cos_config_defaults.h). On ESP32 the
 * allocation is served from PSRAM via heap_caps; if PSRAM is exhausted
 * it degrades to internal RAM instead of failing. On other platforms it
 * simply wraps the C standard library.
 */
#include "mem_mgr.h"

#include <stdlib.h>
#include <string.h>

#if defined(COS_PLATFORM_ESP32)
#include "esp_heap_caps.h"
#endif

void *mem_mgr_alloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }
#if defined(COS_PLATFORM_ESP32)
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (p)
    {
        return p;
    }
    /* PSRAM exhausted: fall back to internal RAM rather than failing. */
    return malloc(size);
#else
    return malloc(size);
#endif
}

void mem_mgr_free(void *ptr)
{
#if defined(COS_PLATFORM_ESP32)
    /* heap_caps_free releases pointers from any esp heap (PSRAM or internal). */
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

void *mem_mgr_realloc(void *ptr, size_t size)
{
    if (!ptr)
    {
        return mem_mgr_alloc(size);
    }
    if (size == 0)
    {
        mem_mgr_free(ptr);
        return NULL;
    }
#if defined(COS_PLATFORM_ESP32)
    /* heap_caps_realloc handles pointers from any esp heap; prefer PSRAM. */
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
#else
    return realloc(ptr, size);
#endif
}

void *mem_mgr_calloc(size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0)
    {
        return NULL;
    }
#if defined(COS_PLATFORM_ESP32)
    void *p = heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM);
    if (p)
    {
        return p;
    }
    /* PSRAM exhausted: fall back to internal RAM rather than failing. */
    return calloc(nmemb, size);
#else
    return calloc(nmemb, size);
#endif
}
