/**
 * @file cos_sim_hw_mock.c
 * @brief Simulator hardware mock — fake device OPS for time/battery/power/sensors.
 *
 * The real board-level drivers live in the ESP-IDF firmware and are not part
 * of this repository, so on the desktop simulator every device OPS table is
 * NULL and the dependent services fail (e.g. "Time device OPS not available",
 * "Battery device not valid"). This file registers plausible fakes so those
 * services run normally on the PC.
 *
 * Compiled ONLY when COS_SIMULATOR is defined.
 */

#include "cos_config.h" /* __has_include -> cos_platform_config.h -> COS_SIMULATOR */
#include "cos_sim_hw_mock.h"

#ifdef COS_SIMULATOR

#include "cos_log.h"
#include "cos_dev_time.h"
#include "cos_dev_battery.h"
#include "cos_dev_power.h"
#include "cos_dev_sensor.h"
#include "cos_service_battery.h"

#include <time.h>

#define COS_LOG_TAG "SimHW"

/* ------------------------------------------------------------------ */
/* Time — fall back to the host system clock.                          */
/* ------------------------------------------------------------------ */
static cos_datetime_t _mock_time_get_datetime(void)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);

    cos_datetime_t dt;
    dt.year   = (uint16_t)(lt->tm_year + 1900);
    dt.month  = (uint8_t)(lt->tm_mon + 1);
    dt.day    = (uint8_t)lt->tm_mday;
    dt.hour   = (uint8_t)lt->tm_hour;
    dt.min    = (uint8_t)lt->tm_min;
    dt.sec    = (uint8_t)lt->tm_sec;
    dt.ms     = 0;
    dt.day_of_week = (uint8_t)lt->tm_wday; /* 0 = Sunday */
    return dt;
}

static const cos_dev_time_ops_t _mock_time_ops = {
    .get_datetime = _mock_time_get_datetime,
};

/* ------------------------------------------------------------------ */
/* Battery — report a plausible fixed level (80%, charging).           */
/* ------------------------------------------------------------------ */
static void _mock_battery_request_update(void)
{
    cos_battery_raw_t raw;
    raw.percent    = 80;
    raw.voltage_mv = 3800;
    raw.current_ma = -120; /* negative = charging */
    raw.charging   = true;
    cos_battery_report_raw(&raw);
}

static const cos_battery_dev_ops_t _mock_battery_ops = {
    .request_update = _mock_battery_request_update,
};

/* ------------------------------------------------------------------ */
/* Power — accept any state change (no real PMIC on the sim).          */
/* ------------------------------------------------------------------ */
static int _mock_power_set(dev_power_state_t state)
{
    COS_LOG_D("set_power state=%d (no-op on simulator)", (int)state);
    return 0;
}

static const cos_dev_power_ops_t _mock_power_ops = {
    .set_power = _mock_power_set,
};

/* ------------------------------------------------------------------ */
/* Sensors — minimal pass-through OPS; no data is produced (no real    */
/* sensor), but registering them keeps the sensor service happy.       */
/* ------------------------------------------------------------------ */
static void _mock_sensor_init(cos_dev_sensor_t *dev)   { (void)dev; }
static void _mock_sensor_deinit(cos_dev_sensor_t *dev) { (void)dev; }
static void _mock_sensor_enable(cos_dev_sensor_t *dev) { (void)dev; }
static void _mock_sensor_disable(cos_dev_sensor_t *dev){ (void)dev; }
static void _mock_sensor_set_rate(cos_dev_sensor_t *dev, uint32_t hz) { (void)dev; (void)hz; }
static void _mock_sensor_get_rate(cos_dev_sensor_t *dev, uint32_t *hz) { if (hz) *hz = 25; (void)dev; }

static const cos_dev_sensor_ops_t _mock_sensor_ops = {
    .init               = _mock_sensor_init,
    .deinit             = _mock_sensor_deinit,
    .enable             = _mock_sensor_enable,
    .disable            = _mock_sensor_disable,
    .set_sample_rate    = _mock_sensor_set_rate,
    .get_sample_rate    = _mock_sensor_get_rate,
    .set_fifo_watermark = NULL, /* no hw FIFO */
    .flush_fifo         = NULL,
    .set_data_ready_cb  = NULL,
};

/* ------------------------------------------------------------------ */
/* Entry point — register everything.                                  */
/* ------------------------------------------------------------------ */
void cos_sim_hw_mock_init(void)
{
    cos_dev_time_register(&_mock_time_ops);
    cos_dev_battery_register(&_mock_battery_ops, 300);
    cos_dev_power_register(&_mock_power_ops);
    cos_dev_sensor_register("sim_acce", COS_SENSOR_TYPE_ACCE, &_mock_sensor_ops);
    cos_dev_sensor_register("sim_hr", COS_SENSOR_TYPE_HR, &_mock_sensor_ops);
    COS_LOG_I("simulator hardware mock registered (time/battery/power/sensor)");
}

#endif /* COS_SIMULATOR */
