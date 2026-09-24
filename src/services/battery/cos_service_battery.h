/**
 * @file cos_service_battery.h
 * @brief Battery service
 */

#ifndef COS_SERVICE_BATTERY_H
#define COS_SERVICE_BATTERY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_error.h"
#include "cos_dev_battery.h"
#include "cos_event.h"

/* Public macros ----------------------------------------------*/

/* State keys for persistent storage -----*/
/**
 * @brief State key for battery capacity
 */
#define COS_STATE_KEY_CAPACITY "battery_capacity"

/**
 * @brief State key for battery cycle count
 */
#define COS_STATE_KEY_CYCLE_COUNT "cycle_count"

/* Public typedefs --------------------------------------------*/

/**
 * @brief Battery mode enumeration
 */
typedef enum
{
    COS_BATTERY_MODE_NORMAL = 0,
    COS_BATTERY_MODE_LOW_POWER,
    COS_BATTERY_MODE_CHARGING,
    COS_BATTERY_MODE_ACTIVE,
    COS_BATTERY_MODE_COUNT,
} cos_battery_mode_t;

/**
 * @brief Battery policy structure
 */
typedef struct
{
    uint32_t interval_ms;
    uint8_t threshold_percent;
} cos_battery_policy_t;

/**
 * @brief Battery raw data from hardware
 *
 * This structure contains the raw battery measurement data
 * as reported by the battery hardware device.
 */
typedef struct
{
    int8_t percent; /**< Battery percentage (0-100), -1 if not available */
    int16_t voltage_mv; /**< Battery voltage in millivolts, -1 if not available */
    int16_t current_ma; /**< Battery current in milliamps, positive=discharge, negative=charge */
    bool charging; /**< true if battery is charging */
} cos_battery_raw_t;

/**
 * @brief Battery processed state
 *
 * This structure contains the processed battery state including
 * timestamp and validity flag.
 */
typedef struct
{
    int8_t percent; /**< Battery percentage (0-100) */
    int16_t voltage_mv; /**< Battery voltage in millivolts */
    int16_t current_ma; /**< Battery current in milliamps */
    bool charging; /**< true if battery is charging */
    uint32_t ts; /**< Timestamp when this state was recorded */
    bool valid; /**< Validity flag, true if this state is valid */
} cos_battery_state_t;

/**
 * @brief Battery percentage calculation callback
 *
 * This callback type is used for custom voltage-to-percentage
 * calculation algorithms.
 *
 * @param voltage_mv Battery voltage in millivolts
 * @return Calculated percentage (0-100), or negative on error
 */
typedef int (*cos_battery_calc_fn_t)(int voltage_mv);

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize battery service
 *
 * Initializes the battery service and registers for battery update events.
 * This function should be called during system initialization.
 *
 * @return COS_OK on success, error code on failure
 */
cos_result_t cos_service_battery_init(void);

/**
 * @brief Report raw battery data from device
 *
 * Called by the battery device layer to report new battery measurements.
 * This function is typically called from the request_update callback
 * registered by the battery device.
 *
 * @param raw Pointer to raw battery data
 * @return None
 *
 * @note This function is thread-safe and can be called from interrupt context.
 */
void cos_battery_report_raw(const cos_battery_raw_t *raw);

/**
 * @brief Get current battery percentage
 *
 * @return Battery percentage (0-100), or -1 if not available
 */
int8_t cos_battery_get_percent(void);

/**
 * @brief Get current battery voltage
 *
 * @return Battery voltage in millivolts, or -1 if not available
 */
int16_t cos_battery_get_voltage_mv(void);

/**
 * @brief Check if battery is charging
 *
 * @return true if charging, false otherwise
 */
bool cos_battery_is_charging(void);

/**
 * @brief Get current battery state
 *
 * @param state Pointer to state structure to fill
 * @return true on success, false if service not initialized or state invalid
 */
bool cos_battery_get_state(cos_battery_state_t *state);

/**
 * @brief Get battery design capacity
 *
 * @return Design capacity in mAh
 */
uint32_t cos_battery_get_design_capacity(void);

/**
 * @brief Get battery current capacity
 *
 * @return Current estimated capacity in mAh
 */
uint32_t cos_battery_get_current_capacity(void);

/**
 * @brief Get battery cycle count
 *
 * @return Battery cycle count
 */
uint32_t cos_battery_get_cycle_count(void);

/**
 * @brief Set custom voltage-to-percent calculation function
 *
 * @param fn Custom calculation function, or NULL to use default
 * @return None
 */
void cos_battery_set_calc_fn(cos_battery_calc_fn_t fn);

/**
 * @brief Set policy for a specific battery mode
 *
 * @param mode Battery mode
 * @param policy Pointer to policy structure
 * @return COS_OK on success, COS_ERR_INVALID_ARG if mode is invalid
 */
cos_result_t cos_battery_set_policy(cos_battery_mode_t mode, const cos_battery_policy_t *policy);

/**
 * @brief Get policy for a specific battery mode
 *
 * @param mode Battery mode
 * @param policy Pointer to policy structure to fill
 * @return COS_OK on success, COS_ERR_INVALID_ARG if mode is invalid
 */
cos_result_t cos_battery_get_policy(cos_battery_mode_t mode, cos_battery_policy_t *policy);

/**
 * @brief Get event ID for battery state changes
 *
 * Use this event ID to register for battery state change notifications.
 * The event will be broadcast when battery percentage or charging state changes.
 *
 * @return Event ID for battery state change events
 */
cos_event_code_t cos_battery_get_event_id(void);

/**
 * @brief Get battery history entry count
 *
 * @return Number of entries in battery history
 */
uint32_t cos_battery_get_history_count(void);

/**
 * @brief Get battery history entry by index
 *
 * @param index History entry index (0 to history_count-1)
 * @param entry Pointer to state structure to fill
 * @return true on success, false on invalid index or service not initialized
 */
bool cos_battery_get_history_entry(uint32_t index, cos_battery_state_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_BATTERY_H */
