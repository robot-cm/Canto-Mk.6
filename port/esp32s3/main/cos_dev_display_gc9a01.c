/**
 * @file cos_dev_display_gc9a01.c
 * @brief GC9A01 LCD 驱动(Canto Mk.6 设备 HAL + LVGL 端口)— 裸 SPI master 实现
 *
 * 位置:port/esp32s3/main/(板级专属,非 Core)。Core HAL 接口在
 * src/devices/display/cos_dev_display.h(平台无关)。
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
#include "cos_dev_display_gc9a01.h"
#include "cos_dev_display.h"

#include <string.h>
#include <stddef.h>   /* offsetof(flush_containing) */
#include <stdio.h>    /* printf:bltest 诊断直出(绕过日志过滤) */
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"   /* GPIO 外设寄存器直读(诊断) */
#include "soc/gpio_sig_map.h"  /* SIG_GPIO_OUT_IDX */
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define TAG "GC9A01"

/* ════════════════════════════════════════════════════════════════
 *  引脚/总线配置
 *  依据: CantoMk6-main/src/port/esp32/cos_port_esp32.h(真机验证显示)
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

/* CantoMk6-main 真机验证的 SPI 参数 */
#define DISPLAY_SPI_CLK_HZ (26 * 1000 * 1000)
#define DISPLAY_SPI_MODE   0
#define DISPLAY_MAX_XFER   24000                   /* partial buffer 传输(实际最大 flush 19200B;
                                                       此前 48000 使 spi_master 占用 2×48KB 内部
                                                       DMA 缓冲,加剧内部 RAM 碎片化导致 SD 写失败) */

/* 背光 LEDC(5kHz, 10bit)
 * 对齐 espp/seeed-studio-round-display 的官方验证配置:
 *   - TIMER_0(espp 用 TIMER_0;避免任何隐藏的 timer 占用)
 *   - LEDC_USE_APB_CLK 显式锁定 80MHz 时钟:ESP-IDF v5.3 +
 *     CONFIG_PM_ENABLE 下 LEDC_AUTO_CLK 会选 RC_FAST_CLK(20MHz),
 *     受 esp_pm 锁管理,配置/运行易失败(现象:背光常亮、亮度调节无效)
 *   - 显式 flags.output_invert = 0(高电平点亮,与 espp backlight_value=true 一致) */
#define BL_LEDC_TIMER     LEDC_TIMER_0
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BL_LEDC_RES       LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ   5000
#define BL_LEDC_CLK       LEDC_USE_APB_CLK

static spi_device_handle_t s_spi;
static bool s_init_done = false;
static bool s_bl_gpio_hold = false;   /* bltest 诊断:BL 引脚被 GPIO 直驱(绕过 LEDC) */
static uint8_t s_last_brightness = 100; /* 最近一次用户设置的亮度;power_on 据此恢复 */

/* SPI 设备级回调(见 flush_pre_cb)与 flush 同步回调 */
static void flush_pre_cb(spi_transaction_t *t);
static void display_flush_wait_cb(lv_display_t *disp);
static void display_flush_tx_init(void);

