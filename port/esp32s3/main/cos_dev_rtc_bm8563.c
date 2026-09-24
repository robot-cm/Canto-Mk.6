/**
 * @file cos_dev_rtc_bm8563.c
 * @brief RTC BM8563 (PCF8563 compatible) I2C driver (XIAO Round Display)
 *
 * PCF8563 / BM8563 寄存器兼容(I2C 7bit 地址 0x51)。
 *   - 0x00 Control/Status1 (bit4 STOP: 1=停钟)
 *   - 0x01 Control/Status2
 *   - 0x02 seconds  (bit7 VL: 1=时钟无效/掉过电)
 *   - 0x03 minutes  (BCD)
 *   - 0x04 hours    (bit5 AMPM, 24h 模式该位为 0, bit4-0 BCD)
 *   - 0x05 days     (bit5-0 BCD)
 *   - 0x06 weekdays (bit2-0: 1=Mon..7=Sun)
 *   - 0x07 months   (bit7 century, bit4-0 BCD)
 *   - 0x08 years    (BCD, 00-99 -> 2000-2099)
 *
 * 写入策略:置 STOP 停钟 -> 写时间寄存器(秒寄存器 bit7=0 清 VL) -> 清 STOP 恢复走时。
 */

#include "cos_dev_rtc_bm8563.h"

#include <string.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "board_xiao_esp32s3_round.h"

static const char *TAG = "RTC8563";

/* PCF8563 寄存器地址 */
#define RTC_REG_CTRL1      0x00
#define RTC_REG_CTRL2      0x01
#define RTC_REG_SECONDS    0x02
#define RTC_REG_MINUTES    0x03
#define RTC_REG_HOURS      0x04
#define RTC_REG_DAYS       0x05
#define RTC_REG_WEEKDAYS   0x06
#define RTC_REG_MONTHS     0x07
#define RTC_REG_YEARS      0x08

#define RTC_CTRL1_STOP     0x10u
#define RTC_SECONDS_VL     0x80u
#define RTC_I2C_TIMEOUT_MS 30

/* ---------------------------------------------------------------- */

