/**
 * @file cos_dispatcher.c
 * @brief Task dispatcher
 */

#include "cos_dispatcher.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "cos_port.h"
#include "cos_config.h"
#include "cos_mem.h"
/* Macros and Definitions -------------------------------------*/
typedef struct
{
    cos_dispatcher_cb_t cb;
    void *user_data;
} cos_dispatcher_item_t;
/* Variables --------------------------------------------------*/
static cos_dispatcher_item_t *s_queue = NULL;
static volatile int s_head = 0;
static volatile int s_tail = 0;
static int s_capacity = 0;
#if COS_COMPILE_MODE == DEBUG
static bool async_initialized = false;
#endif /* COS_COMPILE_MODE */
/* Function Implementations -----------------------------------*/

void cos_dispatcher_init(void)
{
#if COS_COMPILE_MODE == DEBUG
    if (async_initialized)
        return;
#endif /* COS_COMPILE_MODE */

    s_capacity = 8;
    s_queue = cos_malloc(sizeof(cos_dispatcher_item_t) * s_capacity);
    if (!s_queue)
    {
        s_capacity = 0;
    }
    s_head = 0;
    s_tail = 0;
#if COS_COMPILE_MODE == DEBUG
    async_initialized = true;
#endif /* COS_COMPILE_MODE */
}

static int _queue_expand(void)
{
    int new_capacity = s_capacity * 2;
    cos_dispatcher_item_t *new_queue = cos_malloc(sizeof(cos_dispatcher_item_t) * new_capacity);
    if (!new_queue)
        return -1;

    int i = 0;
    for (int idx = s_head; idx != s_tail; idx = (idx + 1) % s_capacity)
    {
        new_queue[i++] = s_queue[idx];
    }

    cos_free(s_queue);
    s_queue = new_queue;
    s_head = 0;
    s_tail = i;
    s_capacity = new_capacity;
    return 0;
}

void cos_dispatcher_call(cos_dispatcher_cb_t cb, void *user_data)
{
#if COS_COMPILE_MODE == DEBUG
    if (!async_initialized)
        return;
#endif /* COS_COMPILE_MODE */
    if (!cb)
        return;

    cos_critical_ctx_t ctx = cos_critical_enter();

    int next = (s_tail + 1) % s_capacity;
    if (next == s_head)
    {
        if (_queue_expand() < 0)
        {
            cos_critical_leave(ctx);
            return;
        }
        next = (s_tail + 1) % s_capacity;
    }

    s_queue[s_tail].cb = cb;
    s_queue[s_tail].user_data = user_data;
    s_tail = next;

    cos_critical_leave(ctx);
}

void cos_dispatch_tick(void)
{
#if COS_COMPILE_MODE == DEBUG
    if (!async_initialized)
        return;
#endif

    if (s_head == s_tail)
        return;

    cos_critical_ctx_t ctx = cos_critical_enter();

    while (s_head != s_tail)
    {
        cos_dispatcher_item_t item = s_queue[s_head];
        s_head = (s_head + 1) & (s_capacity - 1);

        cos_critical_leave(ctx);

        item.cb(item.user_data);

        ctx = cos_critical_enter();
    }

    cos_critical_leave(ctx);
}
