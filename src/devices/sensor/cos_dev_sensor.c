/**
 * @file cos_dev_sensor.c
 * @brief Sensor device implementation
 */

/* Includes ---------------------------------------------------*/
#include <stdlib.h>
#include <string.h>
#include "cos_dev_sensor.h"
#include "cos_event.h"
#include "cos_port_critical.h"
#include "cos_mem.h"
/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/
static cos_dev_sensor_t *_sensor_list = NULL;
static cos_dev_sensor_t *_default_sensors[COS_SENSOR_TYPE_MAX] = {NULL};

/* Function Implementations -----------------------------------*/

cos_result_t cos_dev_sensor_register(const char *name, cos_sensor_type_t type, const cos_dev_sensor_ops_t *ops)
{
    if (!name || !ops || type >= COS_SENSOR_TYPE_MAX || type <= COS_SENSOR_TYPE_UNKNOWN)
        return COS_ERR_INVALID_ARG;

    cos_critical_ctx_t ctx = cos_critical_enter();

    cos_dev_sensor_t *iter = _sensor_list;
    while (iter)
    {
        if (strcmp(iter->name, name) == 0)
        {
            cos_critical_leave(ctx);
            return COS_ERR_ALREADY_EXISTS;
        }
        iter = iter->_next;
    }

    cos_dev_sensor_t *sensor = (cos_dev_sensor_t *)cos_malloc(sizeof(cos_dev_sensor_t));
    if (!sensor)
    {
        cos_critical_leave(ctx);
        return COS_ERR_MEM;
    }

    memset(sensor, 0, sizeof(cos_dev_sensor_t));
    sensor->name = name;
    sensor->type = type;
    sensor->ops = ops;
    sensor->_state = DEV_STATE_READY;
    sensor->_event_id = cos_event_register_id();
    sensor->_next = _sensor_list;
    _sensor_list = sensor;

    if (_default_sensors[type] == NULL)
    {
        _default_sensors[type] = sensor;
    }

    cos_critical_leave(ctx);
    return COS_OK;
}

cos_dev_sensor_t *cos_dev_sensor_find(const char *name)
{
    if (!name)
        return NULL;

    cos_critical_ctx_t ctx = cos_critical_enter();

    cos_dev_sensor_t *iter = _sensor_list;
    while (iter)
    {
        if (strcmp(iter->name, name) == 0)
        {
            cos_critical_leave(ctx);
            return iter;
        }
        iter = iter->_next;
    }

    cos_critical_leave(ctx);
    return NULL;
}

cos_dev_sensor_t *cos_dev_sensor_find_by_type(cos_sensor_type_t type)
{
    if (type >= COS_SENSOR_TYPE_MAX || type <= COS_SENSOR_TYPE_UNKNOWN)
        return NULL;

    cos_critical_ctx_t ctx = cos_critical_enter();

    cos_dev_sensor_t *iter = _sensor_list;
    while (iter)
    {
        if (iter->type == type)
        {
            cos_critical_leave(ctx);
            return iter;
        }
        iter = iter->_next;
    }

    cos_critical_leave(ctx);
    return NULL;
}

cos_dev_sensor_t *cos_dev_sensor_get_default(cos_sensor_type_t type)
{
    if (type >= COS_SENSOR_TYPE_MAX || type <= COS_SENSOR_TYPE_UNKNOWN)
        return NULL;

    return _default_sensors[type];
}

cos_dev_state_t cos_dev_sensor_get_state(cos_dev_sensor_t *dev)
{
    if (!dev)
        return DEV_STATE_NONE;

    cos_critical_ctx_t ctx = cos_critical_enter();
    cos_dev_state_t state = dev->_state;
    cos_critical_leave(ctx);

    return state;
}

void cos_dev_sensor_report_state(cos_dev_sensor_t *dev, cos_dev_state_t state)
{
    if (!dev)
        return;

    cos_critical_ctx_t ctx = cos_critical_enter();
    dev->_state = state;
    cos_critical_leave(ctx);
}

cos_event_code_t cos_dev_sensor_get_event_id(cos_dev_sensor_t *dev)
{
    if (!dev)
        return COS_EVENT_UNKNOWN;

    return dev->_event_id;
}

cos_sensor_type_t cos_dev_sensor_get_type(cos_dev_sensor_t *dev)
{
    if (!dev)
        return COS_SENSOR_TYPE_UNKNOWN;

    return dev->type;
}

cos_dev_sensor_t *cos_dev_sensor_get_list_head(void)
{
    return _sensor_list;
}
