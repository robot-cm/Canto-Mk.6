/**
 * @file cos_mem.h
 * @brief Memory allocation
 */

#ifndef COS_MEM_H
#define COS_MEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Memory allocation function
 * @param size Memory size, unit: bytes
 * @return void* Returns memory address on success, otherwise returns NULL
 */
void *cos_malloc(size_t size);

/**
 * @brief Create a copy of the given string
 * @param s Source string
 * @return char* Returns newly allocated string pointer on success
 *
 * Returns NULL if memory allocation fails
 * @note Use `cos_free(str)` to release the copied string
 */
char *cos_strdup(const char *s);

/**
 * @brief Allocate a block of continuous memory and fill it with zeros
 * @param size Memory size, unit: bytes
 * @return void* Returns memory address on success, otherwise returns NULL
 */
void *cos_malloc_zeroed(size_t size);

/**
 * @brief Free target memory
 * @param ptr Target memory pointer
 */
void cos_free(void *ptr);

/**
 * @brief Reallocate target memory
 * @param ptr Target memory pointer
 * @param new_size New memory size
 * @return void* Returns memory address on success, otherwise returns NULL
 */
void *cos_realloc(void *ptr, size_t new_size);

#ifdef __cplusplus
}
#endif

#endif /* COS_MEM_H */
