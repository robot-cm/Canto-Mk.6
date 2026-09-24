/**
 * @file cos_dev_power.c
 * @brief Power device
 */

#include "cos_dev_power.h"

#define COS_LOG_TAG "DevPower"
#include "cos_log.h"

static cos_dev_power_t _dev_power = {
    .ops = NULL,
    ._state = DEV_STATE_NONE,
};

cos_dev_power_t *cos_dev_power_get_instance(void)
{
    return &_dev_power;
}

cos_result_t cos_dev_power_register(const cos_dev_power_ops_t *ops)
{
    if (ops == NULL)
    {
        COS_LOG_E("OPS is NULL");
        return COS_ERR_INVALID_ARG;
    }

    if (_dev_power._state != DEV_STATE_NONE)
    {
        COS_LOG_W("Already registered");
        return COS_ERR_ALREADY_EXISTS;
    }

    if (ops->set_power == NULL)
    {
        COS_LOG_E("OPS incomplete");
        return COS_ERR_INVALID_ARG;
    }

    _dev_power.ops = ops;
    _dev_power._state = DEV_STATE_READY;
    COS_LOG_I("Registered");
    return COS_OK;
}

cos_dev_state_t cos_dev_power_get_state(void)
{
    return _dev_power._state;
}

void cos_dev_power_report(cos_dev_state_t state)
{
    if (_dev_power._state == state)
    {
        return;
    }
    COS_LOG_I("State: %d -> %d", _dev_power._state, state);
    _dev_power._state = state;
}
