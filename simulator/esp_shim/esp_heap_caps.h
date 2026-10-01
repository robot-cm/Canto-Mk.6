/**
 * @file esp_heap_caps.h
 * @brief ESP-IDF heap_caps shim for the Windows/MinGW simulator.
 *
 * Three files in src/ include "esp_heap_caps.h" WITHOUT any platform guard:
 *   - src/kernel/core/cos_core.c               (heap_caps_get_free_size /
 *                                               heap_caps_get_largest_free_block)
 *   - src/services/config/cos_service_config.c (same two functions)
 *   - src/script_engine/jerry_port.c           (heap_caps_aligned_alloc /
 *                                               heap_caps_free /
 *                                               heap_caps_get_info,
 *                                               plus multi_heap_info_t)
 *
 * On the real ESP32-S3 these map to the internal SRAM / PSRAM heaps. On the
 * host PC there is a single heap, so every capability is served from the C
 * allocator. JerryScript requires the external context buffer to be aligned
 * to JMEM_ALIGNMENT (8), hence aligned_alloc.
 *
 * Statistics: the host has no per-capability heaps, so the *_size/*_info
 * queries report a plausible static model (64 MB "PSRAM" + a few MB
 * "internal") so boot/diagnostic logs stay readable. This is diagnostics
 * only — no allocation policy depends on these numbers.
 *
 * NOTE: mem_mgr.c / cos_shell.c / profmonitor / spotify / usb_msc guard
 * their ESP includes with COS_PLATFORM_ESP32 / CONFIG_* macros and never
 * see this file. This shim is injected only via the simulator CMake
 * include path (simulator/esp_shim/ placed FIRST in the include order).
 */

#ifndef ESP_HEAP_CAPS_SHIM_H
#define ESP_HEAP_CAPS_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Capability bit masks (values identical to real esp_heap_caps.h) ── */
#define MALLOC_CAP_DMA          (1 << 0)  /* internal, DMA-capable          */
#define MALLOC_CAP_8BIT         (1 << 1)  /* 1-byte access                  */
#define MALLOC_CAP_32BIT        (1 << 2)  /* 4-byte access                  */
#define MALLOC_CAP_DEFAULT      (1 << 7)  /* malloc() default heap          */
#define MALLOC_CAP_INTERNAL     (1 << 8)  /* internal SRAM                  */
#define MALLOC_CAP_SPIRAM       (1 << 10) /* external PSRAM                 */
#define MALLOC_CAP_EXEC         (1 << 11) /* executable                     */

/* ── multi_heap_info_t (subset of fields used by src/) ───────────────
 * jerry_port.c reads total_free_bytes / largest_free_block only, but the
 * full layout mirrors the IDF struct for future-proofing. */
typedef struct
{
    size_t total_free_bytes;      /* Total free bytes in the heap        */
    size_t total_allocated_bytes; /* Total bytes allocated               */
    size_t largest_free_block;    /* Largest free block                  */
    size_t minimum_free_bytes;    /* Historical minimum free bytes       */
    size_t allocated_blocks;      /* Number of allocated blocks          */
    size_t free_blocks;           /* Number of free blocks               */
    size_t total_blocks;          /* Total blocks                        */
} multi_heap_info_t;

/* ── Allocation API (subset used by src/) ──────────────────────────── */
void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void  heap_caps_free(void *ptr);

/* ── Statistics API (subset used by src/) ──────────────────────────── */
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
void   heap_caps_get_info(multi_heap_info_t *info, uint32_t caps);

#ifdef __cplusplus
}
#endif

#endif /* ESP_HEAP_CAPS_SHIM_H */
