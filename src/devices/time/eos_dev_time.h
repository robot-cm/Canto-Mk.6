/**
 * @file eos_dev_time.h
 * @brief Time device
 */

#ifndef EOS_DEV_TIME_H
#define EOS_DEV_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "eos_device.h"
#include "eos_error.h"

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
} eos_datetime_t;

typedef struct eos_dev_time eos_dev_time_t;

typedef struct
{
    eos_datetime_t (*get_datetime)(void);
    /* 可选:写回 RTC。不支持写入的驱动可置 NULL。 */
    eos_result_t (*set_datetime)(eos_datetime_t dt);
} eos_dev_time_ops_t;

struct eos_dev_time
{
    const eos_dev_time_ops_t *ops;
    eos_dev_state_t _state;
};

/* Public function prototypes --------------------------------*/

/**
 * @brief Get time device instance
 * @return Time device instance
 */
eos_dev_time_t *eos_dev_time_get_instance(void);

/**
 * @brief Register time device with OPS
 * @param ops Pointer to time OPS structure
 * @return EOS_OK if successful, error code otherwise
 */
eos_result_t eos_dev_time_register(const eos_dev_time_ops_t *ops);

/**
 * @brief Get time device state
 * @return Current device state
 */
eos_dev_state_t eos_dev_time_get_state(void);

/**
 * @brief Report time device state (called by driver)
 * @param state New device state
 */
void eos_dev_time_report_state(eos_dev_state_t state);

/**
 * @brief Set datetime on time device (write back to RTC)
 * @param dt Datetime to set
 * @return EOS_OK on success, EOS_ERR_UNSUPPORTED if write not implemented
 */
eos_result_t eos_dev_time_set_datetime(eos_datetime_t dt);

#ifdef __cplusplus
}
#endif

#endif /* EOS_DEV_TIME_H */
