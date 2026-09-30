/**
 * @file eos_service_time.h
 * @brief Time service (RTC calibration / query / set)
 */

#ifndef EOS_SERVICE_TIME_H
#define EOS_SERVICE_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "eos_dev_time.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/** 当前系统时间来源(启动校对结果) */
typedef enum
{
    EOS_TIME_SOURCE_NONE = 0,   /**< 未知/初始化失败 */
    EOS_TIME_SOURCE_RTC,        /**< 来自 PCF8563/BM8563 硬件 RTC */
    EOS_TIME_SOURCE_BACKUP,     /**< 来自 Flash 备份(上次有效时间) */
    EOS_TIME_SOURCE_COMPILE,    /**< 编译时间兜底(未校准) */
} eos_time_source_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize time service (startup calibration strategy)
 *
 * 校对优先级(支持 CR927 电池):
 *   1. 硬件 RTC:范围/年份可信校验通过后,与 Flash 备份比较——
 *      RTC 不落后备份则采信(含电池断电续走,VL=1 也可信);
 *      RTC 落后备份(无电池停走残留)则采用备份并写回 RTC。
 *   2. Flash 备份:上次有效时间(无电池掉电后 RTC 失效),采用并写回 RTC 恢复走时。
 *   3. 编译时间:首次上电兜底,写 RTC + 备份,标记未校准。
 * 完成后同步 libc 时间(settimeofday),shell/JS/日志时间戳随之正确。
 *
 * @return EOS_OK if successful, error code otherwise
 */
eos_result_t eos_service_time_init(void);

/**
 * @brief Start SNTP time sync (call after WiFi connected)
 *
 * 通过 SNTP 获取 UTC 时间,叠加时区偏移(config: timezone_offset_min,
 * 默认 480 = UTC+8)后调用 eos_time_set_unix 校准系统时间。
 * 同步成功后自动停止 SNTP;同一 WiFi 会话 5 分钟内防抖,
 * WiFi 断开重连超过间隔后允许再次校时。
 *
 * @note Simulator 下为空操作。
 */
void eos_time_ntp_sync_start(void);

/**
 * @brief Force an SNTP re-sync right now (bypasses the 5-minute debounce)
 *
 * 用于 shell `time ntp` 手动校时或用户主动触发。Wi-Fi 未连接时仅告警。
 */
void eos_time_ntp_force_sync(void);

/**
 * @brief Periodic housekeeping for time keeping
 *
 * 只要 Wi-Fi 保持连接,每 24h 自动重校一次 SNTP,抵消 RTC 走时漂移
 * (BM8563 晶振漂移会随时间累积)。由时间服务内部的 lv_timer 周期调用。
 */
void eos_time_ntp_poll(void);

/**
 * @brief Get system time
 * @return Current datetime with millisecond precision
 */
eos_datetime_t eos_time_get(void);

/**
 * @brief Set system time (write RTC + Flash backup + libc time)
 * @param dt Datetime to set (wall-clock)
 * @return EOS_OK on success
 */
eos_result_t eos_time_set(eos_datetime_t dt);

/**
 * @brief Set system time from UNIX timestamp (for NTP sync)
 * @param ts UNIX seconds since 1970-01-01 00:00:00 UTC
 * @return EOS_OK on success
 */
eos_result_t eos_time_set_unix(uint32_t ts);

/**
 * @brief Get current time source (calibration result)
 * @return Time source enum
 */
eos_time_source_t eos_time_get_source(void);

/**
 * @brief Get device RTC raw time without calibration
 * @return RTC time (zero struct if invalid/unavailable)
 */
eos_datetime_t eos_time_get_rtc(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SERVICE_TIME_H */
