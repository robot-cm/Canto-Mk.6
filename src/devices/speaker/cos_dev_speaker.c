/**
 * @file cos_dev_speaker.c
 * @brief Speaker device
 */

#include "cos_dev_speaker.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "DevSpeaker"
#include "cos_log.h"
#include "cos_error.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

static cos_dev_speaker_t _dev_speaker = {
    .ops = NULL,
    ._state = DEV_STATE_NONE,
};

/* Function Implementations -----------------------------------*/

cos_dev_speaker_t *cos_dev_speaker_get_instance(void)
{
    return &_dev_speaker;
}

cos_result_t cos_dev_speaker_register(const cos_dev_speaker_ops_t *ops)
{
    if (ops == NULL)
    {
        COS_LOG_E("OPS is NULL");
        return COS_ERR_INVALID_ARG;
    }

    if (_dev_speaker._state != DEV_STATE_NONE)
    {
        COS_LOG_W("Already registered");
        return COS_ERR_ALREADY_EXISTS;
    }

    if (ops->open == NULL || ops->borrow == NULL || ops->enqueue == NULL || ops->stop == NULL || ops->set_volume == NULL
        || ops->is_available == NULL)
    {
        COS_LOG_E("OPS incomplete: open, borrow, enqueue, stop, set_volume, is_available are required");
        return COS_ERR_INVALID_ARG;
    }

    _dev_speaker.ops = ops;
    _dev_speaker._state = DEV_STATE_READY;
    COS_LOG_I("Speaker registered");
    return COS_OK;
}

cos_dev_state_t cos_dev_speaker_get_state(void)
{
    return _dev_speaker._state;
}

void cos_dev_speaker_report_state(cos_dev_state_t state)
{
    if (_dev_speaker._state == state)
    {
        return;
    }
    COS_LOG_I("State: %d -> %d", _dev_speaker._state, state);
    _dev_speaker._state = state;
}