/* ════════════════════════════════════════════════════════════════
 *  基本传输(CantoMk6-main 原样)
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
 *  GC9A01 初始化 — 完整序列,逐字节照搬 CantoMk6-main(CantoMk6-main
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

    /* SPI 设备 — Mode 0, 26MHz(CantoMk6-main 稳定参数),硬件 CS。
     * queue_size=16:每帧 6 个事务(2 窗口命令 + RAMWR + 像素等),双 slot
     * 峰值约 12 个在途,留余量。
     * pre_cb 是设备级(每个事务都触发,ISR 上下文):按 flush_trans_t 描述符
     * 的 dc_level 切换 DC 引脚。不配 post_cb——flush 完成同步由 LVGL 的
     * flush_wait_cb(任务上下文)用 get_trans_result 取回,见 display_flush_wait_cb。 */
    spi_device_interface_config_t dev_conf = {
        .clock_speed_hz = DISPLAY_SPI_CLK_HZ,
        .mode           = DISPLAY_SPI_MODE,
        .spics_io_num   = DISPLAY_PIN_CS,
        .queue_size     = 16,
        .pre_cb         = flush_pre_cb,
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

    /* 初始化异步 flush 事务组(命令缓冲/DC pre_cb/长度) */
    display_flush_tx_init();

    /* ── 完整 GC9A01 初始化序列(CantoMk6-main 原样) ── */
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

    /* 清屏黑,去除上电随机噪点(CantoMk6-main 原样:128px/块) */
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
 *  背光 LEDC(CantoMk6-main 原样)
 * ════════════════════════════════════════════════════════════════ */
static void display_backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = BL_LEDC_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s (backlight will stay uncontrolled)", esp_err_to_name(ret));
        return;
    }
    uint32_t act_freq = ledc_get_freq(LEDC_LOW_SPEED_MODE, BL_LEDC_TIMER);
    ESP_LOGI(TAG, "LEDC timer %d OK: %luHz (requested %dHz)",
             BL_LEDC_TIMER, (unsigned long)act_freq, BL_LEDC_FREQ_HZ);

    ledc_channel_config_t chan_conf = {
        .gpio_num   = DISPLAY_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .flags      = { .output_invert = 0 }, /* 高电平点亮(espp backlight_value=true) */
    };
    ret = ledc_channel_config(&chan_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s (backlight will stay uncontrolled)", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Backlight initialized on GPIO %d (LEDC CH%d TIMER%d %dHz)",
             DISPLAY_PIN_BL, BL_LEDC_CHANNEL, BL_LEDC_TIMER, BL_LEDC_FREQ_HZ);
}

static void display_set_brightness(uint8_t brightness)
{
    s_last_brightness = brightness; /* 记录用户亮度,power_on 恢复用 */
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
            .flags      = { .output_invert = 0 },
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
        /* duty 要到下一个 PWM 周期才生效(5kHz 周期 200µs):
         * 延时 300µs 再读,readback 才是真实硬件值,避免误判。 */
        esp_rom_delay_us(300);
        uint32_t rduty = ledc_get_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
        ESP_LOGI(TAG, "set_brightness(%u -> duty %lu, readback %lu)",
                 brightness, (unsigned long)duty, (unsigned long)rduty);
    }
}

/* bltest 诊断:绕过 PWM,把背光引脚直接拉高/拉低。
 * 目的:不依赖任何仪表,验证 GPIO43(BL) 到背光的物理通路——
 *   引脚 HIGH 而屏幕不亮 => 通路断开/背光板异常;
 *   引脚 HIGH 屏幕亮、PWM 亮度不变 => 亮度调节入口问题(如 KE 开关)。
 *
 * 关键修复(2026-08-28):gpio_set_direction(GPIO_MODE_OUTPUT) 只打开
 * 输出使能(enable),不会把信号源 func_out_sel 从 LEDC 切回 GPIO.out 寄存器!
 * 此前 GPIO43 信号源一直是 LEDC_CH0(ledc_channel_config 绑定),ledc_stop
 * 后 pad 呈高阻,直驱 HIGH/LOW 电平根本没输出 => 屏幕无变化。
 * 必须显式 esp_rom_gpio_connect_out_signal(SIG_GPIO_OUT_IDX) 切信号源。
 * 另:gpio_get_level 在 OUTPUT 模式读输入寄存器(input 被 disable 恒 0),
 * 无诊断价值;改为直读 GPIO 外设寄存器确认输出链路。 */
