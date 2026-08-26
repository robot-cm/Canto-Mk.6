/**
 * @file eos_dev_display_gc9a01.c
 * @brief GC9A01 LCD 驱动(Canto Mk.6 设备 HAL + LVGL 端口)— 裸 SPI master 实现
 *
 * 位置:port/esp32s3/main/(板级专属,非 Core)。Core HAL 接口在
 * src/devices/display/eos_dev_display.h(平台无关)。
 *
 * 移植背景(2026-08-23):
 *   原实现基于 ESP-IDF esp_lcd_panel_gc9a01 组件 + bit-bang 诊断,真机黑屏;
 *   而项目根目录 Canto Mk.6-main(上游)在真机显示正常。经对比:
 *     - 引脚定义完全相同(MOSI=9/SCLK=7/CS=2/DC=4/BL=43)
 *     - 差异在驱动实现:esp_lcd 组件 vs 裸 spi_master + 自研完整 init 序列
 *   故本文件整体重写为裸 spi_master 实现,逐字节照搬 Canto Mk.6-main 已验证代码:
 *     - 总线:SPI mode 0, 26MHz, 硬件 CS, 写入专用
 *     - init:完整 GC9A01 序列(软复位,0x11/0x29)
 *     - RST:不接硬件引脚。GPIO3 = SD CS,绝不能占用做 RST;
 *           GC9A01 走软件复位(Canto Mk.6-main 注释"uses software reset only")
 *     - 颜色字节序:配合 lv_conf.h 的 LV_COLOR_16_SWAP=1(GC9A01 大端 RGB565)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board_xiao_esp32s3_round.h"
#include "eos_dev_display_gc9a01.h"
#include "eos_dev_display.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define TAG "GC9A01"

/* ════════════════════════════════════════════════════════════════
 *  引脚/总线配置
 *  依据: ElenixOS-main/src/port/esp32/eos_port_esp32.h(真机验证显示)
 *  与 board_xiao_esp32s3_round.h 完全一致:
 *    MOSI=D10=GPIO9, SCLK=D8=GPIO7, CS=D1=GPIO2, DC=D3=GPIO4,
 *    BL=D6=GPIO43, MISO=D9=GPIO8(SD 共用,LCD 不读)
 *  RST: -1(软件复位)。GPIO3 是 SD CS,不能占用。
 * ════════════════════════════════════════════════════════════════ */
#define DISPLAY_HOST       BOARD_GC9A01_SPI_HOST   /* SPI3_HOST(与 SD 同总线) */
#define DISPLAY_PIN_MOSI   BOARD_GC9A01_MOSI_PIN
#define DISPLAY_PIN_MISO   BOARD_GC9A01_MISO_PIN
#define DISPLAY_PIN_SCLK   BOARD_GC9A01_SCK_PIN
#define DISPLAY_PIN_CS     BOARD_GC9A01_CS_PIN
#define DISPLAY_PIN_DC     BOARD_GC9A01_DC_PIN
#define DISPLAY_PIN_BL     BOARD_GC9A01_BL_PIN
#define DISPLAY_PIN_RST    (-1)                    /* GC9A01 仅软件复位 */

/* ElenixOS-main 真机验证的 SPI 参数 */
#define DISPLAY_SPI_CLK_HZ (26 * 1000 * 1000)
#define DISPLAY_SPI_MODE   0
#define DISPLAY_MAX_XFER   24000                   /* partial buffer 传输(实际最大 flush 19200B;
                                                       此前 48000 使 spi_master 占用 2×48KB 内部
                                                       DMA 缓冲,加剧内部 RAM 碎片化导致 SD 写失败) */

/* 背光 LEDC(ElenixOS-main 参数:5kHz, 10bit) */
#define BL_LEDC_TIMER     LEDC_TIMER_1
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BL_LEDC_RES       LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ   5000

static spi_device_handle_t s_spi;
static bool s_init_done = false;
static bool s_bl_gpio_hold = false;   /* bltest 诊断:BL 引脚被 GPIO 直驱(绕过 LEDC) */

/* ════════════════════════════════════════════════════════════════
 *  基本传输(ElenixOS-main 原样)
 *  DC 手动切换 + spi_device_transmit(硬件 CS)
 * ════════════════════════════════════════════════════════════════ */
static void display_send_cmd(uint8_t cmd)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = 8;
    t.tx_buffer = &cmd;
    gpio_set_level(DISPLAY_PIN_DC, 0);
    esp_err_t ret = spi_device_transmit(s_spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send_cmd 0x%02X failed: %s", cmd, esp_err_to_name(ret));
    }
}

