/**
 * @file mem_mgr.h
 * @brief Large-block memory allocation manager (PSRAM on ESP32)
 *
 * Used by the AUTO alloc provider (EOS_MEM_PROVIDER_AUTO) via
 * EOS_MEM_PROVIDER_AUTO_INCLUDE. Large allocations (>= EOS_MEM_POOL_ALLOC_THRESHOLD)
 * are routed here so that big objects go to PSRAM while small, DMA-friendly
 * allocations stay in internal RAM (AGENTS.md section 22).
 */
#ifndef MEM_MGR_H
#define MEM_MGR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate a memory block, preferring PSRAM
 * @param size Requested size in bytes
 * @return Pointer to the allocated block, or NULL on failure
 */
void *mem_mgr_alloc(size_t size);

/**
 * @brief Free a block previously returned by mem_mgr_alloc / mem_mgr_calloc
 * @param ptr Pointer to the block (NULL is allowed)
 */
void mem_mgr_free(void *ptr);

/**
 * @brief Resize a block previously returned by mem_mgr_alloc
 * @param ptr Original pointer (NULL allocates a new block)
 * @param size New size in bytes
 * @return Pointer to the resized block, or NULL on failure
 */
void *mem_mgr_realloc(void *ptr, size_t size);

/**
 * @brief Allocate and zero-initialize a block, preferring PSRAM
 * @param nmemb Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to the zeroed block, or NULL on failure
 */
void *mem_mgr_calloc(size_t nmemb, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MEM_MGR_H */
