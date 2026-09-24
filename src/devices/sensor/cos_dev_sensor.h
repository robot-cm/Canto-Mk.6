/**
 * @file cos_dev_sensor.h
 * @brief Sensor device header file
 */

#ifndef COS_DEV_SENSOR_H
#define COS_DEV_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_device.h"
#include "cos_error.h"
#include "cos_event.h"
#include "lvgl.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Sensor type definitions
 */
typedef enum
{
    COS_SENSOR_TYPE_UNKNOWN = 0,
    COS_SENSOR_TYPE_ACCE, /**< Accelerometer */
    COS_SENSOR_TYPE_GYRO, /**< Gyroscope */
    COS_SENSOR_TYPE_HR, /**< Heart Rate Sensor */
    COS_SENSOR_TYPE_SPO2, /**< SpO2 Sensor */
    COS_SENSOR_TYPE_LIGHT, /**< Ambient Light Sensor */
    COS_SENSOR_TYPE_PROXIMITY, /**< Proximity Sensor */
    COS_SENSOR_TYPE_ECG, /**< ECG Sensor */
    COS_SENSOR_TYPE_TEMP, /**< Temperature Sensor */
    COS_SENSOR_TYPE_MAG, /**< Magnetometer */
    COS_SENSOR_TYPE_BARO, /**< Barometer */
    COS_SENSOR_TYPE_CAP, /**< Capacitance Sensor */
    COS_SENSOR_TYPE_STEP, /**< Step Counter */
    COS_SENSOR_TYPE_MAX
} cos_sensor_type_t;

/**
 * @brief Accelerometer data
 */
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} cos_sensor_data_acce_t;

/**
 * @brief Gyroscope data
 */
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} cos_sensor_data_gyro_t;

/**
 * @brief Magnetometer data
 */
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} cos_sensor_data_mag_t;

/**
 * @brief Temperature data
 */
typedef struct
{
    int32_t temp;
} cos_sensor_data_temp_t;

/**
 * @brief Barometer data
 */
typedef struct
{
    int32_t pressure;
} cos_sensor_data_baro_t;

/**
 * @brief Light sensor data
 */
typedef struct
{
    uint32_t lux;
} cos_sensor_data_light_t;

/**
 * @brief Proximity sensor data
 */
typedef struct
{
    uint16_t distance_mm;
} cos_sensor_data_proximity_t;

/**
 * @brief Heart rate sensor data
 */
typedef struct
{
    uint16_t heart_rate;
} cos_sensor_data_hr_t;

/**
 * @brief SpO2 sensor data
 */
typedef struct
{
    uint16_t spo2;
} cos_sensor_data_spo2_t;

/**
 * @brief ECG sensor data
 */
typedef struct
{
    uint16_t ecg;
} cos_sensor_data_ecg_t;

/**
 * @brief Capacitance sensor data
 */
typedef struct
{
    uint16_t cap;
} cos_sensor_data_cap_t;

/**
 * @brief Step counter data
 */
typedef struct
{
    uint32_t steps;
} cos_sensor_data_step_t;

/**
 * @brief Battery sensor data
 */
typedef struct
{
    uint8_t level;
    bool charging;
} cos_sensor_data_battery_t;

/**
 * @brief Sensor data union
 */
typedef union
{
    cos_sensor_data_acce_t acce;
    cos_sensor_data_gyro_t gyro;
    cos_sensor_data_mag_t mag;
    cos_sensor_data_temp_t temp;
    cos_sensor_data_baro_t baro;
    cos_sensor_data_light_t light;
    cos_sensor_data_proximity_t proximity;
    cos_sensor_data_hr_t hr;
    cos_sensor_data_spo2_t spo2;
    cos_sensor_data_ecg_t ecg;
    cos_sensor_data_cap_t cap;
    cos_sensor_data_step_t step;
    cos_sensor_data_battery_t battery;
} cos_sensor_data_t;

/**
 * @brief Raw sensor data with timestamp
 */
typedef struct
{
    cos_sensor_type_t type;
    cos_sensor_data_t data;
    uint32_t timestamp;
} cos_sensor_raw_data_t;

typedef struct cos_dev_sensor_t cos_dev_sensor_t;

/**
 * @brief Sensor data-ready callback — called by the device driver when the hardware
 *        FIFO watermark is reached or new data is available via interrupt.
 * @param type  Sensor type that has data ready
 * @param count Number of samples available in the hardware FIFO
 */
typedef void (*cos_sensor_data_ready_cb_t)(cos_sensor_type_t type, uint32_t count);

