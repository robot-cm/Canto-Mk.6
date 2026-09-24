/**
 * @file cos_dev_vibrator.h
 * @brief Vibrator device
 */

#ifndef COS_DEV_VIBRATOR_H
#define COS_DEV_VIBRATOR_H

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
    void (*on)(uint8_t strength); /**< Strength range: 0-255 */
    void (*off)(void);
} cos_dev_vibrator_ops_t;

typedef struct
{
    const cos_dev_vibrator_ops_t *ops;
    cos_dev_state_t _state;
} cos_dev_vibrator_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Get vibrator device instance
 * @return Vibrator device instance
 */
cos_dev_vibrator_t *cos_dev_vibrator_get_instance(void);

/**
 * @brief Register vibrator device with OPS
 * @param ops Pointer to vibrator OPS structure
 * @return COS_OK if successful, error code otherwise
 */
cos_result_t cos_dev_vibrator_register(const cos_dev_vibrator_ops_t *ops);

/**
 * @brief Get vibrator device state
 * @return Current device state
 */
cos_dev_state_t cos_dev_vibrator_get_state(void);

/**
 * @brief Report vibrator device state (called by driver)
 * @param state New device state
 */
void cos_dev_vibrator_report_state(cos_dev_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_VIBRATOR_H */
