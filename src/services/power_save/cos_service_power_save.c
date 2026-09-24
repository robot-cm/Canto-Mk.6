/**
 * @file cos_service_power_save.c
 * @brief Power save mode service (省电模式)
 *
 * 启用:
 *   1. 状态持久化(config key "power_save",重启后仍保持省电)
 *   2. 最低亮度 (5%,0=熄灭不可读)
 *   3. DFS(80-160MHz) + 自动 Light-sleep（保持触摸唤醒与运行上下文）
 *   4. 熄屏/待机收紧:无触摸 5s 熄屏 Light Sleep;熄屏累计 10min → L2 自动
 *      待机 Deep Sleep(运行时覆盖,退出恢复;不写持久 config)
 *   5. 限制屏幕刷新率(LVGL 刷新周期 20→100ms,退出恢复)
 *   6. 返回主界面 (cos_activity_back_to_watchface)
 *   7. 广播 COS_EVENT_POWER_SAVE_CHANGED
 * 禁用:
 *   1. 恢复亮度/频率/熄屏超时/L2 阈值/刷新周期
 *   2. 广播事件
 *
 * 页面锁定: watchface 手势回调均检查 cos_power_save_is_active(),
 *   只保留右滑打开 Control Center(可在其中关闭省电开关)。
 */

#include "cos_service_power_save.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "cos_log.h"
#include "cos_event.h"
#include "cos_service_config.h"
#include "cos_service_display.h"
#include "cos_service_pm.h"
#include "cos_activity.h"
#include "cos_net_wifi.h"
#include "cos_net_bt.h"
#include "cos_service_beast_mode.h"
#include "cos_service_cc_snapshot.h"
#include "cos_port.h"

#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
/* ESP-IDF v5.3:esp_cpu.h 无运行时调频 API(esp_cpu_update_freq 为 v5.4+),
 * 动态调频统一走 esp_pm_configure(需 CONFIG_PM_ENABLE=y,sdkconfig.defaults 已开) */
#include "sdkconfig.h"
#include "esp_pm.h"
#endif

#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
/* L2 自动待机阈值查询/设置接口:定义于 port/esp32s3/main/main.c(板级) */
extern void     cos_pm_set_l2_idle_ms(uint32_t idle_ms);
extern uint32_t cos_pm_get_l2_idle_ms(void);
#endif

/* Macros and Definitions -------------------------------------*/

#define _POWER_SAVE_CONFIG_KEY  "power_save"
#define _POWER_SAVE_BRIGHTNESS  5      /* 省电最低亮度 5%(0=背光熄灭不可读,5% 夜间可辨识) */
#define _BRIGHTNESS_TRANS_MS    300
#define _POWER_SAVE_CPU_MIN_MHZ 80
#define _POWER_SAVE_CPU_MAX_MHZ 160

/* 熄屏超时(5s)/L2 待机阈值(10min)/刷新周期(100ms)策略宏来自
 * cos_service_power_save.h(与 PM 服务初始对齐共用同一来源) */

/* Variables --------------------------------------------------*/

static bool _active = false;
static cos_event_code_t _power_save_event_id = COS_EVENT_LAST;
/* 进入省电前的无线状态:退出时恢复(省电期间同时关闭 WiFi + 蓝牙) */
static bool _wifi_was_enabled = false;
static bool _bt_was_enabled = false;

#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
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
        cos_pm_set_sleep_timeout(COS_POWER_SAVE_SLEEP_TIMEOUT_SEC);
#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
        _prev_l2_idle_ms = cos_pm_get_l2_idle_ms();
        if (_prev_l2_idle_ms != 0)
            cos_pm_set_l2_idle_ms(COS_POWER_SAVE_L2_IDLE_MS);
#endif
        /* 限制 LVGL 刷新率(~10 FPS),退出时复位默认周期 */
        cos_display_refresh_period_set(COS_POWER_SAVE_REFR_PERIOD_MS);
    }
    else
    {
        cos_pm_set_sleep_timeout(
            (uint32_t)cos_config_get_number(COS_CONFIG_KEY_SLEEP_TIMEOUT_SEC_NUMBER, 10));
#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
        if (_prev_l2_idle_ms != 0)
        {
            cos_pm_set_l2_idle_ms(_prev_l2_idle_ms);
            _prev_l2_idle_ms = 0;
        }
#endif
        cos_display_refresh_period_set(0); /* 复位系统默认刷新周期 */
    }
}

static void _radios_power_down(void)
{
    cos_net_wifi_set_enabled(false);
    cos_net_bt_set_enabled(false);
    cos_result_t bt_result = cos_net_bt_backend_power_down();
    if (bt_result != COS_OK)
        COS_LOG_W("BLE controller power-down failed: %d", (int)bt_result);
}

