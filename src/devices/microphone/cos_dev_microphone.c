/**
 * @file cos_dev_microphone.c
 * @brief Microphone device
 */
#include "cos_dev_microphone.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "DevMicrophone"
#include "cos_log.h"
#include "cos_error.h"

/* Variables --------------------------------------------------*/

static cos_dev_microphone_t _dev_microphone = {
    .ops = NULL,
    ._state = DEV_STATE_NONE,
};

/* Function Implementations -----------------------------------*/

cos_dev_microphone_t *cos_dev_microphone_get_instance(void)
{
    return &_dev_microphone;
}

cos_result_t cos_dev_microphone_register(const cos_dev_microphone_ops_t *ops)
{
    if (ops == NULL)
    {
        COS_LOG_E("OPS is NULL");
        return COS_ERR_INVALID_ARG;
    }

    if (_dev_microphone._state != DEV_STATE_NONE)
    {
        COS_LOG_W("Already registered");
        return COS_ERR_ALREADY_EXISTS;
    }

    if (ops->open == NULL || ops->close == NULL || ops->start == NULL || ops->stop == NULL || ops->set_buffer == NULL
        || ops->get_write_offset == NULL || ops->is_available == NULL)
    {
        COS_LOG_E("OPS incomplete: open/close/start/stop/set_buffer/get_write_offset/is_available required");
        return COS_ERR_INVALID_ARG;
    }

    _dev_microphone.ops = ops;
    _dev_microphone._state = DEV_STATE_READY;
    COS_LOG_I("Microphone registered");
    return COS_OK;
}

cos_dev_state_t cos_dev_microphone_get_state(void)
{
    return _dev_microphone._state;
}

void cos_dev_microphone_report_state(cos_dev_state_t state)
{
    if (_dev_microphone._state == state)
    {
        return;
    }
    COS_LOG_I("State: %d -> %d", _dev_microphone._state, state);
    _dev_microphone._state = state;
}