static void display_bltest(bool high)
{
    esp_err_t e1 = ledc_stop(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, 0);
    esp_rom_gpio_pad_select_gpio(DISPLAY_PIN_BL);
    esp_rom_gpio_connect_out_signal(DISPLAY_PIN_BL, SIG_GPIO_OUT_IDX, 0, 0);
    esp_err_t e2 = gpio_set_direction(DISPLAY_PIN_BL, GPIO_MODE_OUTPUT);
    esp_err_t e3 = gpio_set_level(DISPLAY_PIN_BL, high ? 1 : 0);
    /* 注意:GPIO43 >= 32,使能/输出位在 enable1/out1 寄存器(bit = pin-32),
     * enable/out 只覆盖 GPIO0-31(上一版诊断读错寄存器,en/out 恒为 0 假象)。 */
    uint32_t en   = (GPIO.enable1.val >> (DISPLAY_PIN_BL - 32)) & 1u;
    uint32_t out  = (GPIO.out1.val >> (DISPLAY_PIN_BL - 32)) & 1u;
    uint32_t func = GPIO.func_out_sel_cfg[DISPLAY_PIN_BL].func_sel;
    uint32_t freq = ledc_get_freq(LEDC_LOW_SPEED_MODE, BL_LEDC_TIMER);
    uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
    s_bl_gpio_hold = true;
    /* printf 直出(诊断命令专用,绕过 ESP_LOG 过滤,shell 终端必显示) */
    printf("\n### BLTEST GPIO%d %s: stop=%s dir=%s lvl=%s\n"
           "###   en=%u out=%u func_sel=%u (SIG_GPIO_OUT=%u, 直驱时 func_sel 应等于此值)\n"
           "###   LEDC: timer=%luHz ch_duty=%lu (PWM 已 stop)\n",
           DISPLAY_PIN_BL, high ? "HIGH" : "LOW",
           esp_err_to_name(e1), esp_err_to_name(e2), esp_err_to_name(e3),
           en, out, (unsigned)func, (unsigned)SIG_GPIO_OUT_IDX,
           (unsigned long)freq, (unsigned long)duty);
}

static void display_power_on(void)
{
    /* 恢复用户最近设置的亮度(而非固定 100%):否则每次熄屏再亮
     * 背光都跳回全亮,表现为"亮度没记住/调节不明显"。 */
    display_set_brightness(s_last_brightness);
}

static void display_power_off(void)
{
    /* 只灭背光,不覆盖 s_last_brightness:下次 power_on 恢复用户亮度。
     * 用 ledc_stop(而非 duty=0):立即停止 PWM 并输出 idle 低电平,
     * 不依赖下一个 PWM 周期,背光熄灭更彻底、可诊断。 */
    if (s_bl_gpio_hold)
    {
        gpio_set_level(DISPLAY_PIN_BL, 0); /* bltest 直驱状态下直接拉低 */
        ESP_LOGI(TAG, "power_off: BL direct LOW (bltest hold)");
        return;
    }
    esp_err_t r1 = ledc_stop(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, 0);
    if (r1 != ESP_OK)
    {
        ESP_LOGE(TAG, "power_off: ledc_stop FAILED: %s", esp_err_to_name(r1));
    }
    else
    {
        esp_rom_delay_us(300);
        ESP_LOGI(TAG, "power_off: backlight OFF (ledc stopped, idle LOW)");
    }
}

/* ════════════════════════════════════════════════════════════════
 *  设备 HAL ops(注册进 cos_dev_display)
 * ════════════════════════════════════════════════════════════════ */
static const cos_dev_display_ops_t s_display_ops = {
    .set_brightness = display_set_brightness,
    .power_on       = display_power_on,
    .power_off      = display_power_off,
    .bltest         = display_bltest,
};

