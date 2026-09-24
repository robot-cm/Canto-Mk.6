/**
 * @file cos_core.h
 * @brief CantoMk6 OS core header file
 */

#ifndef COS_CORE_H
#define COS_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_error.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Canto Mk.6 initialization function
 */
void cos_init(void);
/**
 * @brief Check whether Canto Mk.6 core has completed initialization
 */
bool cos_is_initialized(void);
/**
 * @brief Canto Mk.6 main loop function
 */
uint32_t cos_main_loop(void);
/**
 * @brief Request to skip the next boot animation (one-shot).
 * @note Deep-sleep/standby wake needs fast visual recovery; normal power-on
 * keeps the boot animation. Call from board layer before cos_init().
 */
void cos_boot_anim_skip_request(void);
/**
 * @brief Get the elapsed milliseconds since start up
 * @return Elapsed milliseconds
 */
uint32_t cos_tick_get(void);
/**
 * @brief Start Canto Mk.6 Logo page
 * @param anim Whether to display Logo fade animation
 * @note Will only be displayed once and will remain until the system is fully started.
 */
void cos_logo_play(bool anim);
/**
 * @brief Set periodic memory report interval (real ESP32-S3 only)
 * @param sec Report every N seconds; 0 disables the periodic report.
 * @note The report is emitted through the log system at INFO level so it can
 *       be watched with `idf.py monitor` (or any serial console).
 */
void cos_mem_report_set_interval(uint32_t sec);
/**
 * @brief Get the current periodic memory report interval
 * @return Seconds between reports, or 0 if disabled.
 */
uint32_t cos_mem_report_get_interval(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_CORE_H */
