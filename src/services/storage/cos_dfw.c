/**
 * @file cos_dfw.c
 * @brief Deferred File Writer
 */

#include "cos_config.h"

#if COS_DFW_ENABLE

#include "cos_dfw.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COS_LOG_TAG "DFW"
#include "cos_log.h"
#include "cos_cqueue.h"
#include "cos_service_storage.h"
#include "cos_port.h"
#include "cos_mem.h"
/* Macros and Definitions -------------------------------------*/
typedef struct
{
    char path[COS_FS_PATH_MAX];
    uint8_t *data;
    size_t data_size;
} cos_dfw_cache_t;
/* Variables --------------------------------------------------*/
static cos_cqueue_t *cq = NULL;
/* Function Implementations -----------------------------------*/

static cos_dfw_cache_t *_find_cache(const char *path)
{
    for (int i = 0; i < cos_cqueue_get_size(cq); i++)
    {
        cos_dfw_cache_t *cache = cos_cqueue_peek(cq, i);
        if (strcmp(cache->path, path) == 0)
        {
            return cache;
        }
    }
    return NULL;
}

bool cos_dfw_write(const char *path, const uint8_t *data, size_t data_size)
{
    COS_CHECK_PTR_RETURN_VAL(path && data, false);
    if (data_size == 0)
    {
        COS_LOG_E("data_size == 0");
        return false;
    }
    cos_dfw_cache_t *cache = _find_cache(path);
    if (cache)
    {
        COS_LOG_I("Cache found");
        if (cache->data)
            cos_free(cache->data);
        cache->data = cos_malloc(data_size + 1);
        memcpy(cache->data, data, data_size);
        cache->data[data_size] = '\0';
        cache->data_size = data_size;
        return true;
    }
    else
    {
        COS_LOG_I("Cache not found");
        cache = cos_malloc_zeroed(sizeof(cos_dfw_cache_t));
        COS_CHECK_PTR_RETURN_VAL(cache, false);
        strncpy(cache->path, path, COS_FS_PATH_MAX - 1);
        cache->path[COS_FS_PATH_MAX - 1] = '\0';
        cache->data = cos_malloc(data_size + 1);
        memcpy(cache->data, data, data_size);
        cache->data[data_size] = '\0';
        cache->data_size = data_size;
        if (cos_cqueue_enqueue(cq, cache))
        {
            COS_LOG_I("Cache enqueued");
        }
        else
        {
            COS_LOG_E("Failed to enqueue cache");
        }
        return true;
    }
}

uint8_t *cos_dfw_read(const char *path)
{
    COS_CHECK_PTR_RETURN_VAL(path, NULL);
    cos_dfw_cache_t *cache = _find_cache(path);
    if (cache)
    {
        COS_LOG_I("Cache found");
        uint8_t *copy = cos_malloc_zeroed(cache->data_size);
        memcpy(copy, cache->data, cache->data_size);
        return copy;
    }
    else
    {
        return (uint8_t *)cos_storage_read_file_immediate(path);
    }
}

void cos_dfw_sync(void)
{
    while (cos_cqueue_get_size(cq) > 0)
    {
        cos_dfw_cache_t *cache = cos_cqueue_dequeue(cq);
        COS_CHECK_PTR_RETURN(cache);
        if (cos_storage_write_file_immediate(cache->path, cache->data, cache->data_size) != COS_OK)
        {
            COS_LOG_E("Write failed");
        }
        else
        {
            COS_LOG_I("Write done");
        }
        cos_free(cache->data);
        cos_free(cache);
    }
}

void cos_dfw_init(void)
{
    cq = cos_cqueue_create(4);
}

#endif /* COS_DFW_ENABLE */
