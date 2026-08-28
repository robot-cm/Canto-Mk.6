/**
 * @file eos_service_beast_mode.c
 * @brief Beast mode service (性能模式)
 *
 * 启用:
 *   1. 状态持久化(config key "beast_mode",重启后仍保持)
 *   2. 锁定 CPU 最高主频 240MHz(禁用 DFS 降频),性能最激进
 *   3. 广播 EOS_EVENT_BEAST_MODE_CHANGED(状态变化事件)
 * 禁用:
 *   1. 恢复系统默认调频配置(智能模式:合理 CPU 使用、平衡性能与省电)
 *
 * 与省电模式互斥:
 *   - eos_beast_mode_enter() 会先退出省电模式
 *   - eos_power_save_enter() 会先退出性能模式
 *   - 两者都不启用 = 智能模式(系统默认)
 */

#include "eos_service_beast_mode.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "eos_log.h"
#include "eos_event.h"
#include "eos_service_config.h"
#include "eos_service_power_save.h"
#include "eos_port.h"

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
/* ESP-IDF v5.3:esp_cpu.h 无运行时调频 API(esp_cpu_update_freq 为 v5.4+),
 * 动态调频统一走 esp_pm_configure(需 CONFIG_PM_ENABLE=y,sdkconfig.defaults 已开) */
#include "esp_pm.h"
#endif

/* Macros and Definitions -------------------------------------*/

#define _BEAST_MODE_CONFIG_KEY "beast_mode"
#define _BEAST_MODE_CPU_MHZ    240   /* ESP32-S3 最高主频 */

/* Variables --------------------------------------------------*/

static bool _active = false;
static eos_event_code_t _beast_mode_event_id = EOS_EVENT_LAST;

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
static esp_pm_config_t _pm_normal_config;
static bool _pm_normal_config_valid = false;
#endif

/* Function Implementations -----------------------------------*/

bool eos_beast_mode_is_active(void)
{
    return _active;
}

eos_event_code_t eos_beast_mode_get_event_id(void)
{
    return _beast_mode_event_id;
}

static void _power_profile_apply(bool enabled)
{
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
    if (!_pm_normal_config_valid)
    {
        esp_err_t get_err = esp_pm_get_configuration(&_pm_normal_config);
        if (get_err != ESP_OK)
        {
            EOS_LOG_W("PM configuration read failed: %d", (int)get_err);
            return;
        }
        _pm_normal_config_valid = true;
    }

    /* 性能模式:锁定最高主频 240MHz,禁用 DFS 自动降频,性能最激进。
     * 关闭自动 Light-sleep:避免 UI 活跃期误入睡眠(与省电模式同一策略)。 */
    esp_pm_config_t cfg = enabled ? (esp_pm_config_t) {
        .max_freq_mhz = _BEAST_MODE_CPU_MHZ,
        .min_freq_mhz = _BEAST_MODE_CPU_MHZ,
        .light_sleep_enable = false,
    } : _pm_normal_config;
    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK)
    {
        EOS_LOG_W("PM profile %s failed: %d", enabled ? "beast-mode" : "normal", (int)err);
    }
    else
    {
        EOS_LOG_I("PM profile %s: CPU %d-%dMHz, auto light-sleep=%d",
                  enabled ? "beast-mode" : "normal", cfg.min_freq_mhz,
                  cfg.max_freq_mhz, (int)cfg.light_sleep_enable);
    }
#else
    (void)enabled;
#endif
}

eos_result_t eos_service_beast_mode_init(void)
{
    _beast_mode_event_id = eos_event_register_id();
    _active = eos_config_get_bool(_BEAST_MODE_CONFIG_KEY, false);

    /* 与省电模式互斥:若省电已激活(init 时序 power-save 在前),则放弃性能模式 */
    if (_active && eos_power_save_is_active())
    {
        EOS_LOG_W("Beast mode init skipped: power save is active");
        _active = false;
        eos_config_set_bool(_BEAST_MODE_CONFIG_KEY, false);
    }

    if (_active)
    {
        _power_profile_apply(true);
    }
    EOS_LOG_I("Beast mode init: %s", _active ? "ACTIVE" : "inactive");
    return EOS_OK;
}

eos_result_t eos_beast_mode_enter(void)
{
    if (_active)
    {
        return EOS_OK;
    }

    /* 互斥:开启性能模式前先退出省电模式 */
    if (eos_power_save_is_active())
    {
        EOS_LOG_I("Beast mode: exiting power save first");
        eos_power_save_exit();
    }

    _active = true;
    eos_config_set_bool(_BEAST_MODE_CONFIG_KEY, true);
    _power_profile_apply(true);

    if (_beast_mode_event_id != EOS_EVENT_LAST)
    {
        eos_event_post(_beast_mode_event_id, NULL, NULL);
    }
    EOS_LOG_I("Beast mode ENTERED (CPU locked %dMHz)", _BEAST_MODE_CPU_MHZ);
    return EOS_OK;
}

eos_result_t eos_beast_mode_exit(void)
{
    if (!_active)
    {
        return EOS_OK;
    }
    _active = false;
    eos_config_set_bool(_BEAST_MODE_CONFIG_KEY, false);
    _power_profile_apply(false);

    if (_beast_mode_event_id != EOS_EVENT_LAST)
    {
        eos_event_post(_beast_mode_event_id, NULL, NULL);
    }
    EOS_LOG_I("Beast mode EXITED (back to smart mode)");
    return EOS_OK;
}
