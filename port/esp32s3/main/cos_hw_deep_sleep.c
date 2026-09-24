/**
 * @file cos_hw_deep_sleep.c
 * @brief ESP32-S3 真机硬件深度睡眠实现(覆盖 PM 服务 weak 钩子)
 *
 * 原理:
 *   - esp_deep_sleep_start():CPU / 大部分 RAM / 外设断电,仅 RTC 域保持;
 *   - 外部 RTC(BM8563,CR927 电池供电)继续走时 → 唤醒后时间自动正确;
 *   - esp_sleep_enable_timer_wakeup():RTC 定时器到点 → 芯片重启(bootloader
 *     重新引导 app_main),各服务按新时间自然恢复/命中事件(闹钟、倒计时、
 *     睡眠规则拆分段的唤醒点)。
 *
 * 唤醒源:
 *   - RTC 定时器(唯一可用):触屏 INT 在 GPIO44,非 ESP32-S3 的 RTC GPIO
 *     (GPIO0~21),故硬件深睡无法触摸唤醒,只能定时唤醒。
 */

#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "board_xiao_esp32s3_round.h"
#include "cos_port.h"

static const char *TAG = "HWDeepSleep";

static void _backlight_off(void)
{
    /* 深睡后 LEDC 停止,把 BL 引脚强制拉低避免屏幕亮着 */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_GC9A01_BL_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_GC9A01_BL_PIN, 0);
}

void cos_hw_deep_sleep(uint32_t sleep_sec)
{
    ESP_LOGI(TAG, "Entering hardware deep sleep, wake in %u s", (unsigned)sleep_sec);

    _backlight_off();

    /* BLE 断电(esp_deep_sleep 前关闭外设省电) */
    cos_net_bt_backend_power_down();

    if (sleep_sec == 0)
        sleep_sec = 60; /* 兜底:触摸无法唤醒,至少 60s 定时唤醒防变砖 */

    esp_sleep_enable_timer_wakeup((uint64_t)sleep_sec * 1000000ULL);
    ESP_LOGI(TAG, "Timer wakeup armed: %u s", (unsigned)sleep_sec);

    /* 进入真正硬件深睡;不返回,到点后芯片重启 */
    esp_deep_sleep_start();
    while (1)
    {
        /* unreachable */
    }
}
