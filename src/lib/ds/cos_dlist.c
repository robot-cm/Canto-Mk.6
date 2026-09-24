/**
 * @file cos_dlist.c
 * @brief Doubly linked list
 */

#include "cos_dlist.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "DList"
#include "cos_log.h"
#include "cos_mem.h"

/* Macros and Definitions -------------------------------------*/

struct cos_dlist_node_t
{
    void *data;
    struct cos_dlist_node_t *prev;
    struct cos_dlist_node_t *next;
};

struct cos_dlist_t
{
    struct cos_dlist_node_t *head;
    struct cos_dlist_node_t *tail;
    size_t size;
};

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

cos_dlist_t *cos_dlist_create(void)
{
    cos_dlist_t *list = cos_malloc_zeroed(sizeof(cos_dlist_t));
    COS_CHECK_PTR_RETURN_VAL(list, NULL);

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;

    COS_LOG_I("dlist created");
    return list;
}

void cos_dlist_destroy(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN(list);

    cos_dlist_clear(list);
    cos_free(list);
    COS_LOG_I("dlist destroyed");
}

bool cos_dlist_push_front(cos_dlist_t *list, void *data)
{
    COS_CHECK_PTR_RETURN_VAL(list, false);

    struct cos_dlist_node_t *node = cos_malloc_zeroed(sizeof(struct cos_dlist_node_t));
    COS_CHECK_PTR_RETURN_VAL(node, false);

    node->data = data;
    node->prev = NULL;
    node->next = list->head;

    if (list->head)
        list->head->prev = node;
    else
        list->tail = node;

    list->head = node;
    list->size++;

    COS_LOG_I("push_front data[%p]", data);
    return true;
}

bool cos_dlist_push_back(cos_dlist_t *list, void *data)
{
    COS_CHECK_PTR_RETURN_VAL(list, false);

    struct cos_dlist_node_t *node = cos_malloc_zeroed(sizeof(struct cos_dlist_node_t));
    COS_CHECK_PTR_RETURN_VAL(node, false);

    node->data = data;
    node->prev = list->tail;
    node->next = NULL;

    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;

    list->tail = node;
    list->size++;

    COS_LOG_I("push_back data[%p]", data);
    return true;
}

bool cos_dlist_insert_at(cos_dlist_t *list, size_t index, void *data)
{
    COS_CHECK_PTR_RETURN_VAL(list, false);

    if (index > list->size)
        return false;

    if (index == 0)
        return cos_dlist_push_front(list, data);

    if (index == list->size)
        return cos_dlist_push_back(list, data);

    struct cos_dlist_node_t *node = cos_malloc_zeroed(sizeof(struct cos_dlist_node_t));
    COS_CHECK_PTR_RETURN_VAL(node, false);

    node->data = data;

    struct cos_dlist_node_t *cur = list->head;
    for (size_t i = 0; i < index; i++)
        cur = cur->next;

    node->prev = cur->prev;
    node->next = cur;
    cur->prev->next = node;
    cur->prev = node;
    list->size++;

    COS_LOG_I("insert_at data[%p] at index %zu", data, index);
    return true;
}

void *cos_dlist_pop_front(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN_VAL(list, NULL);

    if (list->size == 0)
        return NULL;

    struct cos_dlist_node_t *node = list->head;
    void *data = node->data;

    list->head = node->next;
    if (list->head)
        list->head->prev = NULL;
    else
        list->tail = NULL;

    list->size--;
    cos_free(node);

    COS_LOG_I("pop_front data[%p]", data);
    return data;
}

void *cos_dlist_pop_back(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN_VAL(list, NULL);

    if (list->size == 0)
        return NULL;

    struct cos_dlist_node_t *node = list->tail;
    void *data = node->data;

    list->tail = node->prev;
    if (list->tail)
        list->tail->next = NULL;
    else
        list->head = NULL;

    list->size--;
    cos_free(node);

    COS_LOG_I("pop_back data[%p]", data);
    return data;
}

