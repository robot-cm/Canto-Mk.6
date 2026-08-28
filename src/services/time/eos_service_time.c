/**
 * @file eos_service_time.c
 * @brief Time service (RTC calibration / query / set)
 *
 * 时间方案(支持 CR927 电池):
 *   - 正常 USB 供电:PCF8563 由 VCC 供电走时,VL=0,启动直接采信 RTC。
 *   - 断电(USB 拔出):VDD 掉电触发 VL 标志。
 *     * 有 CR927 电池:VBAT 维持 RTC 继续走时,重新上电后 RTC 时间最新,
 *       服务层比较 RTC 与 Flash 备份后采信 RTC,断电期间时间不丢失。
 *     * 无电池:RTC 停在断电时刻,采信 Flash 备份(最后有效时间)并写回。
 *   - 首次上电(无备份/出厂 RTC):编译时间兜底并写 RTC + 备份。
 *   - WiFi 连接成功后自动 SNTP 校时(eos_time_ntp_sync_start),
 *     时区偏移可配置(timezone_offset_min, 默认 UTC+8)。
 *   - eos_time_set() / eos_time_set_unix() 提供手动/网络(NTP)校时入口,
 *     同步写 RTC + Flash 备份 + libc 时间(settimeofday)。
 */

#include "eos_service_time.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lvgl.h"
#define EOS_LOG_TAG "ServiceTime"
#include "eos_log.h"
#include "eos_dev_time.h"
#include "eos_core.h"
#include "eos_service_config.h"
#include "eos_net_wifi.h"

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
#include <sys/time.h>
#include <time.h>
#include "esp_sntp.h"
#endif

/* Macros and Definitions -------------------------------------*/

/* RTC 年份可信下限:与固件编译年份相差超过该值视为出厂/未初始化残留(如 2000-01-01) */
#define _RTC_MIN_YEAR_OFFSET 2

/* SNTP 校时参数 */
#define _NTP_DEFAULT_SERVER      "ntp.aliyun.com"   /* 默认 NTP 服务器(可经 config ntp_server 覆盖) */
#define _NTP_MIN_INTERVAL_MS     (5u * 60u * 1000u) /* 两次校时间隔下限(WiFi 重连防抖) */
#define _NTP_RESYNC_INTERVAL_MS  (24u * 3600u * 1000u) /* 每日重校:抵消 RTC 晶振漂移累积 */
#define _NTP_POLL_PERIOD_MS      (60u * 1000u)      /* 周期检查间隔 */
#define _NTP_PLAUSIBLE_UNIX      1600000000u        /* 2020-09 之后才可信 */

#define _BACKUP_KEY_YEAR  "time.backup.year"
#define _BACKUP_KEY_MONTH "time.backup.month"
#define _BACKUP_KEY_DAY   "time.backup.day"
#define _BACKUP_KEY_HOUR  "time.backup.hour"
#define _BACKUP_KEY_MIN   "time.backup.min"
#define _BACKUP_KEY_SEC   "time.backup.sec"
#define _BACKUP_KEY_DOW   "time.backup.dow"

/* Variables --------------------------------------------------*/

static eos_datetime_t _last_valid;              /* 最后有效时间(读失败回退) */
static eos_time_source_t _source = EOS_TIME_SOURCE_NONE;

static void _ntp_poll_timer_create(void); /* 前向声明(init 先于定义调用) */

/* 内部工具:无时区依赖的儒略日换算(proleptic Gregorian) ---------- */

static uint32_t _days_from_civil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)(d - 1);
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (uint32_t)(era * 146097 + (int)doe - 719468);
}

static void _civil_from_days(uint32_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468u;
    unsigned era = (z >= 0 ? z : z - 146096u) / 146097u;
    unsigned doe = (unsigned)(z - era * 146097u);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int yy = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned mm = mp + (mp < 10u ? 3u : 9u);
    yy += (mm <= 2u) ? 1 : 0;
    *y = yy;
    *m = mm;
    *d = dd;
}