/**
 * @brief Sensor device operations
 * @note Device layer does NOT provide read operation. Data is pushed to service layer
 *       via cos_sensor_notify(). Service layer manages FIFO and broadcasts to subscribers.
 *
 *       The last three ops are OPTIONAL (NULL = not supported). When not implemented,
 *       the service layer falls back to software-timer-based polling via the port layer.
 */
typedef struct
{
    /* ---- Required ops ---- */
    void (*init)(cos_dev_sensor_t *dev);
    void (*deinit)(cos_dev_sensor_t *dev);
    void (*enable)(cos_dev_sensor_t *dev);
    void (*disable)(cos_dev_sensor_t *dev);
    void (*set_sample_rate)(cos_dev_sensor_t *dev, uint32_t hz);
    void (*get_sample_rate)(cos_dev_sensor_t *dev, uint32_t *hz);

    /* ---- Optional hardware-FIFO ops (NULL = unsupported) ---- */
    /**
     * @brief Set the hardware FIFO watermark threshold.
     *        When the sensor's internal FIFO reaches this many samples,
     *        the driver fires the data_ready callback.
     * @param dev     Sensor device
     * @param samples Watermark in number of samples (0 = disable watermark IRQ)
     */
    void (*set_fifo_watermark)(cos_dev_sensor_t *dev, uint16_t samples);

    /**
     * @brief Flush (clear) the hardware FIFO.
     * @param dev Sensor device
     */
    void (*flush_fifo)(cos_dev_sensor_t *dev);

    /**
     * @brief Register a data-ready callback for interrupt-driven batch reads.
     *        When the hardware FIFO watermark is reached, the driver calls `cb`
     *        (from ISR or a deferred task). The service layer then reads batched
     *        data and pushes it through cos_sensor_notify().
     * @param dev Sensor device
     * @param cb  Callback (NULL = unregister)
     */
    void (*set_data_ready_cb)(cos_dev_sensor_t *dev, cos_sensor_data_ready_cb_t cb);
} cos_dev_sensor_ops_t;

/**
 * @brief Sensor device structure
 */
struct cos_dev_sensor_t
{
    const cos_dev_sensor_ops_t *ops;
    const char *name;
    cos_sensor_type_t type;
    cos_dev_state_t _state;
    cos_event_code_t _event_id;
    struct cos_dev_sensor_t *_next;
};

/* Public function prototypes --------------------------------*/

/**
 * @brief Register a sensor device
 * @param name Device name
 * @param type Sensor type
 * @param ops Device operations pointer
 * @return cos_result_t Operation result
 */
cos_result_t cos_dev_sensor_register(const char *name, cos_sensor_type_t type, const cos_dev_sensor_ops_t *ops);

/**
 * @brief Find sensor device by name
 * @param name Device name
 * @return cos_dev_sensor_t* Device pointer or NULL
 */
cos_dev_sensor_t *cos_dev_sensor_find(const char *name);

/**
 * @brief Find sensor device by type
 * @param type Sensor type
 * @return cos_dev_sensor_t* Device pointer or NULL
 */
cos_dev_sensor_t *cos_dev_sensor_find_by_type(cos_sensor_type_t type);

/**
 * @brief Get default sensor device by type
 * @param type Sensor type
 * @return cos_dev_sensor_t* Device pointer or NULL
 */
cos_dev_sensor_t *cos_dev_sensor_get_default(cos_sensor_type_t type);

/**
 * @brief Get sensor device state
 * @param dev Device pointer
 * @return cos_dev_state_t Device state
 */
cos_dev_state_t cos_dev_sensor_get_state(cos_dev_sensor_t *dev);

/**
 * @brief Report sensor device state
 * @param dev Device pointer
 * @param state New state
 */
void cos_dev_sensor_report_state(cos_dev_sensor_t *dev, cos_dev_state_t state);

/**
 * @brief Get sensor device event ID
 * @param dev Device pointer
 * @return cos_event_code_t Event ID
 */
cos_event_code_t cos_dev_sensor_get_event_id(cos_dev_sensor_t *dev);

/**
 * @brief Get sensor device type
 * @param dev Device pointer
 * @return cos_sensor_type_t Sensor type
 */
cos_sensor_type_t cos_dev_sensor_get_type(cos_dev_sensor_t *dev);

/**
 * @brief Get the head of sensor list for iteration
 * @return cos_dev_sensor_t* Pointer to the first sensor in the list
 */
cos_dev_sensor_t *cos_dev_sensor_get_list_head(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_SENSOR_H */