void *cos_dlist_remove_at(cos_dlist_t *list, size_t index)
{
    COS_CHECK_PTR_RETURN_VAL(list, NULL);

    if (index >= list->size)
        return NULL;

    if (index == 0)
        return cos_dlist_pop_front(list);

    if (index == list->size - 1)
        return cos_dlist_pop_back(list);

    struct cos_dlist_node_t *cur = list->head;
    for (size_t i = 0; i < index; i++)
        cur = cur->next;

    cur->prev->next = cur->next;
    cur->next->prev = cur->prev;

    void *data = cur->data;
    list->size--;
    cos_free(cur);

    COS_LOG_I("remove_at data[%p] at index %zu", data, index);
    return data;
}

bool cos_dlist_remove(cos_dlist_t *list, void *data)
{
    COS_CHECK_PTR_RETURN_VAL(list, false);

    struct cos_dlist_node_t *cur = list->head;
    while (cur)
    {
        if (cur->data == data)
        {
            if (cur->prev)
                cur->prev->next = cur->next;
            else
                list->head = cur->next;

            if (cur->next)
                cur->next->prev = cur->prev;
            else
                list->tail = cur->prev;

            list->size--;
            cos_free(cur);

            COS_LOG_I("remove data[%p]", data);
            return true;
        }
        cur = cur->next;
    }

    return false;
}

void cos_dlist_clear(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN(list);

    struct cos_dlist_node_t *cur = list->head;
    while (cur)
    {
        struct cos_dlist_node_t *next = cur->next;
        cos_free(cur);
        cur = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;

    COS_LOG_I("dlist cleared");
}

void *cos_dlist_get_front(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN_VAL(list, NULL);

    if (list->size == 0)
        return NULL;

    return list->head->data;
}

void *cos_dlist_get_back(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN_VAL(list, NULL);

    if (list->size == 0)
        return NULL;

    return list->tail->data;
}

void *cos_dlist_get_at(cos_dlist_t *list, size_t index)
{
    COS_CHECK_PTR_RETURN_VAL(list, NULL);

    if (index >= list->size)
        return NULL;

    struct cos_dlist_node_t *cur = list->head;
    for (size_t i = 0; i < index; i++)
        cur = cur->next;

    return cur->data;
}

size_t cos_dlist_get_size(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN_VAL(list, 0);
    return list->size;
}

bool cos_dlist_is_empty(cos_dlist_t *list)
{
    if (!list)
        return true;
    return list->size == 0;
}

int cos_dlist_find(cos_dlist_t *list, void *data)
{
    COS_CHECK_PTR_RETURN_VAL(list, -1);

    struct cos_dlist_node_t *cur = list->head;
    size_t index = 0;

    while (cur)
    {
        if (cur->data == data)
            return (int)index;
        cur = cur->next;
        index++;
    }

    return -1;
}

bool cos_dlist_reverse(cos_dlist_t *list)
{
    COS_CHECK_PTR_RETURN_VAL(list, false);

    if (list->size <= 1)
        return true;

    struct cos_dlist_node_t *cur = list->head;
    struct cos_dlist_node_t *tmp = NULL;

    while (cur)
    {
        tmp = cur->prev;
        cur->prev = cur->next;
        cur->next = tmp;
        cur = cur->prev;
    }

    tmp = list->head;
    list->head = list->tail;
    list->tail = tmp;

    COS_LOG_I("dlist reversed");
    return true;
}

bool cos_dlist_iterate(cos_dlist_t *list, cos_dlist_iter_cb_t callback, void *user_data)
{
    COS_CHECK_PTR_RETURN_VAL(list, false);
    COS_CHECK_PTR_RETURN_VAL(callback, false);

    struct cos_dlist_node_t *cur = list->head;
    while (cur)
    {
        if (!callback(cur->data, user_data))
            return false;
        cur = cur->next;
    }

    return true;
}

void **cos_dlist_to_array(cos_dlist_t *list, size_t *out_size)
{
    if (!list || !out_size)
        return NULL;

    *out_size = 0;

    if (list->size == 0)
        return NULL;

    void **arr = cos_malloc(list->size * sizeof(void *));
    COS_CHECK_PTR_RETURN_VAL(arr, NULL);

    struct cos_dlist_node_t *cur = list->head;
    size_t i = 0;

    while (cur)
    {
        arr[i++] = cur->data;
        cur = cur->next;
    }

    *out_size = list->size;
    return arr;
}