/* ════════════════════════════════════════════════════════════════
 *  LVGL flush 回调(异步 DMA 版本,参考 Seeed lvgl_workshop Phase 4/5)
 *
 *  GC9A01 期望大端 RGB565(SPI 先发高字节)。LVGL 渲染 buffer 始终是
 *  小端标准 RGB565,因此这里在拷贝到 internal DMA 缓冲后统一做字节
 *  交换再发送——不依赖 LV_COLOR_16_SWAP 宏,保证纯色/图片/文字所有
 *  渲染路径字节序一致(该宏在 lv_refr.c 的生效路径不可控,曾造成双重
 *  交换纯色错乱)。
 *
 *  性能要点:
 *  1. 异步 queue_trans:DMA 在后台传输,LVGL 双缓冲下可立即渲染另一
 *     buffer,渲染与 SPI 传输并行(spi_device_transmit 同步版会让 CPU
 *     在 flush 期间空转,双缓冲形同虚设)。
 *  2. 双 slot 轮换:LVGL 双缓冲最多 2 个 flush 在途,2 块 internal
 *     DMA 缓冲恰好匹配;post_cb 在 SPI 驱动任务上下文触发
 *     lv_display_flush_ready(线程安全,不占 ISR)。
 *  3. SWAR 32-bit 字节交换:一次交换两个像素,8 路展开,比逐 16-bit
 *     循环快数倍(ESP32-S3 Xtensa 无 SIMD,用 SWAR 即可接近极限)。
 *
 *  SPI DMA 不认 PSRAM 地址:LVGL 渲染缓冲位于 PSRAM,直接作为
 *  tx_buffer 会让 SPI master 驱动内部临时 malloc(internal DMA buffer)
 *  去 copy——internal 紧张时分配失败(ESP_ERR_NO_MEM)。故先 memcpy
 *  到 static internal DMA 缓冲,稳定且零分配。
 * ════════════════════════════════════════════════════════════════ */
#define FLUSH_BUF_BYTES (BOARD_GC9A01_WIDTH * (BOARD_GC9A01_HEIGHT / 6) * 2)  /* 240*40*2 = 19200B */
#define FLUSH_BUF_SLOTS 2
DRAM_ATTR static uint8_t s_spi_flush_buf[FLUSH_BUF_SLOTS][FLUSH_BUF_BYTES]
    __attribute__((aligned(4)));

/* 异步 flush 事务组:每个 slot 一组"窗口命令 + 像素数据"事务,全部 queue_trans
 * 排队,SPI 驱动按 FIFO 依次执行(命令必在像素前)。
 *
 * 关键(参照 IDF esp_lcd_panel_io_spi 的 lcd_spi_trans_descriptor_t 设计):
 * - IDF v5.x 的 spi_transaction_t 没有事务级回调字段,pre_cb/post_cb 是设备级
 *   (每个事务都触发,且运行在 ISR 上下文)。区分事务靠"私有描述符":把
 *   spi_transaction_t 作为描述符第一个成员,pre_cb 里用 container_of 还原
 *   描述符,读 dc_level 位决定 DC 电平。
 * - DC 引脚在 pre_cb 里按事务标记切换(ISR 上下文,异步安全,无队列竞态)。
 * - flush 完成同步走 LVGL 的 flush_wait_cb(在 LVGL 任务上下文调用,不是
 *   ISR):用 spi_device_get_trans_result 把在途事务取回,避开 ISR 调
 *   lv_display_flush_ready 的 timer resume 断言风险;LVGL 等待后自动清
 *   disp->flushing,渲染下一帧与上一帧 DMA 传输并行。
 * - 不能对命令用 spi_device_transmit 同步发送——它内部 get_trans_result
 *   会取回队列中第一个完成的事务(可能是异步像素事务),与传入指针不符,
 *   触发 assert(ret_trans == trans_desc) 崩溃(真机已踩)。 */
typedef struct {
    spi_transaction_t base;        /* 必须是第一个成员 */
    uint8_t           dc_level;    /* 0=命令(DC低) 1=数据(DC高) */
} flush_trans_t;

/* 在途事务计数(LVGL 任务上下文单线程访问,无需原子):
 * flush_cb queue 时累加,flush_wait_cb get_trans_result 取回时递减 */
static volatile int s_pending_tx;

/* 唤醒黑屏诊断:LVGL flush 帧计数(单线程自增/读取,无需原子)。
 * display_off/on 打印差值,可区分"唤醒后根本没渲染"与"渲染内容黑"。 */
static uint32_t s_flush_total  = 0; /* 累计 flush 的帧数 */
static uint32_t s_flush_at_off = 0; /* 上次熄屏(display_off)时的计数 */

