/**
 * @file cos_dev_display.h
 * @brief Display device
 */

#ifndef COS_DEV_DISPLAY_H
#define COS_DEV_DISPLAY_H

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
    void (*set_brightness)(uint8_t brightness);
    void (*power_on)(void);
    void (*power_off)(void);
    /* 诊断用:绕过 PWM,把背光引脚直接拉高/拉低,验证引脚到背光的物理通路。
     * 可选(非所有板级驱动实现)。执行后下一次 set_brightness 应自动恢复 PWM。 */
    void (*bltest)(bool high);
} cos_dev_display_ops_t;

typedef struct
{
    const cos_dev_display_ops_t *ops;
    cos_dev_state_t _state;
} cos_dev_display_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Get display device instance
 * @return Display device instance
 */
cos_dev_display_t *cos_dev_display_get_instance(void);

/**
 * @brief Register display device with OPS
 * @param ops Pointer to display OPS structure
 * @return COS_OK if successful, error code otherwise
 */
cos_result_t cos_dev_display_register(const cos_dev_display_ops_t *ops);

/**
 * @brief Get display device state
 * @return Current device state
 */
cos_dev_state_t cos_dev_display_get_state(void);

/**
 * @brief Report display device state (called by driver)
 * @param state New device state
 */
void cos_dev_display_report_state(cos_dev_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_DISPLAY_H */