static void display_send_data(const uint8_t *data, size_t len)
{
    spi_transaction_t t;
    if (len == 0) return;

    memset(&t, 0, sizeof(t));
    t.length    = len * 8;
    t.tx_buffer = data;
    gpio_set_level(DISPLAY_PIN_DC, 1);
    esp_err_t ret = spi_device_transmit(s_spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "send_data(%u B) failed: %s", (unsigned)len, esp_err_to_name(ret));
    }
}

static void display_send_data16(uint16_t data)
{
    uint8_t buf[2];
    buf[0] = (data >> 8) & 0xFF;
    buf[1] = data & 0xFF;
    display_send_data(buf, 2);
}

static void display_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    display_send_cmd(0x2A);
    display_send_data16(x1);
    display_send_data16(x2);
    display_send_cmd(0x2B);
    display_send_data16(y1);
    display_send_data16(y2);
    display_send_cmd(0x2C);
}

/* ════════════════════════════════════════════════════════════════
 *  GC9A01 初始化 — 完整序列,逐字节照搬 ElenixOS-main(ElenixOS-main
 *  注释:来自 lvgl_esp32_drivers,已在真机验证显示)
 * ════════════════════════════════════════════════════════════════ */
static void display_init(void)
{
    /* SPI 总线(write-only:MISO 归 SD 卡共用,LCD 不读) */
    spi_bus_config_t bus_conf = {
        .mosi_io_num   = DISPLAY_PIN_MOSI,
        .miso_io_num   = DISPLAY_PIN_MISO,
        .sclk_io_num   = DISPLAY_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_MAX_XFER,
    };
    esp_err_t ret = spi_bus_initialize(DISPLAY_HOST, &bus_conf, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(ret));
        return;
    }

    /* SPI 设备 — Mode 0, 26MHz(ElenixOS-main 稳定参数),硬件 CS */
    spi_device_interface_config_t dev_conf = {
        .clock_speed_hz = DISPLAY_SPI_CLK_HZ,
        .mode           = DISPLAY_SPI_MODE,
        .spics_io_num   = DISPLAY_PIN_CS,
        .queue_size     = 10,
        .pre_cb         = NULL,
    };

    /* DC 引脚 */
    gpio_config_t dc_conf = {
        .pin_bit_mask = (1ULL << DISPLAY_PIN_DC),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&dc_conf);

    ret = spi_bus_add_device(DISPLAY_HOST, &dev_conf, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "SPI ready: HOST=%d SCK=%d MOSI=%d CS=%d DC=%d @%dMHz mode%d",
             (int)DISPLAY_HOST, DISPLAY_PIN_SCLK, DISPLAY_PIN_MOSI,
             DISPLAY_PIN_CS, DISPLAY_PIN_DC, DISPLAY_SPI_CLK_HZ / 1000000,
             DISPLAY_SPI_MODE);

    /* ── 完整 GC9A01 初始化序列(ElenixOS-main 原样) ── */
    display_send_cmd(0xEF);
    display_send_cmd(0xEB);
    uint8_t eb_data[] = {0x14};
    display_send_data(eb_data, 1);

    display_send_cmd(0xFE);
    display_send_cmd(0xEF);
    display_send_cmd(0xEB);
    display_send_data(eb_data, 1);

    uint8_t data1[] = {0x40}; display_send_cmd(0x84); display_send_data(data1, 1);
    uint8_t data2[] = {0xFF}; display_send_cmd(0x85); display_send_data(data2, 1);
                              display_send_cmd(0x86); display_send_data(data2, 1);
                              display_send_cmd(0x87); display_send_data(data2, 1);
    uint8_t data3[] = {0x0A}; display_send_cmd(0x88); display_send_data(data3, 1);
    uint8_t data4[] = {0x21}; display_send_cmd(0x89); display_send_data(data4, 1);
    uint8_t data5[] = {0x00}; display_send_cmd(0x8A); display_send_data(data5, 1);
    uint8_t data6[] = {0x80}; display_send_cmd(0x8B); display_send_data(data6, 1);
    uint8_t data7[] = {0x01}; display_send_cmd(0x8C); display_send_data(data7, 1);
                              display_send_cmd(0x8D); display_send_data(data7, 1);
    uint8_t data8[] = {0xFF}; display_send_cmd(0x8E); display_send_data(data8, 1);
                              display_send_cmd(0x8F); display_send_data(data8, 1);

    /* LCD control */
    uint8_t b6_data[] = {0x00, 0x20};
    display_send_cmd(0xB6);
    display_send_data(b6_data, 2);

    /* Pixel format: 16-bit RGB565 */
    uint8_t pixfmt[] = {0x05};
    display_send_cmd(0x3A);
    display_send_data(pixfmt, 1);

    /* Brightness settings */
    uint8_t b90_data[] = {0x08, 0x08, 0x08, 0x08};
    display_send_cmd(0x90);
    display_send_data(b90_data, 4);

    uint8_t bd_data[] = {0x06}; display_send_cmd(0xBD); display_send_data(bd_data, 1);
    uint8_t bc_data[] = {0x00}; display_send_cmd(0xBC); display_send_data(bc_data, 1);
    uint8_t ff_data[] = {0x60, 0x01, 0x04}; display_send_cmd(0xFF); display_send_data(ff_data, 3);

    /* Power control */
    uint8_t c3_data[] = {0x13}; display_send_cmd(0xC3); display_send_data(c3_data, 1);
    uint8_t c4_data[] = {0x13}; display_send_cmd(0xC4); display_send_data(c4_data, 1);
    uint8_t c9_data[] = {0x22}; display_send_cmd(0xC9); display_send_data(c9_data, 1);
    uint8_t be_data[] = {0x11}; display_send_cmd(0xBE); display_send_data(be_data, 1);

    /* Timing */
    uint8_t e1_data[] = {0x10, 0x0E};
    display_send_cmd(0xE1);
    display_send_data(e1_data, 2);

    uint8_t df_data[] = {0x21, 0x0C, 0x02};
    display_send_cmd(0xDF);
    display_send_data(df_data, 3);

    /* Gamma/FRC settings */
    uint8_t f0_data[] = {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A};
    display_send_cmd(0xF0);
    display_send_data(f0_data, 6);

    uint8_t f1_data[] = {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F};
    display_send_cmd(0xF1);
    display_send_data(f1_data, 6);

    uint8_t f2_data[] = {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A};
    display_send_cmd(0xF2);
    display_send_data(f2_data, 6);

    uint8_t f3_data[] = {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F};
    display_send_cmd(0xF3);
    display_send_data(f3_data, 6);

    /* More settings */
    uint8_t ed_data[] = {0x1B, 0x0B};
    display_send_cmd(0xED);
    display_send_data(ed_data, 2);

    uint8_t ae_data[] = {0x77}; display_send_cmd(0xAE); display_send_data(ae_data, 1);
    uint8_t cd_data[] = {0x63}; display_send_cmd(0xCD); display_send_data(cd_data, 1);

    uint8_t s70_data[] = {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03};
    display_send_cmd(0x70);
    display_send_data(s70_data, 9);

    uint8_t e8_data[] = {0x34}; display_send_cmd(0xE8); display_send_data(e8_data, 1);

    /* Gamma correction */
    uint8_t g62_data[] = {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70};
    display_send_cmd(0x62);
    display_send_data(g62_data, 12);

    uint8_t g63_data[] = {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70};
    display_send_cmd(0x63);
    display_send_data(g63_data, 12);

    uint8_t g64_data[] = {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07};
    display_send_cmd(0x64);
    display_send_data(g64_data, 7);

    uint8_t g66_data[] = {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00};
    display_send_cmd(0x66);
    display_send_data(g66_data, 10);

    uint8_t g67_data[] = {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98};
    display_send_cmd(0x67);
    display_send_data(g67_data, 10);

    uint8_t g74_data[] = {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00};
    display_send_cmd(0x74);
    display_send_data(g74_data, 7);

    uint8_t g98_data[] = {0x3E, 0x07};
    display_send_cmd(0x98);
    display_send_data(g98_data, 2);

    /* Tearing Effect Line OFF */
    display_send_cmd(0x35);

    /* Memory Data Access Control - normal orientation
     * bit3(BGR)=1:控制器输出 BGR 顺序,补偿 Seeed GC9A01 面板硬件
     * 的 BGR 线序。否则红/蓝通道反转(动画天蓝段显示成黄、红段显
     * 示成蓝)。渲染/字节序均正确后仍错乱,唯一剩余原因是面板 RGB
     * 线序。若改为 0x00 恢复 RGB,则需在渲染端做 R/B 交换。 */
    display_send_cmd(0x36);
    uint8_t madctl[] = {0x08};
    display_send_data(madctl, 1);

    /* Display inversion ON (IPS panel) */
    display_send_cmd(0x21);

    /* Sleep Out */
    display_send_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Display ON */
    display_send_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 清屏黑,去除上电随机噪点(ElenixOS-main 原样:128px/块) */
    ESP_LOGI(TAG, "Clearing screen...");
    display_set_window(0, 0, BOARD_GC9A01_WIDTH - 1, BOARD_GC9A01_HEIGHT - 1);
    uint8_t black_chunk[256];
    memset(black_chunk, 0, sizeof(black_chunk));
    uint32_t total_pixels = BOARD_GC9A01_WIDTH * BOARD_GC9A01_HEIGHT;
    uint32_t sent = 0;
    while (sent < total_pixels) {
        uint32_t chunk_px = (total_pixels - sent > 128) ? 128 : (total_pixels - sent);
        display_send_data(black_chunk, chunk_px * 2);
        sent += chunk_px;
    }
    ESP_LOGI(TAG, "Screen cleared");

    s_init_done = true;
    ESP_LOGI(TAG, "GC9A01 display initialized (%dx%d, SPI %dMHz, mode%d, RST=sw-only)",
             BOARD_GC9A01_WIDTH, BOARD_GC9A01_HEIGHT,
             DISPLAY_SPI_CLK_HZ / 1000000, DISPLAY_SPI_MODE);
}