typedef struct {
    flush_trans_t col_cmd;   /* 0x2A 命令 */
    flush_trans_t col_data;  /* 0x2A 参数(4B) */
    flush_trans_t row_cmd;   /* 0x2B 命令 */
    flush_trans_t row_data;  /* 0x2B 参数(4B) */
    flush_trans_t ramwr;     /* 0x2C 命令 */
    flush_trans_t pixel;     /* 像素数据 */
    uint8_t       col_buf[5]; /* 0x2A + x0h x0l x1h x1l */
    uint8_t       row_buf[5]; /* 0x2B + y0h y0l y1h y1l */
    uint8_t       ramwr_byte; /* 0x2C */
} flush_slot_t;

static flush_slot_t s_flush_slot[FLUSH_BUF_SLOTS];
static int s_flush_slot_idx;

#define flush_containing(trans) \
    ((flush_trans_t *)((uint8_t *)(trans) - offsetof(flush_trans_t, base)))

/* 小端标准 RGB565 -> 大端发送序(GC9A01)。
 * 32-bit SWAR:一次交换两个像素((v&0xFF00FF00)>>8)|((v&0x00FF00FF)<<8),
 * 8 路展开。len 为字节数(恒为偶数)。 */
static inline void _swap16_swar(uint8_t *buf, size_t len)
{
    uint32_t *p32 = (uint32_t *)(uintptr_t)buf;
    size_t n32 = len / 4;
    size_t i = 0;
    for (; i + 8 <= n32; i += 8)
    {
        p32[i + 0] = ((p32[i + 0] & 0xFF00FF00u) >> 8) | ((p32[i + 0] & 0x00FF00FFu) << 8);
        p32[i + 1] = ((p32[i + 1] & 0xFF00FF00u) >> 8) | ((p32[i + 1] & 0x00FF00FFu) << 8);
        p32[i + 2] = ((p32[i + 2] & 0xFF00FF00u) >> 8) | ((p32[i + 2] & 0x00FF00FFu) << 8);
        p32[i + 3] = ((p32[i + 3] & 0xFF00FF00u) >> 8) | ((p32[i + 3] & 0x00FF00FFu) << 8);
        p32[i + 4] = ((p32[i + 4] & 0xFF00FF00u) >> 8) | ((p32[i + 4] & 0x00FF00FFu) << 8);
        p32[i + 5] = ((p32[i + 5] & 0xFF00FF00u) >> 8) | ((p32[i + 5] & 0x00FF00FFu) << 8);
        p32[i + 6] = ((p32[i + 6] & 0xFF00FF00u) >> 8) | ((p32[i + 6] & 0x00FF00FFu) << 8);
        p32[i + 7] = ((p32[i + 7] & 0xFF00FF00u) >> 8) | ((p32[i + 7] & 0x00FF00FFu) << 8);
    }
    for (; i < n32; i++)
    {
        p32[i] = ((p32[i] & 0xFF00FF00u) >> 8) | ((p32[i] & 0x00FF00FFu) << 8);
    }
    if (len & 2)   /* 剩余 1 个像素 */
    {
        uint16_t *p16 = (uint16_t *)(buf + n32 * 4);
        *p16 = (uint16_t)((*p16 >> 8) | (*p16 << 8));
    }
}

/* 设备级 pre_cb:每个事务传输前按描述符标记切换 DC(ISR 上下文,异步安全)。
 * 命令(0x2A/0x2B/0x2C)DC=0,参数/像素 DC=1。 */
static void flush_pre_cb(spi_transaction_t *t)
{
    flush_trans_t *ft = flush_containing(t);
    gpio_set_level(DISPLAY_PIN_DC, ft->dc_level);
}

/* flush 同步回调(LVGL 任务上下文,不是 ISR):在渲染下一帧前取回在途
 * 事务,确保上一帧 DMA 传输完毕再复用缓冲。LVGL 调用后自动清
 * disp->flushing,渲染下一帧与上一帧传输并行(见 draw_buf_flush 的
 * wait_for_flushing)。 */
