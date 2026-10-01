/**
 * @file esp_heap_caps.c
 * @brief Host implementation of the esp_heap_caps shim (see esp_heap_caps.h).
 *
 * Allocation strategy: straight pass-through to the C allocator. The MSVCRT
 * malloc on x64 guarantees 16-byte alignment (max_align_t), which covers the
 * only aligned_alloc call site in the project (jerry_port.c uses
 * JMEM_ALIGNMENT = 8). Pass-through also keeps every returned pointer
 * compatible with free()/realloc()/cos_free() on any code path — no tracked
 * block headers, no free-mismatch hazards.
 *
 * Statistics: the host has one heap, so the per-capability queries report a
 * plausible static model that mirrors the ESP32-S3 layout
 * (internal SRAM ≈ 512 KB / PSRAM ≈ 8 MB / DMA ≈ 256 KB). These numbers are
 * diagnostics only (shell `mem`, boot logs, profmonitor); no allocation
 * policy branches on them.
 */

#include "esp_heap_caps.h"

#include <stdio.h>
#include <stdlib.h>

/* ── Allocation API ─────────────────────────────────────────────────── */

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    return calloc(n, size);
}

void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps)
{
    (void)caps;
    return realloc(ptr, size);
}

void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
    (void)caps;
    /* malloc already guarantees _Alignof(max_align_t) (= 16 on x64 MSVCRT).
     * The project's only call site passes JMEM_ALIGNMENT (= 8), so a plain
     * malloc satisfies the alignment contract. Warn loudly if some future
     * call site ever asks for more. */
    if (alignment > _Alignof(max_align_t)) {
        fprintf(stderr,
                "[esp_shim] heap_caps_aligned_alloc: alignment %zu > %zu not "
                "supported on host, serving default-aligned memory\n",
                (size_t)alignment, (size_t)_Alignof(max_align_t));
    }
    return malloc(size);
}

void heap_caps_free(void *ptr)
{
    free(ptr);
}

/* ── Static heap model (diagnostics only) ───────────────────────────── */

#define SIM_INTERNAL_TOTAL    (512u * 1024u)
#define SIM_INTERNAL_FREE     (384u * 1024u)   /* ~25% in use at boot      */
#define SIM_INTERNAL_LARGEST  (320u * 1024u)
#define SIM_INTERNAL_MIN      (352u * 1024u)
#define SIM_PSRAM_TOTAL       (8192u * 1024u)
#define SIM_PSRAM_FREE        (6656u * 1024u)  /* ~1.5 MB "in use"         */
#define SIM_PSRAM_LARGEST     (6144u * 1024u)
#define SIM_PSRAM_MIN         (5504u * 1024u)
#define SIM_DMA_TOTAL         (256u * 1024u)
#define SIM_DMA_FREE          (224u * 1024u)
#define SIM_DMA_LARGEST       (192u * 1024u)
#define SIM_DMA_MIN           (208u * 1024u)

typedef struct
{
    size_t total;
    size_t free;
    size_t largest;
    size_t min;
} sim_pool_t;

/* Map a capability mask onto the simulated pool it refers to. SPIRAM wins
 * over everything; a pure DMA request gets the DMA pool; anything else
 * (INTERNAL|8BIT / DEFAULT / …) lands in the internal pool. */
static const sim_pool_t *sim_pool_for_caps(uint32_t caps)
{
    static const sim_pool_t internal = {
        SIM_INTERNAL_TOTAL, SIM_INTERNAL_FREE, SIM_INTERNAL_LARGEST, SIM_INTERNAL_MIN
    };
    static const sim_pool_t psram = {
        SIM_PSRAM_TOTAL, SIM_PSRAM_FREE, SIM_PSRAM_LARGEST, SIM_PSRAM_MIN
    };
    static const sim_pool_t dma = {
        SIM_DMA_TOTAL, SIM_DMA_FREE, SIM_DMA_LARGEST, SIM_DMA_MIN
    };

    if (caps & MALLOC_CAP_SPIRAM)
        return &psram;
    if (caps == MALLOC_CAP_DMA)
        return &dma;
    return &internal;
}

size_t heap_caps_get_free_size(uint32_t caps)
{
    return sim_pool_for_caps(caps)->free;
}

size_t heap_caps_get_total_size(uint32_t caps)
{
    return sim_pool_for_caps(caps)->total;
}

size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    return sim_pool_for_caps(caps)->largest;
}

size_t heap_caps_get_minimum_free_size(uint32_t caps)
{
    return sim_pool_for_caps(caps)->min;
}

void heap_caps_get_info(multi_heap_info_t *info, uint32_t caps)
{
    if (info == NULL)
        return;
    const sim_pool_t *p = sim_pool_for_caps(caps);
    info->total_free_bytes      = p->free;
    info->total_allocated_bytes = p->total - p->free;
    info->largest_free_block    = p->largest;
    info->minimum_free_bytes    = p->min;
    /* Block counts are cosmetic on the host; keep them plausible. */
    info->allocated_blocks      = 64;
    info->free_blocks           = 8;
    info->total_blocks          = info->allocated_blocks + info->free_blocks;
}