/* ════════════════════════════════════════════════════════════════
 *  背光 LEDC(ElenixOS-main 原样)
 * ════════════════════════════════════════════════════════════════ */
static void display_backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
    }

    ledc_channel_config_t chan_conf = {
        .gpio_num   = DISPLAY_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&chan_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Backlight initialized on GPIO %d (LEDC CH%d TIMER%d %dHz)",
             DISPLAY_PIN_BL, BL_LEDC_CHANNEL, BL_LEDC_TIMER, BL_LEDC_FREQ_HZ);
}

static void display_set_brightness(uint8_t brightness)
{
    if (s_bl_gpio_hold)
    {
        /* bltest(GPIO 直驱)之后首次设亮度:把 BL 引脚重新绑回 LEDC,恢复 PWM */
        ledc_channel_config_t rebind = {
            .gpio_num   = DISPLAY_PIN_BL,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = BL_LEDC_CHANNEL,
            .timer_sel  = BL_LEDC_TIMER,
            .duty       = 0,
            .hpoint     = 0,
        };
        if (ledc_channel_config(&rebind) == ESP_OK)
        {
            s_bl_gpio_hold = false;
            ESP_LOGI(TAG, "bltest: BL rebound to LEDC, PWM restored");
        }
        else
        {
            ESP_LOGE(TAG, "bltest: failed to rebind BL to LEDC");
        }
    }

    uint32_t duty = ((uint32_t)brightness * 1023) / 100;
    esp_err_t r1 = ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    esp_err_t r2 = ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
    if (r1 != ESP_OK || r2 != ESP_OK) {
        ESP_LOGE(TAG, "set_brightness(%u -> duty %lu) FAILED: %s / %s",
                 brightness, (unsigned long)duty,
                 esp_err_to_name(r1), esp_err_to_name(r2));
    } else {
        ESP_LOGD(TAG, "set_brightness(%u -> duty %lu) ok",
                 brightness, (unsigned long)duty);
    }
}

