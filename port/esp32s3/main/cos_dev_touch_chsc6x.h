/**
 * @file cos_dev_touch_chsc6x.h
 * @brief CHSC6X 电容触摸驱动(板级 port 层)
 *
 * 协议依据(AGENTS.md 第28节优先级 #2 Seeed 官方源码):
 *   Seeed_Arduino_RoundDisplay/src/lv_xiao_round_screen.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COS_DEV_TOUCH_CHSC6X_H
#define COS_DEV_TOUCH_CHSC6X_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 CHSC6X 触摸:创建 LVGL POINTER indev(read timer 自动轮询)
 *
 * 前置条件:
 *   - I2C 总线已初始化(board_i2c_bus_init)
 *   - 触摸 INT 引脚已配置(board_gpio_init)
 *   - LVGL display 已注册(cos_dev_display_gc9a01_lvgl_init)
 */
esp_err_t cos_dev_touch_chsc6x_init(void);

/**
 * @brief 读取触摸坐标
 * @param[out] x 屏幕 X(0-239)
 * @param[out] y 屏幕 Y(0-239)
 * @return true=有触摸,false=无触摸/读取失败
 */
bool cos_dev_touch_chsc6x_read(int32_t *x, int32_t *y);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_TOUCH_CHSC6X_H */