static void display_flush_wait_cb(lv_display_t *disp)
{
    (void)disp;
    while (s_pending_tx > 0)
    {
        spi_transaction_t *t;
        if (spi_device_get_trans_result(s_spi, &t, portMAX_DELAY) != ESP_OK)
            break;   /* 极端:避免死循环,交给 LVGL 继续 */
        s_pending_tx--;
    }
}

/* 初始化各 slot 事务组(每帧仅重写 col_buf/row_buf/ramwr_byte 内容) */
static void display_flush_tx_init(void)
{
    for (int i = 0; i < FLUSH_BUF_SLOTS; i++)
    {
        flush_slot_t *fs = &s_flush_slot[i];
        memset(fs, 0, sizeof(*fs));
        fs->col_cmd.base.length     = 8;                /* 1 字节命令 */
        fs->col_cmd.base.tx_buffer  = fs->col_buf;
        fs->col_cmd.dc_level        = 0;
        fs->col_data.base.length    = 32;               /* 4 字节参数 */
        fs->col_data.base.tx_buffer = &fs->col_buf[1];
        fs->col_data.dc_level       = 1;
        fs->row_cmd.base.length     = 8;
        fs->row_cmd.base.tx_buffer  = fs->row_buf;
        fs->row_cmd.dc_level        = 0;
        fs->row_data.base.length    = 32;
        fs->row_data.base.tx_buffer = &fs->row_buf[1];
        fs->row_data.dc_level       = 1;
        fs->ramwr.base.length       = 8;
        fs->ramwr.base.tx_buffer    = &fs->ramwr_byte;
        fs->ramwr.dc_level          = 0;
        /* pixel:每帧动态设 length/tx_buffer */
        fs->pixel.dc_level = 1;
    }
    s_flush_slot_idx = 0;
    s_pending_tx     = 0;
}

static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    s_flush_total++;
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;
    size_t len = (size_t)w * h * 2;
    if (len > FLUSH_BUF_BYTES) len = FLUSH_BUF_BYTES;

    int slot = s_flush_slot_idx;
    s_flush_slot_idx = 1 - s_flush_slot_idx;
    flush_slot_t *fs = &s_flush_slot[slot];

    /* 拷贝 + 字节交换到内部 DMA 缓冲 */
    memcpy(s_spi_flush_buf[slot], px_map, len);
    _swap16_swar(s_spi_flush_buf[slot], len);

    /* 重写窗口命令内容(0x2A 列 / 0x2B 行 / 0x2C RAMWR) */
    fs->col_buf[0] = 0x2A;
    fs->col_buf[1] = (uint8_t)(area->x1 >> 8); fs->col_buf[2] = (uint8_t)(area->x1 & 0xFF);
    fs->col_buf[3] = (uint8_t)(area->x2 >> 8); fs->col_buf[4] = (uint8_t)(area->x2 & 0xFF);
    fs->row_buf[0] = 0x2B;
    fs->row_buf[1] = (uint8_t)(area->y1 >> 8); fs->row_buf[2] = (uint8_t)(area->y1 & 0xFF);
    fs->row_buf[3] = (uint8_t)(area->y2 >> 8); fs->row_buf[4] = (uint8_t)(area->y2 & 0xFF);
    fs->ramwr_byte = 0x2C;

    /* 像素事务:每帧整体重置描述符。
     * 必须清零 rxlength!全双工下 check_trans_valid 会把 rxlength==0 自动
     * 改写为 length(esp_driver_spi spi_master.c: "default rxlength to be
     * the same as length")——若不清零,上一帧的大 length 残留为 rxlength,
     * 本帧 partial 区域变小时 rxlength > length,check_trans_valid 拒绝
     * 该事务(真机每帧 5/6 queued + "rx length > tx length" 即此)。 */
    memset(&fs->pixel, 0, sizeof(fs->pixel));
    fs->pixel.dc_level        = 1;
    fs->pixel.base.length     = len * 8;
    fs->pixel.base.tx_buffer  = s_spi_flush_buf[slot];

    /* 全部 queue_trans 排队(命令必在像素前,SPI 驱动 FIFO 保证),
     * 并累计在途计数(flush_wait_cb 取回)。 */
    spi_transaction_t *txs[] = {
        &fs->col_cmd.base, &fs->col_data.base,
        &fs->row_cmd.base, &fs->row_data.base,
        &fs->ramwr.base,   &fs->pixel.base,
    };
    int queued = 0;
    for (int i = 0; i < 6; i++)
    {
        if (spi_device_queue_trans(s_spi, txs[i], portMAX_DELAY) == ESP_OK)
            queued++;
        else
            break;
    }
    s_pending_tx += queued;

    if (queued < 6)
    {
        /* 极端失败(如队列溢出):同步 polling 兜底发送像素(polling 不走
         * 结果队列,无断言风险),保证画面不丢;已入队命令仍会被
         * flush_wait_cb 取回。 */
        ESP_LOGE(TAG, "flush queue failed (%d/6 queued), sync polling fallback", queued);
        spi_device_polling_transmit(s_spi, &fs->pixel.base);
    }
}

