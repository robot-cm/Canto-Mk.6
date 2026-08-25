/**
 * @file eos_service_power_save.c
 * @brief Power save mode service (省电模式)
 *
 * 启用:
 *   1. 状态持久化(config key "power_save",重启后仍保持省电)
 *   2. 低亮度 (eos_display_set_brightness 20%)
 *   3. 降低 CPU 频率 (ESP32-S3: 240MHz -> 80MHz)
 *   4. 返回主界面 (eos_activity_back_to_watchface)
 *   5. 广播 EOS_EVENT_POWER_SAVE_CHANGED,watchface 据此显示/隐藏退出按钮
 * 禁用:
 *   1. 恢复亮度/频率
 *   2. 广播事件
 *
 * 页面锁定: eos_activity_enter() 与 watchface 手势回调均检查
 *   eos_power_save_is_active(),确保只能停留在主界面。
 */

#include "eos_service_power_save.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "eos_log.h"
#include "eos_event.h"
#include "eos_service_config.h"
#include "eos_service_display.h"
#include "eos_activity.h"
#include "eos_net_wifi.h"
#include "eos_net_bt.h"

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
/* ESP-IDF v5.3:esp_cpu.h 无运行时调频 API(esp_cpu_update_freq 为 v5.4+),
 * 动态调频统一走 esp_pm_configure(需 CONFIG_PM_ENABLE=y,sdkconfig.defaults 已开) */
#include "esp_pm.h"
#endif

/* Macros and Definitions -------------------------------------*/

#define _POWER_SAVE_CONFIG_KEY  "power_save"
#define _POWER_SAVE_BRIGHTNESS  20     /* 省电低亮度 20% */
#define _BRIGHTNESS_TRANS_MS    300

/* Variables --------------------------------------------------*/

static bool _active = false;
static eos_event_code_t _power_save_event_id = EOS_EVENT_LAST;
/* 进入省电前的无线状态:退出时恢复(省电期间同时关闭 WiFi + 蓝牙) */
static bool _wifi_was_enabled = false;
static bool _bt_was_enabled = false;

/* Function Implementations -----------------------------------*/

eos_result_t eos_service_power_save_init(void)
{
    _power_save_event_id = eos_event_register_id();
    _active = eos_config_get_bool(_POWER_SAVE_CONFIG_KEY, false);

    if (_active)
    {
        /* 重启后仍保持省电:直接应用低亮度(CPU 频率由 boot 默认,不强制降频) */
        eos_display_set_brightness(_POWER_SAVE_BRIGHTNESS, _BRIGHTNESS_TRANS_MS, true);
        /* 同步关闭无线(懒初始化安全;config init 可能已按配置恢复开机,这里强制关) */
        _wifi_was_enabled = eos_net_wifi_is_enabled();
        _bt_was_enabled = eos_config_get_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, false);
        eos_net_wifi_set_enabled(false);
        eos_net_bt_set_enabled(false);
    }
    EOS_LOG_I("Power save init: %s", _active ? "ACTIVE" : "inactive");
    return EOS_OK;
}

bool eos_power_save_is_active(void)
{
    return _active;
}

eos_event_code_t eos_power_save_get_event_id(void)
{
    return _power_save_event_id;
}

static void _cpu_freq_set(bool low)
{
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
    /* ESP32-S3 合法频率档:80 / 160 / 240 MHz。max=min=目标频率 → 强制锁频。
     * 恢复时用编译默认频率 CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ(160)。best-effort。 */
    esp_pm_config_t cfg = {
        .max_freq_mhz = low ? 80 : CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = low ? 80 : CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK)
    {
        EOS_LOG_W("CPU freq set(%s) failed: %d", low ? "low" : "high", (int)err);
    }
    else
    {
        EOS_LOG_I("CPU freq set to %dMHz", cfg.max_freq_mhz);
    }
#else
    (void)low;
#endif
}

eos_result_t eos_power_save_enter(void)
{
    if (_active)
    {
        return EOS_OK;
    }
    _active = true;
    eos_config_set_bool(_POWER_SAVE_CONFIG_KEY, true);

    /* 记录省电前的无线状态,然后同时关闭 WiFi + 蓝牙 */
    _wifi_was_enabled = eos_net_wifi_is_enabled();
    _bt_was_enabled = eos_config_get_bool(EOS_CONFIG_KEY_BLUETOOTH_BOOL, false);
    eos_net_wifi_set_enabled(false);
    eos_net_bt_set_enabled(false);
    EOS_LOG_I("Power save: WiFi/BT disabled (was %d/%d)", (int)_wifi_was_enabled, (int)_bt_was_enabled);

    eos_display_set_brightness(_POWER_SAVE_BRIGHTNESS, _BRIGHTNESS_TRANS_MS, true);
    _cpu_freq_set(true);

    /* 回到主界面(省电模式下只能停留在此) */
    eos_activity_back_to_watchface();

    if (_power_save_event_id != EOS_EVENT_LAST)
    {
        eos_event_post(_power_save_event_id, NULL, NULL);
    }
    EOS_LOG_I("Power save mode ENTERED");
    return EOS_OK;
}

eos_result_t eos_power_save_exit(void)
{
    if (!_active)
    {
        return EOS_OK;
    }
    _active = false;
    eos_config_set_bool(_POWER_SAVE_CONFIG_KEY, false);

    eos_display_restore(EOS_DISPLAY_DURATION_MEDIUM);
    _cpu_freq_set(false);

    /* 恢复省电前的无线状态 */
    eos_net_wifi_set_enabled(_wifi_was_enabled);
    eos_net_bt_set_enabled(_bt_was_enabled);
    EOS_LOG_I("Power save: WiFi/BT restored to %d/%d", (int)_wifi_was_enabled, (int)_bt_was_enabled);

    if (_power_save_event_id != EOS_EVENT_LAST)
    {
        eos_event_post(_power_save_event_id, NULL, NULL);
    }
    EOS_LOG_I("Power save mode EXITED");
    return EOS_OK;
}
