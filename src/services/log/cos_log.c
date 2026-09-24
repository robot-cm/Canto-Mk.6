/**
 * @file cos_log.c
 * @brief Listener-based log system implementation
 */

#include "cos_log.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cos_error.h"
#include "cos_config.h"

/* Macros and Definitions -------------------------------------*/
#define COS_LOG_BUFFER_SIZE 1024

/* Variables --------------------------------------------------*/
static cos_log_listener_t s_listeners[COS_LOG_MAX_LISTENERS] = {0};
static bool _initialized = false;
static cos_log_level_t s_min_level = COS_LOG_LEVEL_DEBUG;

/* Function Implementations -----------------------------------*/

void cos_service_log_init(void)
{
    if (_initialized)
    {
        return;
    }

    memset(s_listeners, 0, sizeof(s_listeners));
    _initialized = true;
#if COS_LOG_ENABLE_STD
    cos_service_log_std_register();
#endif
}

cos_log_listener_id_t cos_log_register_listener(const char *name,
                                                cos_log_listener_cb_t cb,
                                                void *user_data,
                                                uint8_t flags)
{
    if (!_initialized || !name || !cb)
    {
        return -1;
    }

    for (int i = 0; i < COS_LOG_MAX_LISTENERS; i++)
    {
        if (!s_listeners[i].used)
        {
            s_listeners[i].name = name;
            s_listeners[i].cb = cb;
            s_listeners[i].user_data = user_data;
            s_listeners[i].flags = flags;
            s_listeners[i].used = 1;
            return i;
        }
    }

    return -1;
}

cos_result_t cos_log_unregister_listener(cos_log_listener_id_t id)
{
    if (!_initialized || id < 0 || id >= COS_LOG_MAX_LISTENERS)
    {
        return COS_ERR_INVALID_ARG;
    }

    if (!s_listeners[id].used)
    {
        return COS_ERR_INVALID_ARG;
    }

    if (s_listeners[id].flags & COS_LOG_FLAG_SYSTEM)
    {
        return COS_ERR_INVALID_ARG;
    }

    memset(&s_listeners[id], 0, sizeof(cos_log_listener_t));
    return COS_OK;
}

cos_log_listener_id_t cos_log_find_listener(const char *name)
{
    if (!_initialized || !name)
    {
        return -1;
    }

    for (int i = 0; i < COS_LOG_MAX_LISTENERS; i++)
    {
        if (s_listeners[i].used && s_listeners[i].name && strcmp(s_listeners[i].name, name) == 0)
        {
            return i;
        }
    }

    return -1;
}

cos_result_t cos_log_get_listener(cos_log_listener_id_t id, cos_log_listener_t *listener)
{
    if (!_initialized || !listener || id < 0 || id >= COS_LOG_MAX_LISTENERS)
    {
        return COS_ERR_INVALID_ARG;
    }

    if (!s_listeners[id].used)
    {
        return COS_ERR_INVALID_ARG;
    }

    memcpy(listener, &s_listeners[id], sizeof(cos_log_listener_t));
    return COS_OK;
}

void cos_log_dispatch(cos_log_level_t level, const char *buf, size_t len)
{
    if (!_initialized || !buf || len == 0)
    {
        return;
    }

    cos_log_listener_t snapshot[COS_LOG_MAX_LISTENERS];
    memcpy(snapshot, s_listeners, sizeof(snapshot));

    for (int i = 0; i < COS_LOG_MAX_LISTENERS; i++)
    {
        if (snapshot[i].used && snapshot[i].cb)
        {
            snapshot[i].cb(level, buf, len, snapshot[i].user_data);
        }
    }
}

void cos_log_set_min_level(cos_log_level_t level)
{
    if (level < COS_LOG_LEVEL_DEBUG)
        level = COS_LOG_LEVEL_DEBUG;
    if (level > COS_LOG_LEVEL_OFF)
        level = COS_LOG_LEVEL_OFF;
    s_min_level = level;
}

cos_log_level_t cos_log_get_min_level(void)
{
    return s_min_level;
}

void cos_log(cos_log_level_t level, const char *fmt, ...)
{
    if (!_initialized || !fmt)
    {
        return;
    }

    if (level < s_min_level)
    {
        return;
    }

    static char buffer[COS_LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(buffer, COS_LOG_BUFFER_SIZE, fmt, args);
    va_end(args);

    if (len > 0 && len < COS_LOG_BUFFER_SIZE)
    {
        cos_log_dispatch(level, buffer, (size_t)len);
    }
}
