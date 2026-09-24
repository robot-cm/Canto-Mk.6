/**
 * @file eos_service_display.h
 * @brief Display service
 */

#ifndef EOS_SERVICE_DISPLAY_H
#define EOS_SERVICE_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "eos_error.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Brightness animation duration presets
 */
typedef enum
{
    EOS_DISPLAY_DURATION_OFF = 0,
    EOS_DISPLAY_DURATION_FAST = 100,
    EOS_DISPLAY_DURATION_MEDIUM = 300,
    EOS_DISPLAY_DURATION_SLOW = 500,
} eos_display_duration_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Set brightness
 * @param brightness Brightness value (0-100)
 * @param duration_ms Animation duration in milliseconds,
 * use EOS_DISPLAY_DURATION_OFF for instant change,
 * can use eos_display_duration_t presets
 * @param is_temporary If true, save current brightness and set new brightness temporarily.
 * Call eos_display_restore() to restore to saved brightness.
 */
void eos_display_set_brightness(uint8_t brightness, eos_display_duration_t duration_ms, bool is_temporary);

/**
 * @brief Get the current brightness value (0-100)
 * @return Current brightness set via eos_display_set_brightness()
 */
uint8_t eos_display_get_brightness(void);

/**
 * @brief Power on the display
 */
void eos_display_power_on(void);

/**
 * @brief Power off the display
 */
void eos_display_power_off(void);

/**
 * @brief Restore brightness to saved value
 * @param duration_ms Animation duration in milliseconds,
 * use EOS_DISPLAY_DURATION_OFF for instant change,
 * can use eos_display_duration_t presets
 */
void eos_display_restore(eos_display_duration_t duration_ms);

/**
 * @brief Set the LVGL refresh period (limits the maximum frame rate)
 * @param period_ms Refresh period in ms (e.g. 100 = ~10 FPS cap);
 * pass 0 to restore the system default (matches LV_DEF_REFR_PERIOD)
 */
void eos_display_refresh_period_set(uint32_t period_ms);

/**
 * @brief Backlight direct-drive diagnostic (bltest)
 * 绕过 PWM,把背光引脚直接拉高/拉低,用于验证引脚到背光的物理通路
 * (无需万用表):true=引脚拉高(屏幕应全亮), false=引脚拉低(屏幕应全黑)。
 * 仅诊断用:执行后下一次 eos_display_set_brightness() 会自动恢复 PWM。
 * @param high true=引脚输出高电平, false=引脚输出低电平
 * @return EOS_OK 成功;EOS_ERR_DEV_OPS_NOT_SUPPORTED 当前板级驱动未实现
 */
eos_result_t eos_display_bltest(bool high);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SERVICE_DISPLAY_H */
