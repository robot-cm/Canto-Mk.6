/**
 * @file cos_dev_time.h
 * @brief Time device
 */

#ifndef COS_DEV_TIME_H
#define COS_DEV_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_device.h"
#include "cos_error.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint16_t ms;
    uint8_t day_of_week;
} cos_datetime_t;

typedef struct cos_dev_time cos_dev_time_t;

typedef struct
{
    cos_datetime_t (*get_datetime)(void);
    /* 可选:写回 RTC。不支持写入的驱动可置 NULL。 */
    cos_result_t (*set_datetime)(cos_datetime_t dt);
} cos_dev_time_ops_t;

struct cos_dev_time
{
    const cos_dev_time_ops_t *ops;
    cos_dev_state_t _state;
};

/* Public function prototypes --------------------------------*/

/**
 * @brief Get time device instance
 * @return Time device instance
 */
cos_dev_time_t *cos_dev_time_get_instance(void);

/**
 * @brief Register time device with OPS
 * @param ops Pointer to time OPS structure
 * @return COS_OK if successful, error code otherwise
 */
cos_result_t cos_dev_time_register(const cos_dev_time_ops_t *ops);

/**
 * @brief Get time device state
 * @return Current device state
 */
cos_dev_state_t cos_dev_time_get_state(void);

/**
 * @brief Report time device state (called by driver)
 * @param state New device state
 */
void cos_dev_time_report_state(cos_dev_state_t state);

/**
 * @brief Set datetime on time device (write back to RTC)
 * @param dt Datetime to set
 * @return COS_OK on success, COS_ERR_UNSUPPORTED if write not implemented
 */
cos_result_t cos_dev_time_set_datetime(cos_datetime_t dt);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_TIME_H */
