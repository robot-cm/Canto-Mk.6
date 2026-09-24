/**
 * @file cos_mem.c
 * @brief Memory allocation
 */

#include "cos_mem.h"

/* Includes ---------------------------------------------------*/
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COS_LOG_TAG "Memory"
#include "cos_log.h"
#include "cos_config.h"
#include "cos_port.h"
#include "cos_mem_port.h"
/* Macros and Definitions -------------------------------------*/
#if COS_MEM_TRACK_ENABLE
#define COS_MEM_TRACK_MAX 2048
typedef struct
{
    void *ptr;
    size_t size;
} cos_mem_track_t;
#endif /* COS_MEM_TRACK_ENABLE */
/* Variables --------------------------------------------------*/
#if COS_MEM_TRACK_ENABLE
static lv_mem_monitor_t mon;
static cos_mem_track_t _mem_track[COS_MEM_TRACK_MAX];
#endif /* COS_MEM_TRACK_ENABLE */
/* Function Implementations -----------------------------------*/

#if COS_MEM_TRACK_ENABLE

/************************** Memory Tracking Functions **************************/

static size_t cos_mem_track_find(void *ptr)
{
    if (!ptr)
        return 0;
    for (int i = 0; i < COS_MEM_TRACK_MAX; i++)
    {
        if (_mem_track[i].ptr == ptr)
            return _mem_track[i].size;
    }
    return 0;
}

static void cos_mem_track_add(void *ptr, size_t size)
{
    for (int i = 0; i < COS_MEM_TRACK_MAX; i++)
    {
        if (_mem_track[i].ptr == NULL)
        {
            _mem_track[i].ptr = ptr;
            _mem_track[i].size = size; // Update LVGL memory monitor
            mon.total_size += size;
            mon.used_cnt++;
            mon.max_used =
                mon.total_size - mon.free_size > mon.max_used ? mon.total_size - mon.free_size : mon.max_used;
            return;
        }
    }
    /* Table full: the accounting is now permanently wedged (a failed add is
       never removable by track_remove, so the table would stay full forever
       and every later free silently mismatches). Reset the tracker once and
       keep the stats coherent. Repeated resets in the log are a strong hint
       of a live leak, so the snapshot below doubles as a debug aid. */
    COS_LOG_W("Memory track table full: resetting tracker (live=%d, tracked=%dB, freed=%dB)",
              (int)mon.used_cnt, (int)mon.total_size, (int)mon.free_size);
    for (int i = 0; i < COS_MEM_TRACK_MAX; i++)
    {
        _mem_track[i].ptr = NULL;
        _mem_track[i].size = 0;
    }
    memset(&mon, 0, sizeof(mon));
    _mem_track[0].ptr = ptr;
    _mem_track[0].size = size;
    mon.total_size += size;
    mon.used_cnt++;
}
static size_t cos_mem_track_remove(void *ptr)
{
    if (!ptr)
        return 0;
    for (int i = 0; i < COS_MEM_TRACK_MAX; i++)
    {
        if (_mem_track[i].ptr == ptr)
        {
            size_t sz = _mem_track[i].size;
            _mem_track[i].ptr = NULL;
            _mem_track[i].size = 0;
            mon.free_cnt++;
            mon.free_size += sz;
            mon.used_cnt--;
            return sz;
        }
    }
    return 0;
}
#else
// Find pointer size
static size_t cos_mem_track_find(void *ptr)
{
    LV_UNUSED(ptr);
    return 0;
}

// Add tracking record
static void cos_mem_track_add(void *ptr, size_t size)
{
    LV_UNUSED(ptr);
    LV_UNUSED(size);
    return;
}

// Remove tracking record
static size_t cos_mem_track_remove(void *ptr)
{
    LV_UNUSED(ptr);
    return 0;
}
#endif /* COS_MEM_TRACK_ENABLE */

/************************** ElneaOS **************************/

void *cos_malloc(size_t size)
{
    void *p = cos_malloc_core(size);
    if (!p)
        return NULL;

    cos_mem_track_add(p, size);
    return p;
}

char *cos_strdup(const char *s)
{
    if (!s)
        return NULL;

    size_t len = strlen(s) + 1;
    char *copy = cos_malloc(len);
    if (copy)
    {
        memcpy(copy, s, len);
    }

    return copy;
}

void cos_free(void *ptr)
{
    if (!ptr)
        return;

    cos_mem_track_remove(ptr);
    cos_free_core(ptr);
}

void *cos_realloc(void *ptr, size_t new_size)
{
    size_t old_size = cos_mem_track_find(ptr);

    void *np = cos_realloc_core(ptr, new_size);
    if (!np)
        return NULL;

    cos_mem_track_remove(ptr);
    cos_mem_track_add(np, new_size);

    return np;
}

void *cos_malloc_zeroed(size_t size)
{
    void *p = cos_malloc_zeroed_core(size);
    if (!p)
        return NULL;

    cos_mem_track_add(p, size);
    return p;
}

/************************** LVGL **************************/

#if COS_OVERRIDE_LVGL_STDLIB_MALLOC_ENABLE

void lv_mem_init(void)
{
#if COS_MEM_TRACK_ENABLE
    memset(_mem_track, 0, sizeof(_mem_track));
    memset(&mon, 0, sizeof(mon));
#endif /* COS_MEM_TRACK_ENABLE */
}

void lv_mem_deinit(void)
{
    /* Not supported */
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    COS_LOG_W("Not supported");
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    COS_LOG_W("Not supported");
    LV_UNUSED(pool);
    return;
}

void *lv_malloc_core(size_t size)
{
    return cos_malloc(size);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return cos_realloc(p, new_size);
}

void lv_free_core(void *p)
{
    cos_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
#if COS_MEM_TRACK_ENABLE
    if (!mon_p)
        return;
    *mon_p = mon;
#else
    LV_UNUSED(mon_p);
#endif /* COS_MEM_TRACK_ENABLE */
    return;
}

lv_result_t lv_mem_test_core(void)
{
    /* Not supported */
    return LV_RESULT_OK;
}

#endif /* COS_OVERRIDE_LVGL_STDLIB_MALLOC_ENABLE */
