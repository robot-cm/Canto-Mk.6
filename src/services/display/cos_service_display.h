/**
 * @file cos_service_display.h
 * @brief Display service
 */

#ifndef COS_SERVICE_DISPLAY_H
#define COS_SERVICE_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_error.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Brightness animation duration presets
 */
typedef enum
{
    COS_DISPLAY_DURATION_OFF = 0,
    COS_DISPLAY_DURATION_FAST = 100,
    COS_DISPLAY_DURATION_MEDIUM = 300,
    COS_DISPLAY_DURATION_SLOW = 500,
} cos_display_duration_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Set brightness
 * @param brightness Brightness value (0-100)
 * @param duration_ms Animation duration in milliseconds,
 * use COS_DISPLAY_DURATION_OFF for instant change,
 * can use cos_display_duration_t presets
 * @param is_temporary If true, save current brightness and set new brightness temporarily.
 * Call cos_display_restore() to restore to saved brightness.
 */
void cos_display_set_brightness(uint8_t brightness, cos_display_duration_t duration_ms, bool is_temporary);

/**
 * @brief Get the current brightness value (0-100)
 * @return Current brightness set via cos_display_set_brightness()
 */
uint8_t cos_display_get_brightness(void);

/**
 * @brief Power on the display
 */
void cos_display_power_on(void);

/**
 * @brief Power off the display
 */
void cos_display_power_off(void);

/**
 * @brief Restore brightness to saved value
 * @param duration_ms Animation duration in milliseconds,
 * use COS_DISPLAY_DURATION_OFF for instant change,
 * can use cos_display_duration_t presets
 */
void cos_display_restore(cos_display_duration_t duration_ms);

/**
 * @brief Set the LVGL refresh period (limits the maximum frame rate)
 * @param period_ms Refresh period in ms (e.g. 100 = ~10 FPS cap);
 * pass 0 to restore the system default (matches LV_DEF_REFR_PERIOD)
 */
void cos_display_refresh_period_set(uint32_t period_ms);

/**
 * @brief Backlight direct-drive diagnostic (bltest)
 * 绕过 PWM,把背光引脚直接拉高/拉低,用于验证引脚到背光的物理通路
 * (无需万用表):true=引脚拉高(屏幕应全亮), false=引脚拉低(屏幕应全黑)。
 * 仅诊断用:执行后下一次 cos_display_set_brightness() 会自动恢复 PWM。
 * @param high true=引脚输出高电平, false=引脚输出低电平
 * @return COS_OK 成功;COS_ERR_DEV_OPS_NOT_SUPPORTED 当前板级驱动未实现
 */
cos_result_t cos_display_bltest(bool high);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_DISPLAY_H */
