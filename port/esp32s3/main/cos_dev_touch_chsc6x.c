/**
 * @file eos_dev_touch_chsc6x.c
 * @brief CHSC6X 电容触摸驱动(板级 port 层)
 *
 * 协议依据(AGENTS.md 第28节优先级 #2 Seeed 官方源码):
 *   Seeed_Arduino_RoundDisplay/src/lv_xiao_round_screen.h
 *   - I2C 地址 0x2E,直接读 5 字节(不发送寄存器地址)
 *   - temp[0]==0x01 触摸有效; X=temp[2], Y=temp[4](单字节,0-239)
 *   - INT(D7=GPIO44) 低有效
 *
 * 显示方向:GC9A01 madctl=0x00(无旋转)→ 触摸坐标直接映射,无需变换
 * 驱动方式:LVGL 9 POINTER indev 的 read timer 自动轮询
 *   (read_cb 随 lv_timer_handler() 在 ui_task 内执行,无需独立任务喂入)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eos_dev_touch_chsc6x.h"

#include "board_xiao_esp32s3_round.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "CHSC6X";

/* ════════════════════════════════════════════════════════════════
 *  触摸读取:INT 检查 + I2C 直读 5 字节 + 解析
 * ════════════════════════════════════════════════════════════════ */
bool eos_dev_touch_chsc6x_read(int32_t *x, int32_t *y)
{
    /* 1. INT 引脚(低有效)先判,高电平直接无触摸 */
    if (gpio_get_level(BOARD_TOUCH_INT_PIN) == 1) {
        return false;
    }

    /* 2. 直接读 5 字节(官方库无寄存器地址前缀):
     *    [0]=status(bit0 触摸), [1]=unused, [2]=X, [3]=unused, [4]=Y */
    uint8_t data[BOARD_TOUCH_READ_LEN] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        return false;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((BOARD_TOUCH_I2C_ADDR << 1) | I2C_MASTER_READ), true);
    i2c_master_read(cmd, data, BOARD_TOUCH_READ_LEN, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BOARD_I2C_HOST, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return false;
    }

    /* 3. status bit0 = 触摸有效(兼容官方 ==0x01 与位判断) */
    if (!(data[0] & 0x01)) {
        return false;
    }

    /* 4. 单字节坐标,8 位 0-239 直接落在 240 屏内 */
    *x = data[2];
    *y = data[4];
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  LVGL indev read callback
 * ════════════════════════════════════════════════════════════════ */
static void _read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int32_t x = 0, y = 0;
    if (eos_dev_touch_chsc6x_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ════════════════════════════════════════════════════════════════
 *  初始化:创建 LVGL POINTER indev
 *  LVGL 9 的 lv_indev_create() 自动创建 read timer 并 enable,
 *  read_cb 随 lv_timer_handler() 周期调用。
 *  Core 侧 eos_touch_get_indev() 通过遍历找到本 indev → 旋钮/手势可用
 * ════════════════════════════════════════════════════════════════ */
esp_err_t eos_dev_touch_chsc6x_init(void)
{
    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        ESP_LOGE(TAG, "lv_indev_create failed");
        return ESP_FAIL;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, _read_cb);
    lv_indev_set_display(indev, lv_display_get_default());

    ESP_LOGI(TAG, "CHSC6X ready (I2C%d 0x%02X, INT=GPIO%d, %dB direct read)",
             BOARD_I2C_HOST, BOARD_TOUCH_I2C_ADDR, BOARD_TOUCH_INT_PIN,
             BOARD_TOUCH_READ_LEN);
    return ESP_OK;
}