/* ════════════════════════════════════════════════════════════════
 *  对外接口(板级 main.c 调用)
 * ════════════════════════════════════════════════════════════════ */

/* GC9A01 驱动初始化:SPI bus + device + init 序列 + 清屏 + 背光 + 自检 */
esp_err_t cos_dev_display_gc9a01_init(void)
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
     * UI 层开机动画(cos_boot_anim)负责,避免驱动自检与系统动画叠加造成色序混乱 */
    return ESP_OK;
}

/* 注册到 CantoMk6 设备 HAL */
void cos_dev_display_gc9a01_register(void)
{
    cos_dev_display_register(&s_display_ops);
    ESP_LOGI(TAG, "Registered to device HAL (set_brightness/power_on/power_off)");
}

/* 排空在途异步 flush 事务(LVGL 任务上下文单线程,逻辑同 display_flush_wait_cb):
 * display_send_cmd/display_send_data 用 spi_device_transmit 同步发送,其内部
 * get_trans_result 取回"结果队列中第一个完成的事务"。若上一条 UI 交互帧
 * (异步队列;flush_wait_cb 只在渲染下一帧前取回)尚未取回,同步发送会错拿
 * 该像素事务,触发 assert(ret_trans == trans_desc) 崩溃(真机已踩:关机流程
 * 点按钮反馈帧在途时,display_off 的 0x28 同步发送崩)。排空后无在途事务,
 * 同步发送安全。 */
static void display_drain_pending_tx(void)
{
    while (s_pending_tx > 0)
    {
        spi_transaction_t *t;
        if (spi_device_get_trans_result(s_spi, &t, portMAX_DELAY) != ESP_OK)
            break;   /* 极端:避免死循环 */
        s_pending_tx--;
    }
}

/* 清屏纯黑(关机深睡前置调用):
 * GC9A01 无硬件 RST(纯软件复位),深睡/唤醒后面板 GRAM 保留深睡前最后一帧
 * (关机页面残影)。深睡前把整屏刷成纯黑后,即使背光被任何意外路径点亮
 * (如非轮询唤醒的短暂状态),显示内容也是黑帧,肉眼不可见。
 * 实现:同步 spi_device_transmit 发送(先排空在途异步事务,驱动内部等待 bus
 * 空闲,无 DMA 竞争)。 */
void cos_dev_display_gc9a01_fill_black(void)
{
    if (!s_init_done) return;

    display_drain_pending_tx();   /* 排空在途异步 flush,防同步发送断言崩溃 */

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
    ESP_LOGI(TAG, "fill_black done (%u px)", (unsigned)total_pixels);
}

/* 面板显示关闭(0x28 DISPOFF):仅关面板显示,背光与 GRAM 均保持。
 * 关机深睡轮询的每次唤醒 boot 窗口(ROM→bootloader_init,约 200ms)里
 * GPIO43 复位成 UART0 TXD 高,背光反亮且软件无法提前压黑(flash cache
 * 未初始化);面板 OFF 后即使背光亮,显示也是黑帧,彻底消除闪烁。
 * 面板常供电且无硬件 RST(纯软件复位),该寄存器状态跨深睡保持;
 * 正常开机 init 序列(0x11/0x29)会重新打开显示。
 * 比 fill_black 轻量(单条命令),避免深睡前大量 SPI 活动引发
 * esp_deep_sleep_start 失败(此前 fill_black 已实测触发该问题)。 */
