/**
 * @file eos_service_power_save.c
 * @brief Power save mode service (省电模式)
 *
 * 启用:
 *   1. 状态持久化(config key "power_save",重启后仍保持省电)
 *   2. 最低亮度 (5%,0=熄灭不可读)
 *   3. DFS(80-160MHz) + 自动 Light-sleep（保持触摸唤醒与运行上下文）
 *   4. 熄屏/待机收紧:无触摸 5s 熄屏 Light Sleep;熄屏累计 10min → L2 自动
 *      待机 Deep Sleep(运行时覆盖,退出恢复;不写持久 config)
 *   5. 限制屏幕刷新率(LVGL 刷新周期 20→100ms,退出恢复)
 *   6. 返回主界面 (eos_activity_back_to_watchface)
 *   7. 广播 EOS_EVENT_POWER_SAVE_CHANGED
 * 禁用:
 *   1. 恢复亮度/频率/熄屏超时/L2 阈值/刷新周期
 *   2. 广播事件
 *
 * 页面锁定: watchface 手势回调均检查 eos_power_save_is_active(),
 *   只保留右滑打开 Control Center(可在其中关闭省电开关)。
 */

#include "eos_service_power_save.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "eos_log.h"
#include "eos_event.h"
#include "eos_service_config.h"
#include "eos_service_display.h"
#include "eos_service_pm.h"
#include "eos_activity.h"
#include "eos_net_wifi.h"
#include "eos_net_bt.h"
#include "eos_service_beast_mode.h"
#include "eos_port.h"

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
/* ESP-IDF v5.3:esp_cpu.h 无运行时调频 API(esp_cpu_update_freq 为 v5.4+),
 * 动态调频统一走 esp_pm_configure(需 CONFIG_PM_ENABLE=y,sdkconfig.defaults 已开) */
#include "sdkconfig.h"
#include "esp_pm.h"
#endif

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
/* L2 自动待机阈值查询/设置接口:定义于 port/esp32s3/main/main.c(板级) */
extern void     eos_pm_set_l2_idle_ms(uint32_t idle_ms);
extern uint32_t eos_pm_get_l2_idle_ms(void);
#endif

/* Macros and Definitions -------------------------------------*/

#define _POWER_SAVE_CONFIG_KEY  "power_save"
#define _POWER_SAVE_BRIGHTNESS  5      /* 省电最低亮度 5%(0=背光熄灭不可读,5% 夜间可辨识) */
#define _BRIGHTNESS_TRANS_MS    300
#define _POWER_SAVE_CPU_MIN_MHZ 80
#define _POWER_SAVE_CPU_MAX_MHZ 160

/* 熄屏超时(5s)/L2 待机阈值(10min)/刷新周期(100ms)策略宏来自
 * eos_service_power_save.h(与 PM 服务初始对齐共用同一来源) */

/* Variables --------------------------------------------------*/

static bool _active = false;
static eos_event_code_t _power_save_event_id = EOS_EVENT_LAST;
/* 进入省电前的无线状态:退出时恢复(省电期间同时关闭 WiFi + 蓝牙) */
static bool _wifi_was_enabled = false;
static bool _bt_was_enabled = false;

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
static esp_pm_config_t _pm_normal_config;
static bool _pm_normal_config_valid = false;
#endif

/* 进入省电前的运行参数记录(退出恢复) */
static uint32_t _prev_l2_idle_ms = 0; /* L2 自动待机阈值(ms) */

/* 熄屏/L2/刷新收紧开关(启用见文件头注释):
 * 无触摸 5s 熄屏 Light Sleep(运行时覆盖,不写持久 config);
 * 熄屏累计 10min → L2 自动待机 Deep Sleep;刷新周期压到 ~10FPS。
 * 退出时恢复到进入前记录值(sleep 超时以最新持久 config 为准)。 */
static void _idle_policy_apply(bool enabled)
{
    if (enabled)
    {
        eos_pm_set_sleep_timeout(EOS_POWER_SAVE_SLEEP_TIMEOUT_SEC);
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
        _prev_l2_idle_ms = eos_pm_get_l2_idle_ms();
        if (_prev_l2_idle_ms != 0)
            eos_pm_set_l2_idle_ms(EOS_POWER_SAVE_L2_IDLE_MS);
#endif
        /* 限制 LVGL 刷新率(~10 FPS),退出时复位默认周期 */
        eos_display_refresh_period_set(EOS_POWER_SAVE_REFR_PERIOD_MS);
    }
    else
    {
        eos_pm_set_sleep_timeout(
            (uint32_t)eos_config_get_number(EOS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, 10));
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
        if (_prev_l2_idle_ms != 0)
        {
            eos_pm_set_l2_idle_ms(_prev_l2_idle_ms);
            _prev_l2_idle_ms = 0;
        }
#endif
        eos_display_refresh_period_set(0); /* 复位系统默认刷新周期 */
    }
}

