/**
 * @file cos_dev_rtc_bm8563.h
 * @brief RTC BM8563 (PCF8563 compatible) I2C driver for XIAO Round Display
 *
 * PCF8563 / BM8563 均为 NXP PCF8563 寄存器兼容芯片(I2C 0x51)。
 * 本驱动实现真实的 get/set datetime,替换阶段B 的 stub。
 */

#ifndef COS_DEV_RTC_BM8563_H
#define COS_DEV_RTC_BM8563_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cos_dev_time.h"

/**
 * @brief Initialize BM8563 RTC and register into cos_dev_time HAL
 * @return COS_OK on success
 */
cos_result_t cos_dev_rtc_bm8563_init(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_RTC_BM8563_H */
