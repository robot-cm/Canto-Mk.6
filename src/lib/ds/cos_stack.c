/**
 * @file cos_stack.c
 * @brief Stack
 */

#include "cos_stack.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "Stack"
#include "cos_log.h"
#include "cos_mem.h"

/* Macros and Definitions -------------------------------------*/
#define _SHRINK_ENABLE 1
#define _SHRINK_THRESHOLD (4)
#define _SHRINK_PROPORTION (2)
#define _CAPACITY_GROWTH (2)

struct cos_stack_t
{
    void **buffer;
    size_t size;
    size_t capacity;
    size_t min_capacity;
    cos_stack_capacity_mode_t mode;
};

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

static bool _stack_expand(cos_stack_t *stack)
{
    size_t new_capacity = stack->capacity * _CAPACITY_GROWTH;
    void **new_buffer = cos_malloc_zeroed(new_capacity * sizeof(void *));
    COS_CHECK_PTR_RETURN_VAL(new_buffer, false);

    for (size_t i = 0; i < stack->size; i++)
    {
        new_buffer[i] = stack->buffer[i];
    }

    cos_free(stack->buffer);
    stack->buffer = new_buffer;
    COS_LOG_I("stack expanded: %zu -> %zu", stack->capacity, new_capacity);
    stack->capacity = new_capacity;
    return true;
}

static bool _stack_shrink(cos_stack_t *stack)
{
    COS_CHECK_PTR_RETURN_VAL(stack, false);

    if (stack->capacity <= stack->min_capacity)
        return true;
    if (stack->size > stack->capacity / _SHRINK_THRESHOLD)
        return true;

    size_t new_capacity = stack->capacity / _SHRINK_PROPORTION;
    if (new_capacity < stack->min_capacity)
        new_capacity = stack->min_capacity;

    void **new_buffer = cos_malloc_zeroed(new_capacity * sizeof(void *));
    COS_CHECK_PTR_RETURN_VAL(new_buffer, false);

    for (size_t i = 0; i < stack->size; i++)
    {
        new_buffer[i] = stack->buffer[i];
    }

    cos_free(stack->buffer);
    stack->buffer = new_buffer;
    COS_LOG_I("stack shrinked: %zu -> %zu", stack->capacity, new_capacity);
    stack->capacity = new_capacity;
    return true;
}

cos_stack_t *cos_stack_create(size_t init_capacity)
{
    return cos_stack_create_with_mode(init_capacity, COS_STACK_CAPACITY_DYNAMIC);
}

cos_stack_t *cos_stack_create_with_mode(size_t init_capacity, cos_stack_capacity_mode_t mode)
{
    if (init_capacity == 0)
        init_capacity = 4;

    cos_stack_t *stack = cos_malloc_zeroed(sizeof(cos_stack_t));
    COS_CHECK_PTR_RETURN_VAL(stack, NULL);

    stack->buffer = cos_malloc_zeroed(init_capacity * sizeof(void *));
    COS_CHECK_PTR_RETURN_VAL_FREE(stack->buffer, NULL, stack);

    stack->size = 0;
    stack->capacity = init_capacity;
    stack->min_capacity = init_capacity;
    stack->mode = mode;
    COS_LOG_I("stack created, mode=%s", mode == COS_STACK_CAPACITY_DYNAMIC ? "dynamic" : "fixed");
    return stack;
}

bool cos_stack_push(cos_stack_t *stack, void *data)
{
    COS_CHECK_PTR_RETURN_VAL(stack, false);

    if (stack->size == stack->capacity)
    {
        if (stack->mode == COS_STACK_CAPACITY_FIXED)
        {
            COS_LOG_W("Push failed: fixed-capacity stack is full");
            return false;
        }

        if (!_stack_expand(stack))
            return false;
    }

    stack->buffer[stack->size++] = data;
    COS_LOG_I("Push data[%p]", data);
    return true;
}

void *cos_stack_pop(cos_stack_t *stack)
{
    COS_CHECK_PTR_RETURN_VAL(stack, NULL);
    if (stack->size == 0)
        return NULL;

    void *data = stack->buffer[stack->size - 1];
    stack->buffer[stack->size - 1] = NULL;
    stack->size--;

#if _SHRINK_ENABLE
    if (stack->mode == COS_STACK_CAPACITY_DYNAMIC)
        _stack_shrink(stack);
#endif

    COS_LOG_I("Pop data[%p]", data);
    return data;
}

void *cos_stack_peek(cos_stack_t *stack)
{
    COS_CHECK_PTR_RETURN_VAL(stack, NULL);
    if (stack->size == 0)
        return NULL;

    return stack->buffer[stack->size - 1];
}

size_t cos_stack_get_size(cos_stack_t *stack)
{
    COS_CHECK_PTR_RETURN_VAL(stack, 0);
    return stack->size;
}

cos_stack_capacity_mode_t cos_stack_get_capacity_mode(cos_stack_t *stack)
{
    COS_CHECK_PTR_RETURN_VAL(stack, COS_STACK_CAPACITY_FIXED);
    return stack->mode;
}

void cos_stack_destroy(cos_stack_t *stack)
{
    COS_CHECK_PTR_RETURN(stack);
    cos_free(stack->buffer);
    cos_free(stack);
}

void *cos_stack_get_at(cos_stack_t *stack, size_t index)
{
    COS_CHECK_PTR_RETURN_VAL(stack, NULL);
    if (index >= stack->size)
    {
        return NULL;
    }
    return stack->buffer[index];
}
