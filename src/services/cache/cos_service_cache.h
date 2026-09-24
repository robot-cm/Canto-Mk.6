/**
 * @file cos_service_cache.h
 * @brief Image cache service
 *
 * Cache for decoded LVGL image pixel data.  Initialised automatically
 * during system startup (see cos_core.c).
 *
 * On platforms with a dedicated memory pool (e.g. PSRAM on SiFli),
 * set COS_CACHE_USE_DEDICATED_MEM=1 in cos_config.h and override
 * the weak cos_cache_buf_alloc / cos_cache_buf_free in the port layer.
 */
#ifndef COS_SERVICE_CACHE_H
#define COS_SERVICE_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "cos_config.h"
#include <stddef.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialise LVGL image decode cache with sizes from
 *        cos_config.h (COS_CACHE_SIZE / COS_CACHE_HEADER_COUNT).
 *
 * Safe to call multiple times — no-op after the first successful
 * init or if LVGL cache was already configured by the user.
 */
void cos_service_cache_init(void);

/**
 * @brief Allocate a cache pixel buffer from dedicated memory pool.
 *
 * Weak default falls back to lv_malloc.
 * Override in a platform port (e.g. SiFli PSRAM) to allocate from
 * a specialised memory heap.
 *
 * @param size  Allocation size in bytes.
 * @return      Pointer to allocated memory, or NULL.
 */
void *cos_cache_buf_alloc(size_t size);

/**
 * @brief Free a cache pixel buffer previously allocated by
 *        cos_cache_buf_alloc.
 * @param ptr  Pointer to free.
 */
void cos_cache_buf_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_CACHE_H */