static void _radios_power_down(void)
{
    eos_net_wifi_set_enabled(false);
    eos_net_bt_set_enabled(false);
    eos_result_t bt_result = eos_net_bt_backend_power_down();
    if (bt_result != EOS_OK)
        EOS_LOG_W("BLE controller power-down failed: %d", (int)bt_result);
}

/* Function Implementations -----------------------------------*/

bool eos_power_save_is_active(void)
{
    return _active;
}

eos_event_code_t eos_power_save_get_event_id(void)
{
    return _power_save_event_id;
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
        /* 未做过 esp_pm_configure 时读到的是空配置(0),直接用它恢复会失败:
         * 回落为 sdkconfig 的 CPU 默认频率,保证退出省电一定能恢复常态。 */
        if (_pm_normal_config.max_freq_mhz == 0)
        {
            _pm_normal_config.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
        }
        if (_pm_normal_config.min_freq_mhz == 0)
        {
            _pm_normal_config.min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
        }
        _pm_normal_config_valid = true;
    }

    /* 省电模式:仅做 DFS 降频(80-160MHz),【不】在此开启自动 Light-sleep。
     * 原因(实测):light_sleep_enable=true 会让系统在 UI 活跃期也进 Light-sleep,
     * USB-Serial-JTAG 控制台在 sleep 中失活 -> shell 写超时/冻结;低主频下
     * LVGL 刷新节拍与 sleep 进/出互相打架 -> 背光明灭闪烁。
     * 真正的 Light-sleep 留给"无交互闲置"子状态单独触发(见 _idle 逻辑),
     * 由触摸/输入唤醒后先关 light_sleep 再恢复,避免与活跃 UI 冲突。
     * Deep-sleep 不在此使用:GPIO44 触摸无法唤醒 ESP32-S3,且 GPIO43 背光
     * 会被板上拉在深睡后反亮。 */
    esp_pm_config_t cfg = enabled ? (esp_pm_config_t) {
        .max_freq_mhz = _POWER_SAVE_CPU_MAX_MHZ,
        .min_freq_mhz = _POWER_SAVE_CPU_MIN_MHZ,
        .light_sleep_enable = false,
    } : _pm_normal_config;
    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK)
    {
        EOS_LOG_W("PM profile %s failed: %d", enabled ? "power-save" : "normal", (int)err);
    }
    else
    {
        EOS_LOG_I("PM profile %s: CPU %d-%dMHz, auto light-sleep=%d",
                  enabled ? "power-save" : "normal", cfg.min_freq_mhz,
                  cfg.max_freq_mhz, (int)cfg.light_sleep_enable);
    }
#else
    (void)enabled;
#endif
}

eos_result_t eos_service_power_save_init(void)
{
    _power_save_event_id = eos_event_register_id();
    _active = eos_config_get_bool(_POWER_SAVE_CONFIG_KEY, false);

    if (_active)
    {
        eos_display_set_brightness(_POWER_SAVE_BRIGHTNESS, _BRIGHTNESS_TRANS_MS, true);
        _wifi_was_enabled = eos_net_wifi_is_enabled();
        _bt_was_enabled = eos_net_bt_is_enabled();
        _radios_power_down();
        _power_profile_apply(true);
        /* 收紧 L2/刷新周期并记录恢复值;熄屏 5s 超时此处设置会因 PM 服务
         * (晚于本服务 init)的 t 尚未创建而落空,已在 PM init 按其激活态对齐 */
        _idle_policy_apply(true);
    }
    EOS_LOG_I("Power save init: %s", _active ? "ACTIVE" : "inactive");
    return EOS_OK;
}

eos_result_t eos_power_save_enter(void)
{
    if (_active)
    {
        return EOS_OK;
    }

    /* 互斥:开启省电模式前先退出性能模式 */
    if (eos_beast_mode_is_active())
    {
        EOS_LOG_I("Power save: exiting beast mode first");
        eos_beast_mode_exit();
    }

    _active = true;
    eos_config_set_bool(_POWER_SAVE_CONFIG_KEY, true);

    /* 记录省电前的无线状态,然后同时关闭 WiFi + 蓝牙 */
    _wifi_was_enabled = eos_net_wifi_is_enabled();
    _bt_was_enabled = eos_net_bt_is_enabled();
    _radios_power_down();
    EOS_LOG_I("Power save: WiFi/BT disabled (was %d/%d)", (int)_wifi_was_enabled, (int)_bt_was_enabled);

    eos_display_set_brightness(_POWER_SAVE_BRIGHTNESS, _BRIGHTNESS_TRANS_MS, true);
    _power_profile_apply(true);
    /* 熄屏/L2/刷新收紧:5s 熄屏 + 10min 自动待机 + ~10FPS 刷新上限 */
    _idle_policy_apply(true);

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
    _power_profile_apply(false);
    /* 恢复熄屏超时(读最新持久 config)/L2 阈值/刷新周期 */
    _idle_policy_apply(false);

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