static uint32_t _dt_to_unix(const eos_datetime_t *dt)
{
    return _days_from_civil(dt->year, dt->month, dt->day) * 86400u
           + (uint32_t)dt->hour * 3600u
           + (uint32_t)dt->min * 60u
           + (uint32_t)dt->sec;
}

static void _unix_to_dt(uint32_t ts, eos_datetime_t *dt)
{
    uint32_t days = ts / 86400u;
    uint32_t rem  = ts % 86400u;
    int y;
    unsigned m, d;
    _civil_from_days(days, &y, &m, &d);
    dt->year        = (uint16_t)y;
    dt->month       = (uint8_t)m;
    dt->day         = (uint8_t)d;
    dt->hour        = (uint8_t)(rem / 3600u);
    dt->min         = (uint8_t)((rem % 3600u) / 60u);
    dt->sec         = (uint8_t)(rem % 60u);
    dt->ms          = 0;
    /* PCF8563 惯例:1=Mon..7=Sun;1970-01-01 为周四 */
    dt->day_of_week = (uint8_t)(((days % 7u) + 3u) % 7u + 1u);
}

/* 内部工具:有效性 / 备份 / 同步 ---------------------------------- */

static bool _is_valid(const eos_datetime_t *dt)
{
    if (dt == NULL) {
        return false;
    }
    if (dt->year < 2000 || dt->year > 2099) {
        return false;
    }
    if (dt->month < 1 || dt->month > 12) {
        return false;
    }
    if (dt->day < 1 || dt->day > 31) {
        return false;
    }
    if (dt->hour > 23 || dt->min > 59 || dt->sec > 59) {
        return false;
    }
    return true;
}

/** 固件编译年份(仅作 RTC 年份可信下限参照,不是校时源) */
static int _compile_year(void)
{
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char mon[8]  = {0};
    int  d       = 1;
    int  y       = 2000;
    if (sscanf(__DATE__, "%3s %d %d", mon, &d, &y) == 3) {
        for (int i = 0; i < 12; i++) {
            if (strncmp(mon, months[i], 3) == 0) {
                return y;
            }
        }
    }
    return 0;
}

/** RTC 可信度:范围校验之外,再排除出厂/未初始化残留值(典型 2000-01-01)。
 *  有 CR927 电池时 RTC 断电续走,年份应接近当前,必然通过本检查。 */
static bool _rtc_trustworthy(const eos_datetime_t *dt)
{
    if (!_is_valid(dt)) {
        return false;
    }
    int cy = _compile_year();
    if (cy > 0 && (int)dt->year + _RTC_MIN_YEAR_OFFSET < cy) {
        return false; /* 明显早于编译年份,视为出厂残留 */
    }
    return true;
}

static void _dev_set(const eos_datetime_t *dt)
{
    eos_dev_time_t *dev = eos_dev_time_get_instance();
    if (dev->ops != NULL && dev->ops->set_datetime != NULL) {
        dev->ops->set_datetime(*dt);
    }
}

static void _backup_save(const eos_datetime_t *dt)
{
    eos_config_set_number(_BACKUP_KEY_YEAR,  (double)dt->year);
    eos_config_set_number(_BACKUP_KEY_MONTH, (double)dt->month);
    eos_config_set_number(_BACKUP_KEY_DAY,   (double)dt->day);
    eos_config_set_number(_BACKUP_KEY_HOUR,  (double)dt->hour);
    eos_config_set_number(_BACKUP_KEY_MIN,   (double)dt->min);
    eos_config_set_number(_BACKUP_KEY_SEC,   (double)dt->sec);
    eos_config_set_number(_BACKUP_KEY_DOW,   (double)dt->day_of_week);
}

