/**
 * @file cos_cqueue.c
 * @brief Resizable circular queue FIFO
 */

#include "cos_cqueue.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "CircularQueue"
#include "cos_log.h"
#include "cos_mem.h"
/* Macros and Definitions -------------------------------------*/
#define _SHRINK_ENABLE 1
#define _SHRINK_THRESHOLD (4) /**< Automatically shrink when below `_SHRINK_THRESHOLD` of capacity */
#define _SHRINK_PROPORTION (2) /**< Shrink proportion, `shrinked size = capacity / _SHRINK_PROPORTION` */
#define _CAPACITY_GROWTH 2 /**< Capacity growth factor */

struct cos_cqueue_t
{
    size_t head;
    size_t tail;
    size_t capacity;
    size_t size;
    size_t min_capacity;
    void **buffer;
};
/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

cos_cqueue_t *cos_cqueue_create(size_t init_capacity)
{
    if (init_capacity == 0)
        init_capacity = 4;

    cos_cqueue_t *cq = cos_malloc_zeroed(sizeof(cos_cqueue_t));
    COS_CHECK_PTR_RETURN_VAL(cq, NULL);

    cq->buffer = cos_malloc_zeroed(init_capacity * sizeof(void *));
    COS_CHECK_PTR_RETURN_VAL_FREE(cq->buffer, NULL, cq);

    cq->head = 0;
    cq->tail = 0;
    cq->size = 0;
    cq->capacity = init_capacity;
    cq->min_capacity = init_capacity;
    COS_LOG_I("cq created");
    return cq;
}

static bool _cqueue_expand(cos_cqueue_t *cq)
{
    size_t new_capacity = cq->capacity * _CAPACITY_GROWTH;
    void **new_buffer = cos_malloc_zeroed(new_capacity * sizeof(void *));
    COS_CHECK_PTR_RETURN_VAL(new_buffer, false);

    for (int i = 0; i < cq->size; i++)
    {
        new_buffer[i] = cq->buffer[(cq->head + i) % cq->capacity];
    }

    cos_free(cq->buffer);
    cq->buffer = new_buffer;
    COS_LOG_I("cq expanded: %d -> %d", cq->capacity, new_capacity);
    cq->capacity = new_capacity;
    cq->head = 0;
    cq->tail = cq->size;
    return true;
}

static bool _cqueue_shrink(cos_cqueue_t *cq)
{
    COS_CHECK_PTR_RETURN_VAL(cq, false);
    // No expansion, no need to shrink
    if (cq->capacity <= cq->min_capacity)
        return true;
    if (cq->size > cq->capacity / _SHRINK_THRESHOLD)
        return true;
    size_t new_capacity = cq->capacity / _SHRINK_PROPORTION;

    void **new_buf = cos_malloc_zeroed(sizeof(void *) * new_capacity);
    COS_CHECK_PTR_RETURN_VAL(new_buf, false);

    for (size_t i = 0; i < cq->size; i++)
    {
        new_buf[i] = cq->buffer[(cq->head + i) % cq->capacity];
    }

    cos_free(cq->buffer);

    cq->buffer = new_buf;
    COS_LOG_I("cq shrinked: %d -> %d", cq->capacity, new_capacity);
    cq->capacity = new_capacity;
    cq->head = 0;
    cq->tail = cq->size;

    return true;
}

bool cos_cqueue_enqueue(cos_cqueue_t *cq, void *data)
{
    COS_CHECK_PTR_RETURN_VAL(cq, false);
    if (cq->size == cq->capacity)
    {
        if (!_cqueue_expand(cq))
            return false;
    }

    cq->buffer[cq->tail] = data;
    cq->tail = (cq->tail + 1) % cq->capacity;
    cq->size++;
    return true;
}

void *cos_cqueue_dequeue(cos_cqueue_t *cq)
{
    COS_CHECK_PTR_RETURN_VAL(cq, NULL);
    if (cq->size == 0)
        return NULL;
    void *tmp_data = cq->buffer[cq->head];
    cq->head = (cq->head + 1) % cq->capacity;
    cq->size--;
#if _SHRINK_ENABLE
    _cqueue_shrink(cq);
#endif /* _SHRINK_ENABLE */
    return tmp_data;
}

void cos_cqueue_destroy(cos_cqueue_t *cq)
{
    COS_CHECK_PTR_RETURN(cq);
    cos_free(cq->buffer);
    cos_free(cq);
}

size_t cos_cqueue_get_size(cos_cqueue_t *cq)
{
    COS_CHECK_PTR_RETURN_VAL(cq, 0);
    return cq->size;
}

void *cos_cqueue_peek(cos_cqueue_t *cq, size_t index)
{
    COS_CHECK_PTR_RETURN_VAL(cq, NULL);

    if (index >= cq->size)
        return NULL; // Out of bounds

    // Calculate actual position in buffer
    size_t real_pos = (cq->head + index) % cq->capacity;
    return cq->buffer[real_pos];
}