static uint8_t _dec_to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static uint8_t _bcd_to_dec(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

/* 校验 BCD 字节合法(高低半字节均 <= 9) */
static bool _bcd_valid(uint8_t v)
{
    return (((v >> 4) & 0x0F) <= 9) && ((v & 0x0F) <= 9);
}

/* ---------------------------------------------------------------- */

static esp_err_t _rtc_write_reg(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(BOARD_RTC_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    if (len > 0) {
        i2c_master_write(cmd, data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BOARD_I2C_HOST, cmd, pdMS_TO_TICKS(RTC_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t _rtc_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(BOARD_RTC_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(BOARD_RTC_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, &data[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BOARD_I2C_HOST, cmd, pdMS_TO_TICKS(RTC_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ---------------------------------------------------------------- */

static cos_datetime_t _bm8563_get_datetime(void)
{
    cos_datetime_t dt = {0};
    uint8_t raw[7] = {0};

    if (_rtc_read_regs(RTC_REG_SECONDS, raw, sizeof(raw)) != ESP_OK) {
        ESP_LOGW(TAG, "I2C read failed");
        return dt; /* 全 0 -> 上层判定无效,走备份时间 */
    }

    /* 时钟有效标志 VL:1 表示掉过电。注意:插入 CR927 电池后,掉电期间 RTC 由
     * VBAT 维持继续走时,VL 置位但时间仍然有效,不能直接判无效;
     * 是否采信由上层与 Flash 备份比较后决定(首次发现时提示一次)。 */
    static bool _vl_warned = false;
    if (raw[0] & RTC_SECONDS_VL) {
        if (!_vl_warned) {
            ESP_LOGW(TAG, "RTC VL=1 (power loss occurred); time may be battery-backed");
            _vl_warned = true;
        }
    }

    uint8_t sec  = _bcd_to_dec(raw[0] & 0x7F);
    uint8_t min  = _bcd_to_dec(raw[1] & 0x7F);
    uint8_t hour = _bcd_to_dec(raw[2] & 0x3F);
    uint8_t day  = _bcd_to_dec(raw[3] & 0x3F);
    uint8_t dow  = raw[4] & 0x07;
    uint8_t mon  = _bcd_to_dec(raw[5] & 0x1F);
    uint8_t year = _bcd_to_dec(raw[6]);

    /* BCD/范围合法性校验(I2C 访问期间时钟进位可能造成坏值) */
    if (!_bcd_valid(raw[0] & 0x7F) || !_bcd_valid(raw[1] & 0x7F) ||
        !_bcd_valid(raw[2] & 0x3F) || !_bcd_valid(raw[3] & 0x3F) ||
        !_bcd_valid(raw[5] & 0x1F) || !_bcd_valid(raw[6]) ||
        sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 ||
        mon < 1 || mon > 12 || year > 99) {
        ESP_LOGW(TAG, "RTC invalid BCD/range (read glitch)");
        return dt;
    }

    dt.year        = 2000 + year;
    dt.month       = mon;
    dt.day         = day;
    dt.hour        = hour;
    dt.min         = min;
    dt.sec         = sec;
    dt.ms          = 0;
    dt.day_of_week = dow; /* 1=Mon..7=Sun */
    return dt;
}

static cos_result_t _bm8563_set_datetime(cos_datetime_t dt)
{
    if (dt.year < 2000 || dt.year > 2099 || dt.month < 1 || dt.month > 12 ||
        dt.day < 1 || dt.day > 31 || dt.hour > 23 || dt.min > 59 || dt.sec > 59) {
        ESP_LOGE(TAG, "set: invalid datetime");
        return COS_ERR_INVALID_ARG;
    }

    uint8_t ctrl1 = 0;
    if (_rtc_read_regs(RTC_REG_CTRL1, &ctrl1, 1) != ESP_OK) {
        ESP_LOGE(TAG, "set: read ctrl1 failed");
        return COS_ERR_IO;
    }

    /* 1. 停钟,避免写入期间秒进位 */
    ctrl1 |= RTC_CTRL1_STOP;
    if (_rtc_write_reg(RTC_REG_CTRL1, &ctrl1, 1) != ESP_OK) {
        ESP_LOGE(TAG, "set: stop clock failed");
        return COS_ERR_IO;
    }

    /* 2. 写时间寄存器(0x02..0x08),秒寄存器 bit7=0 清除 VL */
    uint8_t raw[7];
    raw[0] = _dec_to_bcd(dt.sec) & 0x7F;          /* VL=0 */
    raw[1] = _dec_to_bcd(dt.min) & 0x7F;
    raw[2] = _dec_to_bcd(dt.hour) & 0x3F;         /* 24h 模式 */
    raw[3] = _dec_to_bcd(dt.day) & 0x3F;
    raw[4] = (dt.day_of_week > 0 && dt.day_of_week <= 7) ? (dt.day_of_week & 0x07) : 0;
    raw[5] = _dec_to_bcd(dt.month) & 0x1F;
    raw[6] = _dec_to_bcd(dt.year % 100);
    if (_rtc_write_reg(RTC_REG_SECONDS, raw, sizeof(raw)) != ESP_OK) {
        ESP_LOGE(TAG, "set: write time failed");
        return COS_ERR_IO;
    }

    /* 3. 清中断/定时器标志位 */
    uint8_t ctrl2 = 0x00;
    _rtc_write_reg(RTC_REG_CTRL2, &ctrl2, 1);

    /* 4. 启动时钟 */
    ctrl1 &= ~RTC_CTRL1_STOP;
    if (_rtc_write_reg(RTC_REG_CTRL1, &ctrl1, 1) != ESP_OK) {
        ESP_LOGE(TAG, "set: start clock failed");
        return COS_ERR_IO;
    }

    ESP_LOGI(TAG, "RTC set: %04d-%02d-%02d %02d:%02d:%02d",
             dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
    return COS_OK;
}

/* ---------------------------------------------------------------- */

static const cos_dev_time_ops_t _bm8563_ops = {
    .get_datetime = _bm8563_get_datetime,
    .set_datetime = _bm8563_set_datetime,
};

cos_result_t cos_dev_rtc_bm8563_init(void)
{
    uint8_t probe = 0;
    if (_rtc_read_regs(RTC_REG_CTRL1, &probe, 1) != ESP_OK) {
        ESP_LOGW(TAG, "BM8563 not responding on I2C (0x%02X)", BOARD_RTC_I2C_ADDR);
        return COS_ERR_IO;
    }
    ESP_LOGI(TAG, "BM8563/PCF8563 detected at 0x%02X, ctrl1=0x%02X",
             BOARD_RTC_I2C_ADDR, probe);
    return cos_dev_time_register(&_bm8563_ops);
}