static eos_datetime_t _backup_load(void)
{
    eos_datetime_t dt;
    memset(&dt, 0, sizeof(dt));
    dt.year        = (uint16_t)eos_config_get_number(_BACKUP_KEY_YEAR, 0);
    dt.month       = (uint8_t)eos_config_get_number(_BACKUP_KEY_MONTH, 0);
    dt.day         = (uint8_t)eos_config_get_number(_BACKUP_KEY_DAY, 0);
    dt.hour        = (uint8_t)eos_config_get_number(_BACKUP_KEY_HOUR, 0);
    dt.min         = (uint8_t)eos_config_get_number(_BACKUP_KEY_MIN, 0);
    dt.sec         = (uint8_t)eos_config_get_number(_BACKUP_KEY_SEC, 0);
    dt.day_of_week = (uint8_t)eos_config_get_number(_BACKUP_KEY_DOW, 0);
    return dt;
}

static void _sync_libc(const eos_datetime_t *dt)
{
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
    struct timeval tv;
    tv.tv_sec  = (time_t)_dt_to_unix(dt);
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
#else
    (void)dt;
#endif
}

/** 编译时间兜底(首次上电且无任何有效时间源) */
static eos_datetime_t _compile_time(void)
{
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    eos_datetime_t dt;
    memset(&dt, 0, sizeof(dt));

    char mon[8] = {0};
    int d = 1, y = 2000, h = 0, m = 0, s = 0;
    if (sscanf(__DATE__, "%3s %d %d", mon, &d, &y) == 3 &&
        sscanf(__TIME__, "%d:%d:%d", &h, &m, &s) >= 1) {
        for (int i = 0; i < 12; i++) {
            if (strncmp(mon, months[i], 3) == 0) {
                dt.month = (uint8_t)(i + 1);
                break;
            }
        }
        dt.year  = (uint16_t)y;
        dt.day   = (uint8_t)d;
        dt.hour  = (uint8_t)h;
        dt.min   = (uint8_t)m;
        dt.sec   = (uint8_t)s;
        dt.ms    = 0;
        dt.day_of_week = (uint8_t)(((_dt_to_unix(&dt) / 86400u) % 7u + 3u) % 7u + 1u);
    }
    return dt;
}

/* Function Implementations -----------------------------------*/

eos_result_t eos_service_time_init(void)
{
    eos_dev_time_t *dev = eos_dev_time_get_instance();
    if (dev->ops == NULL || dev->ops->get_datetime == NULL) {
        EOS_LOG_E("Time device not available, time uncalibrated");
        _source = EOS_TIME_SOURCE_NONE;
        return EOS_ERR_DEV_OPS_NOT_SUPPORTED;
    }

    eos_datetime_t rtc    = dev->ops->get_datetime();
    eos_datetime_t backup = _backup_load();
    bool rtc_ok    = _rtc_trustworthy(&rtc);
    bool backup_ok = _is_valid(&backup);

    eos_datetime_t chosen;
    memset(&chosen, 0, sizeof(chosen));

    if (rtc_ok && backup_ok) {
        int64_t diff = (int64_t)_dt_to_unix(&rtc) - (int64_t)_dt_to_unix(&backup);
        if (diff >= 0) {
            /* RTC 不落后备份:正常续走(含 CR927 电池维持的断电走时)或与备份一致,
             * 采信 RTC。电池期间 RTC 领先备份越多越说明它在正确走时。 */
            chosen = rtc;
            _source = EOS_TIME_SOURCE_RTC;
        } else {
            /* RTC 落后备份:无电池断电停走残留,采信备份并写回恢复走时 */
            EOS_LOG_W("RTC behind backup (%llds), restore backup and rewrite RTC",
                      (long long)(-diff));
            chosen = backup;
            _dev_set(&chosen);
            _source = EOS_TIME_SOURCE_BACKUP;
        }
    } else if (rtc_ok) {
        chosen = rtc;
        _source = EOS_TIME_SOURCE_RTC;
        _backup_save(&chosen);
    } else if (backup_ok) {
        /* RTC 不可信(出厂残留/失效):恢复备份并写回 RTC,恢复走时 */
        EOS_LOG_W("RTC invalid, restore backup and rewrite RTC");
        chosen = backup;
        _dev_set(&chosen);
        _source = EOS_TIME_SOURCE_BACKUP;
    } else {
        /* 初次上电(无备份 + 出厂 RTC):编译时间兜底 */
        EOS_LOG_W("No valid time source, use build time (uncalibrated)");
        chosen = _compile_time();
        _dev_set(&chosen);
        _backup_save(&chosen);
        _source = EOS_TIME_SOURCE_COMPILE;
    }

    _last_valid = chosen;
    _sync_libc(&chosen);
    EOS_LOG_I("System time: %04d-%02d-%02d %02d:%02d:%02d (source=%d)",
              chosen.year, chosen.month, chosen.day,
              chosen.hour, chosen.min, chosen.sec, (int)_source);
    _ntp_poll_timer_create();
    return EOS_OK;
}