/* bltest 诊断:绕过 PWM,把背光引脚直接拉高/拉低。
 * 目的:不依赖任何仪表,验证 GPIO43(BL) 到背光的物理通路——
 *   引脚 HIGH 而屏幕不亮 => 通路断开/背光板异常;
 *   引脚 HIGH 屏幕亮、PWM 亮度不变 => 亮度调节入口问题(如 KE 开关)。 */
static void display_bltest(bool high)
{
    ledc_stop(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, 0);
    gpio_set_direction(DISPLAY_PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(DISPLAY_PIN_BL, high ? 1 : 0);
    s_bl_gpio_hold = true;
    ESP_LOGI(TAG, "bltest: BL(GPIO%d) -> %s direct drive, PWM stopped",
             DISPLAY_PIN_BL, high ? "HIGH" : "LOW");
}

static void display_power_on(void)
{
    display_set_brightness(100);
}

static void display_power_off(void)
{
    display_set_brightness(0);
}

/* ════════════════════════════════════════════════════════════════
 *  设备 HAL ops(注册进 eos_dev_display)
 * ════════════════════════════════════════════════════════════════ */
static const eos_dev_display_ops_t s_display_ops = {
    .set_brightness = display_set_brightness,
    .power_on       = display_power_on,
    .power_off      = display_power_off,
    .bltest         = display_bltest,
};

/* ════════════════════════════════════════════════════════════════
 *  LVGL flush 回调
 *
 *  GC9A01 期望大端 RGB565(SPI 先发高字节)。LVGL 渲染 buffer 始终是
 *  小端标准 RGB565(lv_color_to_u16/实心填充均不 swap),因此这里在
 *  拷贝到 internal DMA 缓冲后统一做字节交换再发送——不依赖
 *  LV_COLOR_16_SWAP 宏(该宏在 lv_refr.c 的生效路径不可控),保证
 *  纯色/图片/文字所有渲染路径字节序一致。
 *
 *  关键:SPI DMA 不认 PSRAM 地址。LVGL 渲染缓冲可能位于 PSRAM,
 *  直接作为 tx_buffer 会让 SPI master 驱动内部临时 malloc(internal
 *  DMA buffer)去 copy——internal 紧张时分配失败(ESP_ERR_NO_MEM)。
 *  因此这里先 memcpy 到 static internal DMA 缓冲再传输,稳定且零分配。
 * ════════════════════════════════════════════════════════════════ */
#define FLUSH_BUF_BYTES (BOARD_GC9A01_WIDTH * (BOARD_GC9A01_HEIGHT / 6) * 2)  /* 240*40*2 = 19200B */
DRAM_ATTR static uint8_t s_spi_flush_buf[FLUSH_BUF_BYTES];

/* 调试:开机后前若干次 flush 打印区域首像素的原始/交换后值,便于对照
 * 动画各阶段期望色(eos_boot_anim flash[] 日志)确认字节序修正生效 */
static int s_flush_dbg_cnt;

static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;
    size_t len = (size_t)w * h * 2;
    if (len > sizeof(s_spi_flush_buf)) len = sizeof(s_spi_flush_buf);
    display_set_window(area->x1, area->y1, area->x2, area->y2);
    memcpy(s_spi_flush_buf, px_map, len);

    /* 小端标准 RGB565 -> 大端发送序(GC9A01) */
    uint16_t *p = (uint16_t *)s_spi_flush_buf;
    for (size_t i = 0; i < len / 2; i++)
        p[i] = (uint16_t)((p[i] >> 8) | (p[i] << 8));

    /* 前 80 次 flush 打印首像素(覆盖主界面初始化 + 4s 开机动画 flash
     * 阶段)。flash[] 期望:天蓝 0x055F / 红 0xF96A / 黄 0xFEE0 / 绿 0x072E
     * (标准小端 u16)。px0 应为这些标准值,经 -> 交换后大端发送。 */
    if (s_flush_dbg_cnt < 80)
    {
        uint16_t raw;
        memcpy(&raw, px_map, 2);
        ESP_LOGI(TAG, "flush[%d] area=(%d,%d)-(%d,%d) px0=0x%04X -> 0x%04X",
                 s_flush_dbg_cnt, area->x1, area->y1, area->x2, area->y2,
                 (unsigned)raw, (unsigned)p[0]);
        s_flush_dbg_cnt++;
    }

    display_send_data(s_spi_flush_buf, len);
    lv_display_flush_ready(disp);
}