/* Function Implementations -----------------------------------*/

bool cos_power_save_is_active(void)
{
    return _active;
}

cos_event_code_t cos_power_save_get_event_id(void)
{
    return _power_save_event_id;
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
        COS_LOG_W("PM profile %s failed: %d", enabled ? "power-save" : "normal", (int)err);
    }
    else
    {
        COS_LOG_I("PM profile %s: CPU %d-%dMHz, auto light-sleep=%d",
                  enabled ? "power-save" : "normal", cfg.min_freq_mhz,
                  cfg.max_freq_mhz, (int)cfg.light_sleep_enable);
    }
#else
    (void)enabled;
#endif
}

cos_result_t cos_service_power_save_init(void)
{
    _power_save_event_id = cos_event_register_id();
    _active = cos_config_get_bool(_POWER_SAVE_CONFIG_KEY, false);

    if (_active)
    {
        cos_display_set_brightness(_POWER_SAVE_BRIGHTNESS, _BRIGHTNESS_TRANS_MS, true);
        _wifi_was_enabled = cos_net_wifi_is_enabled();
        _bt_was_enabled = cos_net_bt_is_enabled();
        _radios_power_down();
        _power_profile_apply(true);
        /* 收紧 L2/刷新周期并记录恢复值;熄屏 5s 超时此处设置会因 PM 服务
         * (晚于本服务 init)的 t 尚未创建而落空,已在 PM init 按其激活态对齐 */
        _idle_policy_apply(true);
    }
    COS_LOG_I("Power save init: %s", _active ? "ACTIVE" : "inactive");
    return COS_OK;
}

cos_result_t cos_power_save_enter(void)
{
    if (_active)
    {
        return COS_OK;
    }

    /* 互斥:开启省电模式前先退出性能模式 */
    if (cos_beast_mode_is_active())
    {
        COS_LOG_I("Power save: exiting beast mode first");
        cos_beast_mode_exit();
    }

    _active = true;
    cos_config_set_bool(_POWER_SAVE_CONFIG_KEY, true);

    /* 记录省电前的无线状态,然后同时关闭 WiFi + 蓝牙 */
    _wifi_was_enabled = cos_net_wifi_is_enabled();
    _bt_was_enabled = cos_net_bt_is_enabled();
    _radios_power_down();
    COS_LOG_I("Power save: WiFi/BT disabled (was %d/%d)", (int)_wifi_was_enabled, (int)_bt_was_enabled);

    cos_display_set_brightness(_POWER_SAVE_BRIGHTNESS, _BRIGHTNESS_TRANS_MS, true);
    _power_profile_apply(true);
    /* 熄屏/L2/刷新收紧:5s 熄屏 + 10min 自动待机 + ~10FPS 刷新上限 */
    _idle_policy_apply(true);

    /* 回到主界面(省电模式下只能停留在此) */
    cos_activity_back_to_watchface();

    /* 快照电源模式到 SD(/sdcard/history/cc)。无真 SD 卡时空操作,
     * 仍以 cfg.json 的 "power_save" 为准(见 cos_service_cc_snapshot.h)。 */
    cos_cc_snapshot_store_power(COS_CC_POWER_SAVE);

    if (_power_save_event_id != COS_EVENT_LAST)
    {
        cos_event_post(_power_save_event_id, NULL, NULL);
    }
    COS_LOG_I("Power save mode ENTERED");
    return COS_OK;
}

cos_result_t cos_power_save_exit(void)
{
    if (!_active)
    {
        return COS_OK;
    }
    _active = false;
    cos_config_set_bool(_POWER_SAVE_CONFIG_KEY, false);

    cos_display_restore(COS_DISPLAY_DURATION_MEDIUM);
    _power_profile_apply(false);
    /* 恢复熄屏超时(读最新持久 config)/L2 阈值/刷新周期 */
    _idle_policy_apply(false);

    /* 恢复省电前的无线状态 */
    cos_net_wifi_set_enabled(_wifi_was_enabled);
    cos_net_bt_set_enabled(_bt_was_enabled);
    COS_LOG_I("Power save: WiFi/BT restored to %d/%d", (int)_wifi_was_enabled, (int)_bt_was_enabled);

    /* 退出省电:模式回到智能档,无线状态一并刷新到快照(两者都被本次改动影响) */
    cos_cc_snapshot_store_power(COS_CC_POWER_SMART);
    if (cos_cc_snapshot_storage_available())
    {
        cos_cc_snapshot_store_wifi(_wifi_was_enabled);
        cos_cc_snapshot_store_bt(_bt_was_enabled);
    }

    if (_power_save_event_id != COS_EVENT_LAST)
    {
        cos_event_post(_power_save_event_id, NULL, NULL);
    }
    COS_LOG_I("Power save mode EXITED");
    return COS_OK;
}
