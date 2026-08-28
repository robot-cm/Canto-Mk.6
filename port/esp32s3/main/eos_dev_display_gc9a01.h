/**
 * @file eos_dev_display_gc9a01.h
 * @brief GC9A01 LCD 驱动(ElenixOS 设备 HAL + LVGL 端口)— 板级实现
 *
 * 位置:port/esp32s3/main/(板级专属,非 Core。因引用 esp_lcd.h 等 ESP-IDF 头)
 * Core HAL 接口在 src/devices/display/eos_dev_display.h(平台无关)
 *
 * 实现:
 *   - 基于 ESP-IDF 官方 esp_lcd_panel_gc9a01 组件(AGENTS.md 第28节官方资料优先)
 *   - 包成 ElenixOS eos_dev_display_ops_t(set_brightness / power_on / power_off)
 *   - 提供 LVGL 9.x flush 回调 + display 初始化
 *
 * 引脚依据:子报告1(Seeed 官方库 lv_xiao_round_screen.h 双重验证)
 *   - CS=D1=GPIO2, DC=D3=GPIO4, BL=D6=GPIO43
 *   - SCK=D8=GPIO7, MOSI=D10=GPIO9, MISO=D9=GPIO8(LCD 不读,可省)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EOS_DEV_DISPLAY_GC9A01_H
#define EOS_DEV_DISPLAY_GC9A01_H

#ifdef __cplusplus
extern "C" {
#endif

#include "eos_dev_display.h"
#include "esp_err.h"

/* GC9A01 驱动初始化(板级 main 调用)
 * 完成:SPI bus + panel IO + GC9A01 panel + 背光 PWM
 * 返回:ESP_OK 成功
 */
esp_err_t eos_dev_display_gc9a01_init(void);

/* 注册到 ElenixOS 设备 HAL
 * 把 GC9A01 的 set_brightness/power_on/power_off 注册进 eos_dev_display
 */
void eos_dev_display_gc9a01_register(void);

/* LVGL 显示初始化(板级 main 调用)
 * 完成:lv_display_create + set_buffers + set_flush_cb
 * buffer 策略(AGENTS.md 第22节):partial 10 行,internal DMA 内存
 */
esp_err_t eos_dev_display_gc9a01_lvgl_init(void);

/* 清屏纯黑(关机深睡前置调用):
 * GC9A01 无硬件 RST,面板 GRAM 保留最后一帧;深睡前刷黑,即使背光意外
 * 点亮,残留帧也是黑帧,避免关机页面残影闪烁。 */
void eos_dev_display_gc9a01_fill_black(void);

/* 面板显示关闭(0x28 DISPOFF,关机深睡前调用):
 * 仅关面板显示、保留 GRAM;深睡轮询每次唤醒的 boot 窗口背光反亮,
 * 面板 OFF 后显示黑帧,消除"快速亮起"闪烁。轻量单命令,避免深睡前
 * 大量 SPI 活动导致 esp_deep_sleep_start 失败。 */
void eos_dev_display_gc9a01_display_off(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_DEV_DISPLAY_GC9A01_H */