/* ════════════════════════════════════════════════════════════════
 *  对外接口(板级 main.c 调用)
 * ════════════════════════════════════════════════════════════════ */

/* GC9A01 驱动初始化:SPI bus + device + init 序列 + 清屏 + 背光 + 自检 */
esp_err_t eos_dev_display_gc9a01_init(void)
{
    ESP_LOGI(TAG, "Init: SCK=%d MOSI=%d CS=%d DC=%d BL=%d RST=none(sw)",
             DISPLAY_PIN_SCLK, DISPLAY_PIN_MOSI, DISPLAY_PIN_CS,
             DISPLAY_PIN_DC, DISPLAY_PIN_BL);

    display_init();
    if (!s_init_done) {
        ESP_LOGE(TAG, "Init failed (SPI bus/device)");
        return ESP_ERR_INVALID_STATE;
    }

    display_backlight_init();
    display_set_brightness(100);

    /* 注:曾有的红绿蓝黑自检闪烁已移除——开机颜色序列统一由
     * UI 层开机动画(eos_boot_anim)负责,避免驱动自检与系统动画叠加造成色序混乱 */
    return ESP_OK;
}

/* 注册到 ElenixOS 设备 HAL */
void eos_dev_display_gc9a01_register(void)
{
    eos_dev_display_register(&s_display_ops);
    ESP_LOGI(TAG, "Registered to device HAL (set_brightness/power_on/power_off)");
}