void cos_dev_display_gc9a01_display_off(void)
{
    if (!s_init_done) return;

    /* 先排空在途异步 flush:关机按钮按下反馈帧的事务可能仍在队列(flush_wait_cb
     * 只在渲染下一帧前取回,而事件处理链路同步直达此处),同步发送 0x28 会错拿
     * 该像素事务,触发 assert(ret_trans == trans_desc) 崩溃(真机已踩)。 */
    display_drain_pending_tx();

    s_flush_at_off = s_flush_total;   /* 记录熄屏时 flush 计数,唤醒对比用 */
    display_send_cmd(0x28);   /* Display OFF */
    ESP_LOGI(TAG, "display OFF (0x28), flush_total=%u", (unsigned)s_flush_total);
}

/* 面板显示打开(0x29 DISPON):与 display_off 配对,用于熄屏(Light Sleep)
 * 后唤醒恢复显示。
 * 目的:熄屏时关面板可省掉面板静态电流(熄屏待机最大的一项,见
 * board_power_set 的 DEV_POWER_STATE_SLEEP 分支)。
 * GRAM 内容保留,无需重跑 init 序列;同 display_off 一样先排空在途异步
 * flush,避免同步发送与 LVGL 渲染帧事务竞争触发 assert。 */
void cos_dev_display_gc9a01_display_on(void)
{
    if (!s_init_done) return;

    display_drain_pending_tx();

    display_send_cmd(0x29);   /* Display ON */
    /* 退出 DISPOFF 后面板需要稳定时间(上电 init 序列用 120ms;此处 GRAM 与
     * 电荷泵已在工作,20ms 足够,再开背光即可避免黑帧/闪白) */
    vTaskDelay(pdMS_TO_TICKS(20));
    /* 唤醒黑屏诊断:差值=熄屏到唤醒期间 LVGL flush 的帧数(应为 0~少量;
     * 若远大于此说明熄屏时 UI 仍在持续刷屏,应检查是否漏停动画);
     * 唤醒后的 flush 由下一条日志之后新帧反映。 */
    ESP_LOGI(TAG, "display ON (0x29), flushed=%u since off (total %u)",
             (unsigned)(s_flush_total - s_flush_at_off), (unsigned)s_flush_total);
}

/* LVGL 显示初始化:lv_display_create + 双缓冲(PARTIAL) + flush_cb */
esp_err_t cos_dev_display_gc9a01_lvgl_init(void)
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
    /* 2016-09-05 曾在此处 heap_caps_dump_all() 排障:8MB PSRAM 池逐块经 ROM
     * UART 打印、长时间屏蔽中断,真机触发 Interrupt WDT(TWDT)panic,已移除。 */
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
    ESP_LOGI(TAG, "LVGL buffers: %lu B x2 @%p/%p (PSRAM/DMA)",
             (unsigned long)buf_bytes, (void *)buf1, (void *)buf2);

    lv_display_set_buffers(disp, buf1, buf2, buf_pixels, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, display_flush_cb);
    /* 异步 flush:flush_cb 只排队(不调 flush_ready);完成同步由
     * flush_wait_cb 在 LVGL 任务上下文用 get_trans_result 取回在途事务。
     * 相比 ISR 里调 lv_display_flush_ready,避开 ISR 调 LVGL timer 的风险。 */
    lv_display_set_flush_wait_cb(disp, display_flush_wait_cb);
    lv_display_set_default(disp);

    ESP_LOGI(TAG, "LVGL display registered (%dx%d, partial %u px)",
             BOARD_GC9A01_WIDTH, BOARD_GC9A01_HEIGHT, (unsigned)buf_pixels);
    return ESP_OK;
}
