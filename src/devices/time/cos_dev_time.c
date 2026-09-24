/**
 * @file cos_dev_time.c
 * @brief Time device
 */

#include "cos_dev_time.h"

#define COS_LOG_TAG "DevTime"
#include "cos_log.h"

static cos_dev_time_t _dev_time = {
    .ops = NULL,
    ._state = DEV_STATE_NONE,
};

cos_dev_time_t *cos_dev_time_get_instance(void)
{
    return &_dev_time;
}

cos_result_t cos_dev_time_register(const cos_dev_time_ops_t *ops)
{
    if (ops == NULL)
    {
        COS_LOG_E("OPS is NULL");
        return COS_ERR_INVALID_ARG;
    }

    if (_dev_time._state != DEV_STATE_NONE)
    {
        COS_LOG_W("Already registered");
        return COS_ERR_ALREADY_EXISTS;
    }

    if (ops->get_datetime == NULL)
    {
        COS_LOG_E("OPS incomplete");
        return COS_ERR_INVALID_ARG;
    }

    _dev_time.ops = ops;
    _dev_time._state = DEV_STATE_READY;
    COS_LOG_I("Registered");
    return COS_OK;
}

cos_dev_state_t cos_dev_time_get_state(void)
{
    return _dev_time._state;
}

void cos_dev_time_report_state(cos_dev_state_t state)
{
    if (_dev_time._state == state)
    {
        return;
    }
    COS_LOG_I("State: %d -> %d", _dev_time._state, state);
    _dev_time._state = state;
}

cos_result_t cos_dev_time_set_datetime(cos_datetime_t dt)
{
    if (_dev_time.ops == NULL || _dev_time.ops->set_datetime == NULL)
    {
        COS_LOG_W("Time device write not supported");
        return COS_ERR_DEV_OPS_NOT_SUPPORTED;
    }
    return _dev_time.ops->set_datetime(dt);
}
