/**
 * @file cos_service_state.c
 * @brief System state service implementation
 */

#include "cos_service_state.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_service_storage.h"
#include "cos_storage_paths.h"
#define COS_LOG_TAG "StateService"
#include "cos_log.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/
static bool _initialized = false;

/* Function Implementations -----------------------------------*/

void cos_service_state_init(void)
{
    if (_initialized)
    {
        return;
    }

    COS_LOG_D("Init cos_state");
    cos_storage_mkdir_if_not_exist(COS_SYS_DIR);
    cos_storage_json_create_if_not_exist(COS_STATE_FILE_PATH, NULL);

    _initialized = true;
}

cos_result_t cos_state_set_bool(const char *key, bool value)
{
    COS_CHECK_PTR_RETURN_VAL(key, COS_ERR_INVALID_ARG);

    COS_LOG_I("Set state \"%s\" = \"%s\"", key, value ? "true" : "false");
    return cos_storage_json_set_bool(COS_STATE_FILE_PATH, key, value);
}

cos_result_t cos_state_set_string(const char *key, const char *value)
{
    COS_CHECK_PTR_RETURN_VAL(key && value, COS_ERR_INVALID_ARG);

    COS_LOG_I("Set state \"%s\" = \"%s\"", key, value);
    return cos_storage_json_set_string(COS_STATE_FILE_PATH, key, value);
}

cos_result_t cos_state_set_number(const char *key, double value)
{
    COS_CHECK_PTR_RETURN_VAL(key, COS_ERR_INVALID_ARG);

    COS_LOG_I("Set state \"%s\" = \"%f\"", key, value);
    return cos_storage_json_set_number(COS_STATE_FILE_PATH, key, value);
}

bool cos_state_get_bool(const char *key, bool default_value)
{
    return cos_storage_json_get_bool(COS_STATE_FILE_PATH, key, default_value);
}

char *cos_state_get_string(const char *key, const char *default_value)
{
    return cos_storage_json_get_string(COS_STATE_FILE_PATH, key, default_value);
}

double cos_state_get_number(const char *key, double default_value)
{
    return cos_storage_json_get_number(COS_STATE_FILE_PATH, key, default_value);
}