eos_datetime_t eos_time_get(void)
{
    static eos_datetime_t last_sec_time;
    static uint32_t sec_base_tick = 0;
    static uint8_t initialized = 0;

    eos_dev_time_t *dev = eos_dev_time_get_instance();
    if (dev->ops == NULL || dev->ops->get_datetime == NULL) {
        EOS_LOG_E("Time device OPS not available");
        eos_datetime_t dt = _last_valid;
        dt.ms = 0;
        return dt;
    }

    eos_datetime_t now = dev->ops->get_datetime();
    uint32_t tick = eos_tick_get();

    if (!_is_valid(&now)) {
        /* RTC 暂不可读:回退最后有效时间(秒不推进,ms 由 tick 补偿) */
        if (_last_valid.year == 0) {
            eos_datetime_t zero;
            memset(&zero, 0, sizeof(zero));
            return zero;
        }
        now = _last_valid;
        uint32_t ms = tick - sec_base_tick;
        now.ms = (uint16_t)((ms >= 1000) ? 999 : ms);
        return now;
    }

    if (!initialized || now.sec != last_sec_time.sec || now.min != last_sec_time.min
        || now.hour != last_sec_time.hour || now.day != last_sec_time.day
        || now.month != last_sec_time.month || now.year != last_sec_time.year) {
        sec_base_tick = tick;
        last_sec_time = now;
        initialized = 1;
    }

    uint32_t ms = tick - sec_base_tick;
    if (ms >= 1000) {
        ms = 999;
    }
    now.ms = (uint16_t)ms;
    _last_valid = now;
    return now;
}

