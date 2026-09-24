/**
 * @file cos_dev_display.c
 * @brief Display device
 */

#include "cos_dev_display.h"

#define COS_LOG_TAG "DevDisplay"
#include "cos_log.h"

static cos_dev_display_t _dev_display = {
    .ops = NULL,
    ._state = DEV_STATE_NONE,
};

cos_dev_display_t *cos_dev_display_get_instance(void)
{
    return &_dev_display;
}

cos_result_t cos_dev_display_register(const cos_dev_display_ops_t *ops)
{
    if (ops == NULL)
    {
        COS_LOG_E("OPS is NULL");
        return COS_ERR_INVALID_ARG;
    }

    if (_dev_display._state != DEV_STATE_NONE)
    {
        COS_LOG_W("Already registered");
        return COS_ERR_ALREADY_EXISTS;
    }

    if (ops->set_brightness == NULL)
    {
        COS_LOG_E("OPS incomplete");
        return COS_ERR_INVALID_ARG;
    }

    _dev_display.ops = ops;
    _dev_display._state = DEV_STATE_READY;
    COS_LOG_I("Registered");
    return COS_OK;
}

cos_dev_state_t cos_dev_display_get_state(void)
{
    return _dev_display._state;
}

void cos_dev_display_report_state(cos_dev_state_t state)
{
    if (_dev_display._state == state)
    {
        return;
    }
    COS_LOG_I("State: %d -> %d", _dev_display._state, state);
    _dev_display._state = state;
}
