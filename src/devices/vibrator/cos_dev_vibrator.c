/**
 * @file cos_dev_vibrator.c
 * @brief Vibrator device
 */

#include "cos_dev_vibrator.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "DevVibrator"
#include "cos_log.h"
#include "cos_error.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

static cos_dev_vibrator_t _dev_vibrator = {
    .ops = NULL,
    ._state = DEV_STATE_NONE,
};

/* Function Implementations -----------------------------------*/

cos_dev_vibrator_t *cos_dev_vibrator_get_instance(void)
{
    return &_dev_vibrator;
}

cos_result_t cos_dev_vibrator_register(const cos_dev_vibrator_ops_t *ops)
{
    if (ops == NULL)
    {
        COS_LOG_E("OPS is NULL");
        return COS_ERR_INVALID_ARG;
    }

    if (_dev_vibrator._state != DEV_STATE_NONE)
    {
        COS_LOG_W("Already registered");
        return COS_ERR_ALREADY_EXISTS;
    }

    if (ops->on == NULL || ops->off == NULL)
    {
        COS_LOG_E("OPS incomplete");
        return COS_ERR_INVALID_ARG;
    }

    _dev_vibrator.ops = ops;
    _dev_vibrator._state = DEV_STATE_READY;
    COS_LOG_I("Vibrator registered");
    return COS_OK;
}

cos_dev_state_t cos_dev_vibrator_get_state(void)
{
    return _dev_vibrator._state;
}

void cos_dev_vibrator_report_state(cos_dev_state_t state)
{
    if (_dev_vibrator._state == state)
    {
        return;
    }
    COS_LOG_I("State: %d -> %d", _dev_vibrator._state, state);
    _dev_vibrator._state = state;
}
