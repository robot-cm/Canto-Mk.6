/**
 * @file cos_mem_port.c
 * @brief Memory allocation
 */

#include "cos_config.h"

#if (COS_MEM_ALLOC_PROVIDER == COS_MEM_PROVIDER_STDLIB_CLIB) || (COS_MEM_ALLOC_PROVIDER == COS_MEM_PROVIDER_CUSTOM)

#include "cos_mem_port.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_port.h"
#include "cos_log.h"
#include "cos_config.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

COS_WEAK void *cos_malloc_core(size_t size)
{
    return malloc(size);
}

COS_WEAK void *cos_malloc_zeroed_core(size_t size)
{
    return calloc(1, size);
}

COS_WEAK void cos_free_core(void *ptr)
{
    free(ptr);
}

COS_WEAK void *cos_realloc_core(void *ptr, size_t new_size)
{
    return realloc(ptr, new_size);
}

#endif /* COS_MEM_ALLOC_PROVIDER */
