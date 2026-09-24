/**
 * @file cos_log_std.c
 * @brief Standard output log listener
 */

#include "cos_log.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_error.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

static const char *_log_level_to_str(cos_log_level_t level)
{
    switch (level)
    {
        case COS_LOG_LEVEL_DEBUG:
            return "DEBUG";
        case COS_LOG_LEVEL_INFO:
            return "INFO";
        case COS_LOG_LEVEL_WARN:
            return "WARN";
        case COS_LOG_LEVEL_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

static const char *_log_level_to_color(cos_log_level_t level)
{
    switch (level)
    {
        case COS_LOG_LEVEL_DEBUG:
            return COS_LOG_COLOR_CYAN;
        case COS_LOG_LEVEL_INFO:
            return COS_LOG_COLOR_GREEN;
        case COS_LOG_LEVEL_WARN:
            return COS_LOG_COLOR_YELLOW;
        case COS_LOG_LEVEL_ERROR:
            return COS_LOG_COLOR_RED;
        default:
            return COS_LOG_COLOR_RESET;
    }
}

static void _std_log_listener(cos_log_level_t level, const char *buf, size_t len, void *user)
{
    (void)user;
    (void)len;

#if COS_LOG_USE_COLOR
    printf("%s[%s] %s%s\n", _log_level_to_color(level), _log_level_to_str(level), buf, COS_LOG_COLOR_RESET);
#else
    printf("[%s] %s\n", _log_level_to_str(level), buf);
#endif

    fflush(stdout);
}

cos_result_t cos_service_log_std_register(void)
{
    cos_log_listener_id_t id = cos_log_register_listener("std_log", _std_log_listener, NULL, COS_LOG_FLAG_SYSTEM);

    return (id >= 0) ? COS_OK : COS_FAILED;
}
