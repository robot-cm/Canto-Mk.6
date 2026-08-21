/**
 * @file eos_service_pm.h
 * @brief Power Manager
 */

#ifndef EOS_SERVICE_PM_H
#define EOS_SERVICE_PM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef enum
{
    EOS_PM_DISPLAY_ON, /**< Wake mode */
    EOS_PM_DISPLAY_AOD, /**< Screen always-on (Always-On Display) mode */
    EOS_PM_SLEEP, /**< Sleep mode */
    EOS_PM_DEEP_SLEEP, /**< Deep sleep mode (simulator: black screen; real HW: ESP32 deep sleep) */
} eos_pm_state_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize power manager
 */
void eos_service_pm_init(void);

/**
 * @brief Wake up device
 */
void eos_pm_wake_up(void);

/**
 * @brief Set sleep timeout
 * @param sec Duration, unit: seconds
 */
void eos_pm_set_sleep_timeout(uint32_t sec);

/**
 * @brief Request to enter sleep mode (if AOD is enabled, enter AOD mode)
 */
void eos_pm_request_sleep(void);

/**
 * @brief Reset timer
 */
void eos_pm_reset_timer(void);

/**
 * @brief Get power manager state
 */
eos_pm_state_t eos_pm_get_state(void);

/**
 * @brief Set AOD mode
 * @param enable true = enable AOD mode, false = disable AOD mode
 */
void eos_pm_set_aod_mode(bool enable);

/**
 * @brief Request to enter DEEP SLEEP mode (screen off).
 * @param duration_sec Auto-wake after this many seconds; 0 = stay asleep
 *        until a manual wake (touch / eos_pm_wake_up()).
 * @note Simulator: black full-screen mask + touch auto-wake.
 *       Real HW: must be wired to esp_deep_sleep_start() with a timer
 *       wakeup source in the board bring-up layer.
 */
void eos_pm_deep_sleep_request(uint32_t duration_sec);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SERVICE_PM_H */
