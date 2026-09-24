/**
 * @file cos_dev_power.h
 * @brief Power device
 */

#ifndef COS_DEV_POWER_H
#define COS_DEV_POWER_H

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
typedef enum
{
    DEV_POWER_STATE_OFF = 0,
    DEV_POWER_STATE_ON,
    DEV_POWER_STATE_SLEEP,
    DEV_POWER_STATE_AOD,
} dev_power_state_t;

typedef struct
{
    int (*set_power)(dev_power_state_t state);
    /* 深睡前设置关机模式(timed=1: 定时唤醒自动开机,触摸无效;
     * timed=0: 触摸模式,如累计 5 下点击开机)。真机板级实现;
     * 模拟器可留空(NULL),PM 服务会判空跳过。 */
    int (*set_poweroff_params)(bool timed, uint32_t wake_after_s);
} cos_dev_power_ops_t;

typedef struct
{
    const cos_dev_power_ops_t *ops;
    cos_dev_state_t _state;
} cos_dev_power_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Get power device instance
 * @return Power device instance
 */
cos_dev_power_t *cos_dev_power_get_instance(void);

/**
 * @brief Register power device with OPS
 * @param ops Pointer to power OPS structure
 * @return COS_OK if successful, error code otherwise
 */
cos_result_t cos_dev_power_register(const cos_dev_power_ops_t *ops);

/**
 * @brief Get power device state
 * @return Current device state
 */
cos_dev_state_t cos_dev_power_get_state(void);

/**
 * @brief Report power device state (called by driver)
 * @param state New device state
 */
void cos_dev_power_report(cos_dev_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_POWER_H */