/* LVGL 显示初始化:lv_display_create + 双缓冲(PARTIAL) + flush_cb */
esp_err_t eos_dev_display_gc9a01_lvgl_init(void)
{
    lv_display_t *disp = lv_display_create(BOARD_GC9A01_WIDTH, BOARD_GC9A01_HEIGHT);
    if (!disp) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return ESP_ERR_NO_MEM;
    }

    /* 1/6 屏(40 行)双缓冲,PSRAM 优先(SPI DMA 兼容),internal DMA 兜底 */
    uint32_t buf_pixels = BOARD_GC9A01_WIDTH * (BOARD_GC9A01_HEIGHT / 6);
    uint32_t buf_bytes  = buf_pixels * sizeof(lv_color_t);

    /* 1/6 屏(40 行)双缓冲。S3 SPI2 的 IDMA 原生支持 PSRAM 地址,故优先
     * 纯 PSRAM;不要组合 MALLOC_CAP_DMA——无 CONFIG_SPIRAM_DMA_CAPABLE
     * 时 PSRAM 块不带 DMA cap,组合请求必失败回退 internal,57.6KB 占用
     * 会挤掉 ui_task 的 48KB internal 栈(largest<50KB → 创建失败,UI 全灭)。 */
    ESP_LOGI(TAG, "[FIX-DIAG] need=%lu SPIRAM free=%u largest=%u | DMA free=%u largest=%u",
             (unsigned long)buf_bytes,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

    /* [FIX-DIAG2] 全量堆信息 + 分配矩阵 */
    ESP_LOGI(TAG, "[FIX-DIAG2] ==== heap dump ====");
    heap_caps_dump_all();
    ESP_LOGI(TAG, "[FIX-DIAG2] ==== alloc matrix ====");
    {
        void *ptrs[8];
        int np = 0;
#define T2(sz, cap, nm) do { \
        void *p_ = heap_caps_malloc(sz, cap); \
        ESP_LOGI(TAG, "[FIX-DIAG2] %-20s %6luB -> %p", nm, (unsigned long)(sz), p_); \
        if (p_) ptrs[np++] = p_; \
    } while (0)
        T2(1, MALLOC_CAP_SPIRAM, "SPIRAM 1B");
        T2(16, MALLOC_CAP_SPIRAM, "SPIRAM 16B");
        T2(28800, MALLOC_CAP_SPIRAM, "SPIRAM 28800B");
        T2(28800, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, "SPIRAM|8BIT");
        T2(28800, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT, "SPIRAM|32BIT");
        T2(28800, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_32BIT, "SPIRAM|8|32");
        T2(28800, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM, "DEFAULT|SPIRAM");
        T2(28800, MALLOC_CAP_DEFAULT, "DEFAULT");
        for (int i = 0; i < np; i++) free(ptrs[i]);
    }
#undef T2
    lv_color_t *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!buf1) buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_32BIT);
    if (!buf1) buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    if (!buf1) buf1 = malloc(buf_bytes);
    if (!buf1) {
        ESP_LOGE(TAG, "buffer1 alloc failed (%lu B)", (unsigned long)buf_bytes);
        return ESP_ERR_NO_MEM;
    }
    lv_color_t *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!buf2) buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_32BIT);
    if (!buf2) buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    if (!buf2) buf2 = malloc(buf_bytes);
    if (!buf2) {
        ESP_LOGE(TAG, "buffer2 alloc failed (%lu B)", (unsigned long)buf_bytes);
        free(buf1);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "[FIX-DIAG] buf1=%p buf2=%p", (void *)buf1, (void *)buf2);
    ESP_LOGI(TAG, "LVGL buffers: %lu B x2 @%p/%p (PSRAM/DMA)",
             (unsigned long)buf_bytes, (void *)buf1, (void *)buf2);

    lv_display_set_buffers(disp, buf1, buf2, buf_pixels, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, display_flush_cb);
    lv_display_set_default(disp);

    ESP_LOGI(TAG, "LVGL display registered (%dx%d, partial %u px)",
             BOARD_GC9A01_WIDTH, BOARD_GC9A01_HEIGHT, (unsigned)buf_pixels);
    return ESP_OK;
}
