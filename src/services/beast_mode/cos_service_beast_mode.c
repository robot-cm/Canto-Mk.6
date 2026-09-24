/**
 * @file cos_service_beast_mode.c
 * @brief Beast mode service (性能模式)
 *
 * 启用:
 *   1. 状态持久化(config key "beast_mode",重启后仍保持)
 *   2. 锁定 CPU 最高主频 240MHz(禁用 DFS 降频),性能最激进
 *   3. 收紧 LVGL 刷新周期(默认 20ms → 10ms,动画/手势更跟手)
 *   4. 广播 COS_EVENT_BEAST_MODE_CHANGED(状态变化事件)
 * 禁用:
 *   1. 恢复系统默认调频配置(DFS 80-160MHz,智能模式)
 *   2. 恢复 LVGL 默认刷新周期(20ms)
 *
 * 边界:性能模式【只】动 CPU 频率与 LVGL 刷新周期——
 *   - 不改屏幕亮度(与智能模式一致,沿用用户/省电退出恢复值)
 *   - 不改熄屏/深睡规则(熄屏超时 10s / L2 15min 与智能模式完全相同,
 *     beast 服务不触碰 PM sleep timer / L1 / L2 时序)
 * 与省电模式互斥:
 *   - cos_beast_mode_enter() 会先退出省电模式
 *   - cos_power_save_enter() 会先退出性能模式
 *   - 两者都不启用 = 智能模式(系统默认)
 */

#include "cos_service_beast_mode.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "cos_log.h"
#include "cos_event.h"
#include "cos_service_config.h"
#include "cos_service_power_save.h"
#include "cos_service_display.h" /* cos_display_refresh_period_set:刷新周期 0=复位默认 */
#include "cos_service_cc_snapshot.h"
#include "cos_port.h"

#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
/* ESP-IDF v5.3:esp_cpu.h 无运行时调频 API(esp_cpu_update_freq 为 v5.4+),
 * 动态调频统一走 esp_pm_configure(需 CONFIG_PM_ENABLE=y,sdkconfig.defaults 已开) */
#include "esp_pm.h"
#endif

/* Macros and Definitions -------------------------------------*/

#define _BEAST_MODE_CONFIG_KEY "beast_mode"
#define _BEAST_MODE_CPU_MHZ    240   /* ESP32-S3 最高主频 */
#define _BEAST_MODE_REFR_PERIOD_MS 10 /* LVGL 刷新周期:默认 20ms → 10ms(50→100FPS 上限) */

/* Variables --------------------------------------------------*/

static bool _active = false;
static cos_event_code_t _beast_mode_event_id = COS_EVENT_LAST;

#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
static esp_pm_config_t _pm_normal_config;
static bool _pm_normal_config_valid = false;
#endif

/* Function Implementations -----------------------------------*/

bool cos_beast_mode_is_active(void)
{
    return _active;
}

cos_event_code_t cos_beast_mode_get_event_id(void)
{
    return _beast_mode_event_id;
}

static void _power_profile_apply(bool enabled)
{
#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
    if (!_pm_normal_config_valid)
    {
        esp_err_t get_err = esp_pm_get_configuration(&_pm_normal_config);
        if (get_err != ESP_OK)
        {
            COS_LOG_W("PM configuration read failed: %d", (int)get_err);
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
        COS_LOG_W("PM profile %s failed: %d", enabled ? "beast-mode" : "normal", (int)err);
    }
    else
    {
        COS_LOG_I("PM profile %s: CPU %d-%dMHz, auto light-sleep=%d",
                  enabled ? "beast-mode" : "normal", cfg.min_freq_mhz,
                  cfg.max_freq_mhz, (int)cfg.light_sleep_enable);
    }
#else
    (void)enabled;
#endif
}

/* 性能模式:收紧 LVGL 刷新周期(20→10ms),动画/手势更跟手。
 * 边界:与省电模式互斥(激活状态不可能共存);只调刷新周期,
 * 不改亮度/熄屏/深睡(那些只由省电模式收窄)。模拟器同样生效
 * (display 服务跨平台,刷新周期走 LVGL refr timer)。 */
static void _refresh_period_apply(bool enabled)
{
    cos_display_refresh_period_set(enabled ? _BEAST_MODE_REFR_PERIOD_MS : 0);
}

cos_result_t cos_service_beast_mode_init(void)
{
    _beast_mode_event_id = cos_event_register_id();
    _active = cos_config_get_bool(_BEAST_MODE_CONFIG_KEY, false);

    /* 与省电模式互斥:若省电已激活(init 时序 power-save 在前),则放弃性能模式 */
    if (_active && cos_power_save_is_active())
    {
        COS_LOG_W("Beast mode init skipped: power save is active");
        _active = false;
        cos_config_set_bool(_BEAST_MODE_CONFIG_KEY, false);
    }

    if (_active)
    {
        _power_profile_apply(true);
        _refresh_period_apply(true);
    }
    COS_LOG_I("Beast mode init: %s", _active ? "ACTIVE" : "inactive");
    return COS_OK;
}

cos_result_t cos_beast_mode_enter(void)
{
    if (_active)
    {
        return COS_OK;
    }

    /* 互斥:开启性能模式前先退出省电模式 */
    if (cos_power_save_is_active())
    {
        COS_LOG_I("Beast mode: exiting power save first");
        cos_power_save_exit();
    }

    _active = true;
    cos_config_set_bool(_BEAST_MODE_CONFIG_KEY, true);
    _power_profile_apply(true);
    _refresh_period_apply(true);

    /* 快照电源模式到 SD(/sdcard/history/cc)。无真 SD 卡时空操作,
     * 仍以 cfg.json 的 "beast_mode" 为准(见 cos_service_cc_snapshot.h)。 */
    cos_cc_snapshot_store_power(COS_CC_POWER_BEAST);

    if (_beast_mode_event_id != COS_EVENT_LAST)
    {
        cos_event_post(_beast_mode_event_id, NULL, NULL);
    }
    COS_LOG_I("Beast mode ENTERED (CPU locked %dMHz, LVGL refresh %dms)",
              _BEAST_MODE_CPU_MHZ, _BEAST_MODE_REFR_PERIOD_MS);
    return COS_OK;
}

cos_result_t cos_beast_mode_exit(void)
{
    if (!_active)
    {
        return COS_OK;
    }
    _active = false;
    cos_config_set_bool(_BEAST_MODE_CONFIG_KEY, false);
    _power_profile_apply(false);
    _refresh_period_apply(false);

    /* 退出性能模式:回到智能档,同步刷新快照 */
    cos_cc_snapshot_store_power(COS_CC_POWER_SMART);

    if (_beast_mode_event_id != COS_EVENT_LAST)
    {
        cos_event_post(_beast_mode_event_id, NULL, NULL);
    }
    COS_LOG_I("Beast mode EXITED (back to smart mode, LVGL refresh back to default)");
    return COS_OK;
}
