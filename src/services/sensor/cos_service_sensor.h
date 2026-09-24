/**
 * @file cos_service_sensor.h
 * @brief Sensor service header file
 */

#ifndef COS_SERVICE_SENSOR_H
#define COS_SERVICE_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_error.h"
#include "cos_dev_sensor.h"
#include "cos_fifo.h"
#include "cos_event.h"

/* Public macros ----------------------------------------------*/

#define COS_SENSOR_FIFO_CAPACITY 64

/* Public typedefs --------------------------------------------*/

/**
 * @brief Sensor data callback function
 * @param type      Sensor type that produced the data
 * @param data      Sensor data with timestamp (caller-owned; copy if needed)
 * @param user_data User data registered at subscribe time
 * @note  This replaces the previous cos_event_cb_t alias. The callback
 *        now receives typed sensor data directly instead of an opaque
 *        cos_event_t pointer requiring cos_event_get_param().
 */
typedef void (*cos_sensor_data_cb_t)(cos_sensor_type_t type, const cos_sensor_raw_data_t *data, void *user_data);

/**
 * @brief Sensor operating mode — determines sampling policy
 */
typedef enum
{
    COS_SENSOR_MODE_NORMAL = 0, /**< Normal operation (display on, user active) */
    COS_SENSOR_MODE_LOW_POWER, /**< Low-power (AOD, battery save) */
    COS_SENSOR_MODE_ACTIVE, /**< Active (workout / high-motion) */
    COS_SENSOR_MODE_SLEEP, /**< Sleep (display off, no user interaction) */
    COS_SENSOR_MODE_COUNT,
} cos_sensor_mode_t;

/**
 * @brief Per-mode sensor sampling policy
 */
typedef struct
{
    uint32_t min_interval_ms; /**< Floor for any subscriber request in this mode */
    bool allow_enable; /**< Whether sensors may be powered on in this mode */
} cos_sensor_policy_t;

/**
 * @brief Subscriber node — links callback, user_data, and interval.
 *        Single source of truth for sensor data push subscribers.
 *        Used for direct callback invocation in cos_sensor_notify().
 */
typedef struct _sensor_subscriber_t
{
    cos_sensor_data_cb_t cb;
    void *user_data;
    uint32_t min_interval_ms;
    struct _sensor_subscriber_t *next;
    bool marked_for_delete; /**< Deferred deletion flag for safe broadcast */
} sensor_subscriber_t;

/**
 * @brief Sensor service instance structure
 */
typedef struct
{
    cos_sensor_type_t type;
    cos_dev_sensor_t *device;
    cos_fifo_t *fifo;
    cos_sensor_raw_data_t latest_data;
    uint16_t subscriber_count;
    uint32_t sample_period_ms;
    uint32_t last_sample_time;
    bool is_active;
    bool is_enabled; /**< Whether the hardware device is currently enabled */
    sensor_subscriber_t *subscribers; /**< Linked list of active subscribers */
} cos_sensor_service_instance_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize sensor service
 * @return cos_result_t Operation result
 */
cos_result_t cos_service_sensor_init(void);

/**
 * @brief Read sensor data from FIFO (Pull mode)
 * @param type Sensor type
 * @param data Pointer to store sensor data
 * @return cos_result_t Operation result
 */
cos_result_t cos_sensor_read(cos_sensor_type_t type, cos_sensor_raw_data_t *data);

/**
 * @brief Read latest sensor data
 * @param type Sensor type
 * @param data Pointer to store sensor data
 * @return cos_result_t Operation result
 */
cos_result_t cos_sensor_read_latest(cos_sensor_type_t type, cos_sensor_raw_data_t *data);

/**
 * @brief Subscribe to sensor data (Push mode)
 * @param type Sensor type
 * @param cb Callback function
 * @param user_data User data passed to callback
 * @param min_interval_ms Minimum callback interval in milliseconds
 * @return cos_result_t Operation result
 */
cos_result_t cos_sensor_subscribe(cos_sensor_type_t type,
                                  cos_sensor_data_cb_t cb,
                                  void *user_data,
                                  uint32_t min_interval_ms);

/**
 * @brief Unsubscribe from sensor data
 * @param type Sensor type
 * @param cb Callback function
 * @param user_data User data
 * @return cos_result_t Operation result
 */
cos_result_t cos_sensor_unsubscribe(cos_sensor_type_t type, cos_sensor_data_cb_t cb, void *user_data);

/**
 * @brief Set sensor sample period
 * @param type Sensor type
 * @param period_ms Sample period in milliseconds
 * @return cos_result_t Operation result
 */
cos_result_t cos_sensor_set_sample_period(cos_sensor_type_t type, uint32_t period_ms);

/**
 * @brief Get sensor sample period
 * @param type Sensor type
 * @return uint32_t Sample period in milliseconds
 */
uint32_t cos_sensor_get_sample_period(cos_sensor_type_t type);

/**
 * @brief Notify sensor data (called by driver)
 * @param type Sensor type
 * @param data Sensor data
 * @param timestamp Data timestamp
 */
void cos_sensor_notify(cos_sensor_type_t type, const cos_sensor_data_t *data, uint32_t timestamp);

/**
 * @brief Set sensor mode policy
 * @param mode Sensor operating mode
 * @param policy Policy configuration (NULL to reset to default)
 * @return cos_result_t Operation result
 */
cos_result_t cos_sensor_set_mode_policy(cos_sensor_mode_t mode, const cos_sensor_policy_t *policy);

/**
 * @brief Get current sensor mode policy
 * @param mode Sensor operating mode
 * @param policy Output for policy configuration
 * @return cos_result_t Operation result
 */
cos_result_t cos_sensor_get_mode_policy(cos_sensor_mode_t mode, cos_sensor_policy_t *policy);

/**
 * @brief Get current sensor operating mode
 * @return cos_sensor_mode_t Current mode
 */
cos_sensor_mode_t cos_sensor_get_current_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_SENSOR_H */