eos_result_t eos_time_set(eos_datetime_t dt)
{
    if (!_is_valid(&dt)) {
        EOS_LOG_E("set: invalid datetime (%u/%u/%u %u:%u:%u)",
                  dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
        return EOS_ERR_INVALID_ARG;
    }

    if (dt.day_of_week == 0) {
        dt.day_of_week = (uint8_t)(((_dt_to_unix(&dt) / 86400u) % 7u + 3u) % 7u + 1u);
    }

    _dev_set(&dt);
    _backup_save(&dt);
    _last_valid = dt;
    _sync_libc(&dt);
    _source = EOS_TIME_SOURCE_RTC; /* 显式校时后视为已校准 */
    EOS_LOG_I("Time set: %04d-%02d-%02d %02d:%02d:%02d",
              dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
    return EOS_OK;
}

eos_result_t eos_time_set_unix(uint32_t ts)
{
    eos_datetime_t dt;
    _unix_to_dt(ts, &dt);
    return eos_time_set(dt);
}

eos_time_source_t eos_time_get_source(void)
{
    return _source;
}

eos_datetime_t eos_time_get_rtc(void)
{
    eos_dev_time_t *dev = eos_dev_time_get_instance();
    if (dev->ops == NULL || dev->ops->get_datetime == NULL) {
        eos_datetime_t zero;
        memset(&zero, 0, sizeof(zero));
        return zero;
    }
    return dev->ops->get_datetime();
}

/* ---- SNTP 校时(联网后自动触发) ----------------------------------- */

static volatile bool _ntp_synced = false;
static uint32_t _ntp_synced_tick = 0;

static void _ntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    if (_ntp_synced) {
        return;
    }
    time_t utc = time(NULL);
    if (utc < (time_t)_NTP_PLAUSIBLE_UNIX) {
        EOS_LOG_W("NTP got implausible time, ignore");
        return;
    }
    int32_t tz_min = (int32_t)eos_config_get_number(EOS_CONFIG_KEY_TIMEZONE_OFFSET_MIN_NUMBER, 480);
    eos_time_set_unix((uint32_t)(utc + tz_min * 60));
    _ntp_synced      = true;
    _ntp_synced_tick = eos_tick_get();
    esp_sntp_stop(); /* 一次校时即可,后续由 RTC(或 CR927 电池)维持走时 */
    EOS_LOG_I("NTP time synced (UTC %ld, tz %+d min)", (long)utc, (int)tz_min);
}

void eos_time_ntp_sync_start(void)
{
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
    if (_ntp_synced) {
        /* 已同步过:间隔内不重复(WiFi 反复连接防抖) */
        if ((uint32_t)(eos_tick_get() - _ntp_synced_tick) < _NTP_MIN_INTERVAL_MS) {
            return;
        }
        _ntp_synced = false;
    }
    if (esp_sntp_enabled()) {
        return; /* 已在同步中 */
    }
    /* NTP 服务器可经 config(ntp_server)覆盖;static 缓冲保证指针在 esp_sntp 生命周期内有效 */
    static char _ntp_server[64];
    const char *s = eos_config_get_string(EOS_CONFIG_KEY_NTP_SERVER_STR, _NTP_DEFAULT_SERVER);
    snprintf(_ntp_server, sizeof(_ntp_server), "%s", (s && s[0]) ? s : _NTP_DEFAULT_SERVER);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, _ntp_server);
    esp_sntp_set_time_sync_notification_cb(_ntp_sync_cb);
    esp_sntp_init();
    EOS_LOG_I("NTP sync started (%s)", _ntp_server);
#else
    EOS_LOG_D("NTP sync skipped (simulator)");
#endif
}

void eos_time_ntp_force_sync(void)
{
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
    if (eos_net_wifi_state() != EOS_WIFI_CONNECTED) {
        EOS_LOG_W("NTP force sync skipped: WiFi not connected");
        return;
    }
    _ntp_synced = false; /* 绕过 5 分钟防抖,立即重新发起校时 */
    eos_time_ntp_sync_start();
#else
    EOS_LOG_D("NTP force sync skipped (simulator)");
#endif
}

void eos_time_ntp_poll(void)
{
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
    if (!_ntp_synced) {
        return; /* 从未成功同步:由 WiFi 连接事件负责首次校时 */
    }
    if (eos_net_wifi_state() != EOS_WIFI_CONNECTED) {
        return;
    }
    if ((uint32_t)(eos_tick_get() - _ntp_synced_tick) < _NTP_RESYNC_INTERVAL_MS) {
        return;
    }
    /* 距离上次成功校时已超 24h:重新 SNTP,抵消 RTC 走时漂移累积 */
    EOS_LOG_I("NTP daily re-sync (RTC drift compensation)");
    _ntp_synced = false;
    eos_time_ntp_sync_start();
#endif
}

#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
static void _ntp_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    eos_time_ntp_poll();
}
#endif

static void _ntp_poll_timer_create(void)
{
#if !defined(EOS_SIMULATOR) || EOS_SIMULATOR == 0
    lv_timer_t *t = lv_timer_create(_ntp_poll_timer_cb, _NTP_POLL_PERIOD_MS, NULL);
    if (t == NULL) {
        EOS_LOG_W("NTP poll timer create failed");
    }
#else
    (void)0;
#endif
}
