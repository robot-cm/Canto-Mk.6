/**
 * @file main.c
 * @brief ElenixOS ESP32-S3 板级入口(XIAO ESP32-S3 + Round Display)
 *
 * 阶段:集成 GC9A01 真实驱动 + CHSC6X 真实触摸 + microSD(SDSPI 真 SD,
 * 优先挂载 /sdcard,无卡回退 SPIFFS;RTC 为 BM8563 真实驱动)。
 *   - SPI/I2C 总线初始化(SPI 由 GC9A01 驱动初始化,I2C 板级初始化)
 *   - GC9A01 LCD 真实驱动:init + register HAL + LVGL display
 *   - CHSC6X 触摸:LVGL POINTER indev(read timer 自动轮询)
 *   - FreeRTOS 任务:ui_task 调 lv_timer_handler
 *   - Shell:USB-CDC stub(D6/D7 被屏占)
 *   - 调用 eos_init()
 *
 * 依据:AGENTS.md 第29节修改流程 + 子报告1引脚表 + 子报告2方案A
 */

#include "board_xiao_esp32s3_round.h"

/* ESP-IDF 板级 API */
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
/* 电池:ADC1_CH0(GPIO1) 单次模式 + eFuse 校准(见 _battery_request_update) */
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
/* 充电检测:USB-Serial-JTAG is_connected = USB 已插入(VBUS 供电)→ charging
 * XIAO ESP32-S3 普通版无 ADC_BAT / VBUS 检测脚,充电状态只能靠 USB 连接推断 */
#include "driver/usb_serial_jtag.h"
/* 关机深睡:esp_deep_sleep_start / esp_sleep_get_wakeup_cause /
 * esp_sleep_enable_timer_wakeup;深睡期间用 pad hold 保持背光为低
 * (GPIO43 非 RTC GPIO,浮空会被板上拉反亮,见 board_backlight_low_hold) */
#include "esp_sleep.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
/* microSD(SDSPI,与 LCD 共用 SPI3 总线,见 board_sd_mount) */
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_dma_utils.h" /* esp_dma_is_buffer_alignment_satisfied 诊断 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
/* xTaskCreatePinnedToCoreWithCaps:任务栈可指定内存 cap(见下方任务创建) */
#include "freertos/idf_additions.h"

/* LVGL 9.x */
#include "lvgl.h"

/* ElenixOS Core(平台无关) */
#include "elenix_os.h"
#include "eos_port.h"
#include "kernel/scheduler/eos_dispatcher.h" /* eos_dispatch_tick() 异步回调队列 */
#include "eos_dev_display.h"
#include "eos_dev_display_gc9a01.h"
#include "eos_dev_touch_chsc6x.h"
#include "eos_dev_time.h"
#include "eos_dev_rtc_bm8563.h"
#include "eos_dev_battery.h"
#include "eos_service_battery.h" /* eos_battery_raw_t / eos_battery_report_raw */
#include "eos_dev_power.h"
#include "eos_shell.h"
#include "eos_shell_framework.h"
#include <string.h>
#include "eos_dev_sensor.h"

static const char *TAG = "Board";

/* eos_bt_esp32.c 导出:在 eos_init() 之前、internal RAM 尚连续时预初始化
 * NimBLE(controller 需 internal|DMA 大块连续内存,见 eos_bt_esp32.c 注释) */
extern void eos_bt_esp32_early_init(void);
#include "eos_net_wifi_esp32.h" /* eos_net_wifi_esp32_early_init():带内存门控 */

/* eos_init() 完成信号:ui_task 必须等 Core/UI 服务就绪后才开始 LVGL 渲染 */
static SemaphoreHandle_t s_eos_ready = NULL;

/* ════════════════════════════════════════════════════════════════
 *  电池驱动(真实):D0=GPIO1=ADC1_CH0 单次模式读电池电压
 *  - 20 次均值(raw) → adc_cali 校准转真实 mV(曲线拟合优先,线拟合兜底,
 *    校准不可用再退化为 raw*3300/4095 线性估算,保证任何情况能出数字)
 *  - 电压范围 1850~2100 mV → 0~100%(与 board 头注释一致,
 *    来源 lv_hardware_test.h analogReadMilliVolts(D0))
 *  - 无电流计/充电检测引脚:current_ma=-1, charging=false
 *  - 阶段化懒初始化:unit/chan 与 cali 分开建,失败部分可下次 timer 重试
 * ════════════════════════════════════════════════════════════════ */
static adc_oneshot_unit_handle_t s_batt_adc = NULL;
static adc_cali_handle_t         s_batt_cali = NULL;

static esp_err_t _battery_hw_init(void)
{
    if (s_batt_adc == NULL)
    {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = ADC_UNIT_1,
        };
        esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_batt_adc);
        if (ret != ESP_OK)
            return ret;

        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_oneshot_config_channel(s_batt_adc, BOARD_BATTERY_ADC_CH, &chan_cfg);
        if (ret != ESP_OK)
            return ret;
    }

    if (s_batt_cali == NULL)
    {
        adc_cali_curve_fitting_config_t curve_cfg = {
            .unit_id   = ADC_UNIT_1,
            .atten     = ADC_ATTEN_DB_12,
            .bitwidth  = ADC_BITWIDTH_12,
        };
        /* ESP32-S3 仅支持 curve fitting 校准(依赖出厂 eFuse),
         * 创建失败时 s_batt_cali 保持 NULL → request_update 走线性估算兜底 */
        esp_err_t ret = adc_cali_create_scheme_curve_fitting(&curve_cfg, &s_batt_cali);
        if (ret != ESP_OK)
        {
            s_batt_cali = NULL;
            ESP_LOGW(TAG, "Battery ADC cali unavailable, use linear estimate");
        }
    }
    return ESP_OK;
}

/* 电压→百分比:分段查表 + 线性插值,贴合锂电 S 形放电曲线。
 * 表中点为分压后 D0 电压实测近似,如需更准请按实板标定
 * (量到某百分比对应电压,填进 v/p 表即可)。 */
static int _batt_voltage_to_percent(int mv)
{
    static const int16_t v[] = {1850, 1890, 1930, 1975, 2010, 2045, 2070, 2090, 2100};
    static const int8_t  p[] = {0,    15,   30,   45,   60,   75,   88,   95,  100};
    const int n = (int)(sizeof(v) / sizeof(v[0]));

    if (mv <= v[0])        return p[0];
    if (mv >= v[n - 1])    return p[n - 1];
    for (int i = 1; i < n; i++)
    {
        if (mv <= v[i])
            return p[i - 1] + (mv - v[i - 1]) * (p[i] - p[i - 1])
                              / (v[i] - v[i - 1]);
    }
    return 100;
}

/* 被电池服务 LVGL timer 周期调用(正常 60s,活跃 10s,充电 5s) */
static void _battery_request_update(void)
{
    if (s_batt_adc == NULL)
    {
        if (_battery_hw_init() != ESP_OK)
        {
            ESP_LOGE(TAG, "Battery ADC init failed, retry next tick");
            return;
        }
        ESP_LOGI(TAG, "Battery ADC ready (GPIO%d ADC1_CH0)", BOARD_BATTERY_ADC_PIN);
    }

    int sum_raw = 0;
    for (int i = 0; i < BOARD_BATTERY_SAMPLES; i++)
    {
        int raw = 0;
        if (adc_oneshot_read(s_batt_adc, BOARD_BATTERY_ADC_CH, &raw) != ESP_OK)
            return;   /* 本次采样失败:放弃本轮,下次 timer 再读 */
        sum_raw += raw;
    }
    int raw_avg = sum_raw / BOARD_BATTERY_SAMPLES;

    int mv = -1;
    if (s_batt_cali != NULL && adc_cali_raw_to_voltage(s_batt_cali, raw_avg, &mv) != ESP_OK)
        mv = -1;
    if (mv < 0)
        mv = raw_avg * 3300 / 4095;   /* 无校准兜底:线性估算 */

    int percent = _batt_voltage_to_percent(mv);

    /* 充电判定:USB 已插入(VBUS 供电)视为充电,但电压 ≥ 满电阈值
     * 时视为已充满(充满后充电 IC 停充,不再显示绿色)。
     * XIAO ESP32-S3 普通版无充电状态 GPIO,这是硬件上最可靠的信号:
     * USB-Serial-JTAG 外设在检测到 VBUS 时置位 connected。 */
    bool usb_in   = usb_serial_jtag_is_connected();
    bool charging = usb_in && (mv >= 0) && (mv < BOARD_BATTERY_FULL_MV);

    eos_battery_raw_t raw = {
        .percent    = (int8_t)percent,
        .voltage_mv = (int16_t)mv,
        .current_ma = -1,
        .charging   = charging,
    };
    eos_battery_report_raw(&raw);
}

static const eos_battery_dev_ops_t s_board_battery_ops = {
    .request_update = _battery_request_update,
};

/* ════════════════════════════════════════════════════════════════
 *  I2C 总线初始化(touch CHSC6X + RTC BM8563 共用)
 *  SPI 总线由 GC9A01 驱动初始化(gc9a01_init 内含 spi_bus_initialize)
 * ════════════════════════════════════════════════════════════════ */
static esp_err_t board_i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = BOARD_I2C_SDA_PIN,
        .scl_io_num       = BOARD_I2C_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(BOARD_I2C_HOST, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C config: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_driver_install(BOARD_I2C_HOST, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C install: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C bus ready (SDA=%d SCL=%d %dkHz)",
             BOARD_I2C_SDA_PIN, BOARD_I2C_SCL_PIN, BOARD_I2C_FREQ_HZ / 1000);
    return ESP_OK;
}

/* [DIAG] I2C 扫描:验证 XIAO 与 Round Display 扩展板的物理连接与供电
 * 板载设备:CHSC6X 触摸 = 0x2E, RTC = 0x51(PCF8563/BM8563)
 * - 发现设备 → 扩展板连接/供电正常 → SPI 问题聚焦驱动配置
 * - 无任何设备 → 物理连接或供电问题,需检查 XIAO 插接方向/排针/开关 */
static void board_i2c_scan(void)
{
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (uint8_t)(addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(BOARD_I2C_HOST, cmd, pdMS_TO_TICKS(20));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "[DIAG] I2C found device at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "[DIAG] I2C scan done: %u device(s) (expect CHSC6X=0x2E RTC=0x51)",
             (unsigned)found);
}

static void board_gpio_init(void)
{
    /* 触摸 INT(低有效) */
    gpio_config_t tint = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_INT_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&tint);

    /* 用户 LED */
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << BOARD_LED_PIN,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led);
    gpio_set_level(BOARD_LED_PIN, 0);
}

/* ════════════════════════════════════════════════════════════════
 *  FreeRTOS 任务
 * ════════════════════════════════════════════════════════════════ */

static void ui_task(void *arg)
{
    (void)arg;
    /* 等待 eos_init() 完成(Core/UI 服务就绪)后才开始 LVGL 渲染
     * (任务在 eos_init() 之前创建,见 app_main 中注释) */
    if (s_eos_ready != NULL) {
        xSemaphoreTake(s_eos_ready, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "ui_task started (LVGL %dx%d)", BOARD_GC9A01_WIDTH, BOARD_GC9A01_HEIGHT);
    const TickType_t period = pdMS_TO_TICKS(BOARD_LVGL_TIMER_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_tick = last_wake;
    uint32_t ticks = 0;
    for (;;) {
        /* 注意:lv_timer_handler() 内部自带 lv_lock/lv_unlock(递归锁),
         * 这里不再外层加锁,避免锁嵌套混乱 */
        /* tick 必须前进真实经过的时间:用 FreeRTOS tick 差分,而非固定增量。
         * 历史 bug:每 5ms 循环只 lv_tick_inc(1) → 所有 LVGL timer 慢 5 倍
         * (JS lv.timer 1000ms 倒计时实际 5s,Breach 失准)。
         * 差分法不受单轮循环耗时波动影响(渲染/flush 拖长某轮也不会漂移),
         * 计时永远与真实时间同步。FREERTOS_HZ=1000 → portTICK_PERIOD_MS=1,
         * (now - last_tick) 即真实毫秒。 */
        TickType_t now = xTaskGetTickCount();
        lv_tick_inc((uint32_t)(now - last_tick) * portTICK_PERIOD_MS);
        last_tick = now;
        /* 统一走 eos_main_loop():dispatch_tick + lv_timer_handler,并在开机动画
         * 完成后于此初始化 activity controller(主界面延迟显示的关键入口)。 */
        eos_main_loop();
        if ((++ticks % 200) == 0) {
            /* 心跳诊断:证明 ui_task 循环活着(约 1s 一次) */
            ESP_LOGI(TAG, "ui heartbeat: %u ticks, DRAM free=%u",
                     (unsigned)ticks,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        vTaskDelayUntil(&last_wake, period);
    }
}

static void input_task(void *arg)
{
    (void)arg;
    /* 触摸已由 LVGL POINTER indev 的 read timer 驱动(eos_dev_touch_chsc6x_init),
     * read_cb 随 lv_timer_handler() 在 ui_task 内执行,无需独立任务喂入。
     * 本任务保留,供未来按键/手势/传感器扩展。 */
    ESP_LOGI(TAG, "input_task started (touch via LVGL indev read timer)");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void service_task(void *arg)
{
    (void)arg;
    /* TODO 阶段C: Wi-Fi / SOCKS5 / RTC 周期维护 */
    ESP_LOGI(TAG, "service_task started");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void script_task(void *arg)
{
    (void)arg;
    /* TODO 阶段C: JerryScript 执行(SPM 加载 main.js)*/
    ESP_LOGI(TAG, "script_task started");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ════════════════════════════════════════════════════════════════
 *  Shell:USB-Serial-JTAG 交互式命令控制台
 *  依据子报告1:D6/D7 被 LCD 背光/触摸 INT 占用,UART 不可用;但板载
 *  USB-Serial-JTAG(idf.py monitor 日志口)是全双工的——读输入喂
 *  eos_shell_framework_feed(),命令输出写回该口,即可直接敲命令
 *  (如 'display brightness 50')。CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
 *  已把 stdin/stdout 映射到该口,故用标准库 I/O 即可,零 GPIO 占用。
 * ════════════════════════════════════════════════════════════════ */
static void shell_usb_out(const char *line, void *user)
{
    (void)user;
    if (!line)
        return;
    fputs(line, stdout);
    fputs("\r\n", stdout);
    fflush(stdout);
}

static void shell_usb_echo(const char *line, void *user)
{
    (void)user;
    if (!line)
        return;
    fputs(line, stdout);
    fflush(stdout);
}

static void shell_usb_line_handler(const char *line, void *user)
{
    (void)user;
    eos_shell_exec(line, shell_usb_out, NULL);
    eos_shell_framework_prompt(shell_usb_echo, NULL);
}

static void shell_usb_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        int c = fgetc(stdin); /* 阻塞读 USB-JTAG 输入 */
        if (c == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(10)); /* 防断开时忙转 */
            continue;
        }
        eos_shell_framework_feed((char)c, shell_usb_echo, shell_usb_line_handler, NULL);
    }
}

static void board_shell_usb_cdc_start(void)
{
    eos_shell_framework_init();
    /* shell 只读写 USB 口,不碰 flash 写 → 栈放 PSRAM,别挤 internal RAM
     * (eos_init 前 internal 必须为 ui_task 保留 48KB+TCB 连续块)。 */
    BaseType_t r = xTaskCreatePinnedToCoreWithCaps(shell_usb_task, "shell_usb", 4096, NULL, 5, NULL,
                                                   BOARD_TASK_INPUT_AFFINITY, MALLOC_CAP_SPIRAM);
    if (r == pdPASS)
        ESP_LOGI(TAG, "Shell: USB-Serial-JTAG console ready — type 'help' in idf.py monitor");
    else
        ESP_LOGE(TAG, "Shell: USB-Serial-JTAG task create failed");
}

/* ════════════════════════════════════════════════════════════════
 *  microSD 挂载(/sdcard)— 真 SD 优先,SPIFFS 兜底
 *  microSD 与 LCD 共用 SPI3 总线(SCK=D8/MOSI=D10/MISO=D9),SD CS=D2=GPIO3:
 *   - SPI bus 已由 eos_dev_display_gc9a01_init() 完成 spi_bus_initialize
 *     (main 第 3 步先于本函数执行),SD 通过 sdspi_host_init_device
 *     (内部 spi_bus_add_device) 挂到同一 bus,绝不重复初始化总线。
 *   - 互斥:ESP-IDF SPI master 的 bus lock(spi_bus_lock)保证 LCD 刷屏
 *     与 SD IO 的 SPI transaction 原子互斥,无需手写 mutex。
 *   - 挂载失败(无卡/坏卡/非 FAT)绝不格式化用户卡,返回错误 → 回退 SPIFFS。
 * ════════════════════════════════════════════════════════════════ */
/* [FIX] SDSPI DMA 内存策略:ESP32-S3 的 AHB GDMA 支持 PSRAM。
 * 默认 sdspi_host_get_dma_info 只返回 MALLOC_CAP_DMA(内部 RAM):
 * 内部 DMA RAM 被 BT controller / LCD SPI 总线缓冲占满后,sdmmc 读写
 * 的临时缓冲 esp_dma_capable_malloc() 直接失败(ESP_ERR_NO_MEM 0x101,
 * 日志 "dma_utils: Not enough heap memory")→ FATFS 读失败 → 系统崩溃。
 * 返回 DMA|SPIRAM caps 后:临时缓冲可落 PSRAM(8MB),不再受内部 DMA 限制。 */
static void board_sd_get_dma_info(int slot, esp_dma_mem_info_t *dma_mem_info)
{
    (void)slot;
    dma_mem_info->extra_heap_caps = MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM;
    dma_mem_info->dma_alignment_bytes = 4;
}

static esp_err_t board_sd_mount(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot         = BOARD_SD_SPI_HOST;               /* SPI3_HOST,与 LCD 同总线 */
    host.max_freq_khz = BOARD_SD_SPI_FREQ_HZ / 1000;     /* 20 MHz */
    host.get_dma_info = &board_sd_get_dma_info;          /* [FIX] PSRAM fallback */

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BOARD_SD_CS_PIN;               /* D2 = GPIO3 */
    slot_config.host_id = host.slot;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,                 /* 绝不格式化用户卡 */
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config,
                                            &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed(%s) - falling back to SPIFFS",
                 esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SD card mounted: /sdcard");
    sdmmc_card_print_info(stdout, card);

    /* [DIAG-FS] 文件系统逐层实验:定位 fopen("wb") errno=22 的层次 */
    {
        FILE *f = NULL;
        int r = 0;
        errno = 0;
        f = fopen("/sdcard/_fs_test.txt", "wb");
        ESP_LOGI(TAG, "[DIAG-FS] 1 fopen /sdcard/_fs_test.txt wb -> %s errno=%d (%s)",
                 f ? "OK" : "FAIL", errno, strerror(errno));
        if (f) { fputs("hi", f); fclose(f); }
        errno = 0;
        r = mkdir("/sdcard/.sys", 0755);
        ESP_LOGI(TAG, "[DIAG-FS] 2 mkdir /sdcard/.sys -> %d errno=%d (%s)",
                 r, errno, strerror(errno));
        errno = 0;
        f = fopen("/sdcard/.sys/_fs_test2.txt", "wb");
        ESP_LOGI(TAG, "[DIAG-FS] 3 fopen /sdcard/.sys/_fs_test2.txt wb -> %s errno=%d (%s)",
                 f ? "OK" : "FAIL", errno, strerror(errno));
        if (f) { fputs("hi", f); fclose(f); }
        errno = 0;
        r = mkdir("/sdcard/.sys/config", 0755);
        ESP_LOGI(TAG, "[DIAG-FS] 4 mkdir /sdcard/.sys/config -> %d errno=%d (%s)",
                 r, errno, strerror(errno));
        errno = 0;
        f = fopen("/sdcard/.sys/config/cfg.json", "wb");
        ESP_LOGI(TAG, "[DIAG-FS] 5 fopen /sdcard/.sys/config/cfg.json wb -> %s errno=%d (%s)",
                 f ? "OK" : "FAIL", errno, strerror(errno));
        if (f) { fputs("{}", f); fclose(f); }
        errno = 0;
        f = fopen("/sdcard/.sys/config/", "wb");
        ESP_LOGI(TAG, "[DIAG-FS] 6 fopen dir-as-file -> %s errno=%d (%s)",
                 f ? "OK" : "FAIL", errno, strerror(errno));
        if (f) fclose(f);
    }

    /* [DIAG] SD 读路径:确认临时缓冲是否可落 PSRAM、FATFS 缓冲是否满足直读 */
    {
        esp_dma_mem_info_t dma_info;
        card->host.get_dma_info(card->host.slot, &dma_info);
        ESP_LOGI(TAG, "[DIAG] sd dma align=%d caps=0x%x | INT|DMA free=%u largest=%u",
                 (int)dma_info.dma_alignment_bytes, (unsigned)dma_info.extra_heap_caps,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        void *probe = malloc(512); /* 模拟 FATFS ff_memalloc 缓冲 */
        if (probe) {
            ESP_LOGI(TAG, "[DIAG] probe=%p ext=%d dma=%d sat=%d",
                     probe, (int)esp_ptr_external_ram(probe),
                     (int)esp_ptr_dma_capable(probe),
                     (int)esp_dma_is_buffer_alignment_satisfied(probe, 512, dma_info));
            free(probe);
        }
        /* 直接测试 sdmmc 写路径的临时缓冲分配(读已直读,写仍依赖它) */
        void *tb = NULL;
        size_t asz = 0;
        esp_err_t merr = esp_dma_capable_malloc(512, &dma_info, &tb, &asz);
        ESP_LOGI(TAG, "[DIAG] esp_dma_capable_malloc(512)=%s buf=%p size=%u PSRAM_free=%u",
                 esp_err_to_name(merr), tb, (unsigned)asz,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if (tb) free(tb);
    }
    return ESP_OK;
}

/* ════════════════════════════════════════════════════════════════
 *  文件系统挂载(/sdcard)— 真 SD 优先,SD 缺失时回退 SPIFFS
 *  eos_platform_config.h 定义 EOS_SYS_ROOT_DIR="/sdcard",
 *  eos_fs_realpath() 会把所有 POSIX 绝对路径(/.sys/... 等)
 *  自动映射到 /sdcard 下,故挂载此处即可覆盖 config/state/资源。
 *  Plugin Manager 扫描 /sdcard/apps/ 下的 .eapk/.ewpk 自动安装。
 *  回退分支:SPIFFS 首次挂载失败会自动格式化(format_if_mount_failed)。
 * ════════════════════════════════════════════════════════════════ */
static esp_err_t board_fs_mount(void)
{
    if (board_sd_mount() == ESP_OK)
        return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path            = "/sdcard",
        .partition_label      = "spiffs",
        .max_files            = 10,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info(conf.partition_label, &total, &used);
        ESP_LOGI(TAG, "SPIFFS mounted (fallback): /sdcard (%u/%u KB)",
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    } else {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ════════════════════════════════════════════════════════════════
 *  关机深睡(方案A:深睡 + RTC 定时器唤醒轮询,双模式)
 *
 *  模式(关机页选择,经 board_power_setoff_params 传入):
 *    - 触摸模式(timed=0): 唤醒读 CHSC6X,长按开机(连续唤醒读到触摸;
 *      任一唤醒读到即开机 ≈ 按住 3s)。
 *    - 定时模式(timed=1): 触摸无效,存 wake_after_s 秒,深睡一次睡满后
 *      RTC 定时器唤醒即自动开机;零中途唤醒,零微闪。
 *
 *  硬件约束(已核实,AGENTS.md 第28节):
 *    - 触摸 INT = GPIO44,非 RTC GPIO → 深睡期间无法被触摸唤醒
 *      (S3 深睡 GPIO 唤醒仅支持 RTC GPIO0-21),瞬间点击几乎全部错过,
 *      故采用"周期性定时唤醒后轮询触摸状态"捕获手势。
 *    - 背光 BL = GPIO43,非 RTC GPIO → 深睡后浮空被板上拉 → 反亮
 *  因此:深睡唤醒靠 RTC 定时器;唤醒后从 app_main 重新启动(RTC_DATA_ATTR
 *  变量跨深睡保留);轮询期间用 gpio_hold 把背光保持在低电平
 *  (ESP32-S3 所有 GPIO 支持 pad hold,深睡期间由 RTC 域维持)。
 *  定时模式不依赖 esp_rtc_get_time_us()(深睡唤醒后软件时基可能未恢复,
 *  曾导致 now<deadline 永远重睡、根本不开机);只存秒数 + 一次性 RTC
 *  定时器睡眠,唤醒即开机。精度受 RTC 慢时钟(RC)影响 ±5-10%,属预期。
 *
 *  功耗(实测量级):每唤醒一次约 50-80ms 活跃(@50-80mA)+ 深睡 44µA;
 *  触摸模式 3s 周期平均 ≈1.5-2mA,1 分钟后自动降到 5s;定时模式一次
 *  睡到 deadline(零中途唤醒),功耗 = 纯深睡 44µA。
 * ════════════════════════════════════════════════════════════════ */
#define POWEROFF_HOLD_WAKEUPS_TO_BOOT  1   /* 触摸模式:连续 N 次唤醒读到触摸即开机(周期 3s,N=1 ≈ 按住 3s) */
#define POWEROFF_WAKEUP_PERIOD_US      (3000 * 1000)  /* 触摸模式轮询周期:3s */
#define POWEROFF_SLOW_AFTER_WAKES      20  /* 触摸模式:20 次唤醒(~1 分钟)后降频 */
#define POWEROFF_WAKEUP_PERIOD_SLOW_US (5000 * 1000)  /* 触摸模式待机轮询周期:5s */

/* 关机待机状态(跨深睡保留) */
RTC_DATA_ATTR static bool    s_poweroff_pending      = false;
RTC_DATA_ATTR static bool    s_poweroff_timed        = false;  /* true=定时模式(触摸无效,到点自动开机) */
RTC_DATA_ATTR static uint32_t s_poweroff_wake_secs   = 0;      /* 定时模式: 自动开机延时秒数(RTC 定时器一次睡) */
RTC_DATA_ATTR static uint8_t s_poweroff_hold_wakeups = 0;  /* 连续长按唤醒计数 */
RTC_DATA_ATTR static uint32_t s_poweroff_wakes       = 0;

/* 背光(GPIO43)拉低 + pad hold:深睡期间保持低电平,防浮空反亮。
 * 必须与 esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)
 * 搭配使用(见 board_poweroff_enter_deep_sleep):GPIO43 的 IO_MUX 复用
 * 配置位于 RTC 外设域,若 RTC_PERIPH 深睡掉电,唤醒后 IO_MUX 会复位成
 * 硅片默认 UART0 TXD(输出空闲高)+ hold 锁存丢失 → 背光反亮,关机页面
 * 残影整段待机可见(真机已见)。保持 RTC_PERIPH 供电 → IO_MUX 保持
 * GPIO-out 低电平配置,深睡期间与唤醒全程 pad 低,物理黑屏。
 * 5 次触摸开机时 board_backlight_hold_release() 释放后由正常 LEDC
 * 初始化接管。 */
static void board_backlight_low_hold(void)
{
    /* 参考 gc9a01 驱动 bltest:把引脚信号源从 LEDC 切回 GPIO out */
    esp_rom_gpio_pad_select_gpio(BOARD_GC9A01_BL_PIN);
    esp_rom_gpio_connect_out_signal(BOARD_GC9A01_BL_PIN, SIG_GPIO_OUT_IDX, 0, 0);
    gpio_set_direction(BOARD_GC9A01_BL_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_GC9A01_BL_PIN, 0);
    gpio_hold_en(BOARD_GC9A01_BL_PIN);
    gpio_deep_sleep_hold_en();
    ESP_LOGD(TAG, "Backlight GPIO%d held low for deep sleep", BOARD_GC9A01_BL_PIN);
}

static void board_backlight_hold_release(void)
{
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(BOARD_GC9A01_BL_PIN);
}

/* 深睡(不返回):失败则保持黑屏死循环重试,【绝不返回】。
 * 若返回,PM 状态仍是 DEEP_SLEEP,系统黑屏运行,任何触摸都会触发
 * _indev_pressed_cb -> eos_pm_wake_up() 假唤醒,用户看到
 * "按下关机却立即亮回系统"(像重启)。 */
static void _poweroff_sleep(uint64_t period_us)
{
    esp_sleep_enable_timer_wakeup(period_us);
    for (int fail = 0; ; fail++)
    {
        esp_deep_sleep_start();  /* 成功则永不返回 */
        ESP_LOGE(TAG, "Deep sleep start failed #%d (keep black, retry)", fail);
        if (fail >= 5)
        {
            /* 死循环黑屏等于"根本不开机":失败 5 次强制重启,恢复可操作 */
            ESP_LOGE(TAG, "Deep sleep start failed %d times -> force reboot", fail);
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* 进入关机深睡(由 board_power_set(DEV_POWER_STATE_OFF) 调用,不返回) */
static void board_poweroff_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Power off: enter deep sleep (%s)",
             s_poweroff_timed ? "timed, one-shot until deadline" : "touch hold-to-boot");
    if (!s_poweroff_timed)
        ESP_LOGI(TAG, "  wake every %u ms to poll touch (hold to boot)",
                 (unsigned)(POWEROFF_WAKEUP_PERIOD_US / 1000));

    s_poweroff_pending      = true;
    s_poweroff_hold_wakeups = 0;
    s_poweroff_wakes        = 0;

    /* 背光保持低(防深睡浮空反亮) */
    board_backlight_low_hold();

    /* 先整屏刷黑再 DISPOFF:消除每次唤醒 boot 窗口的"弱弱一闪"。
     * 唤醒 = 完整重启,ROM→bootloader 会解除 GPIO hold(CONFIG_ESP_SLEEP_
     * GPIO_RESET_WORKAROUND),GPIO43 短暂回到 UART0 TXD 高 → 背光反亮,
     * 透出关机页面残影(GRAM 保留最后一帧);app_main 早期 backlight_low_hold
     * 才压回黑,故每 1s 定时唤醒微闪一次。fill_black 把 GRAM 写纯黑后,
     * 背光反亮也无画面可透,微闪不可见。
     * 此前 fill_black 因与在途异步 flush 交错触发断言崩溃被禁;驱动现已有
     * display_drain_pending_tx() 排空保护(display_off/fill_black 内部调用),
     * 同步 SPI 发送安全,不会导致 esp_deep_sleep_start 失败。
     * 面板显示关闭(0x28 DISPOFF)仍保留:关面板刷新,防正常开机路径闪亮;
     * 面板常供电 + 无硬件 RST,寄存器状态跨深睡保持,开机 init(0x29)恢复。 */
    eos_dev_display_gc9a01_fill_black();
    eos_dev_display_gc9a01_display_off();

    /* 保持 RTC 外设域供电:GPIO43 的 IO_MUX 复用配置位于 RTC 域。
     * 默认深睡 RTC_PERIPH 掉电 → 唤醒时 IO_MUX 复位成 UART0 TXD 默认
     * (输出空闲高)、hold 锁存丢失 → 背光反亮,关机页面残影整段待机
     * 可见,且每次 1s 唤醒 boot 时 GPIO 重配造成闪烁(真机已见)。
     * 保持供电 → IO_MUX 保持 GPIO-out 低配置,深睡与唤醒全程 pad 低。 */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    /* 初始睡眠周期:触摸模式固定 3s;定时模式直接睡 wake_after_s 秒
     * (零中途唤醒,零微闪)。不依赖 esp_rtc_get_time_us() 的深睡连续
     * 性(唤醒后时基可能未恢复,曾导致"永远没到点"死循环不开机),
     * 按秒数设置 RTC 定时器一次睡满,唤醒即由 poll 判定开机。 */
    uint64_t period_us = POWEROFF_WAKEUP_PERIOD_US;
    if (s_poweroff_timed)
    {
        period_us = (uint64_t)s_poweroff_wake_secs * 1000000ULL;
        if (period_us == 0)
            period_us = 1000;  /* 0 秒兜底:睡 1ms 即醒 */
        ESP_LOGI(TAG, "Power off (timed): one-shot sleep %u s",
                 (unsigned)s_poweroff_wake_secs);
    }

    /* 注意:不配置 GPIO44 触摸唤醒——S3 深睡 GPIO 唤醒仅支持 RTC GPIO
     * (GPIO0-21),触摸 INT 在 GPIO44(非 RTC),深睡期间 GPIO 外设掉电,
     * gpio_wakeup_enable 配置的唤醒寄存器随之丢失,点击无法唤醒芯片。
     * 开机手势依赖周期性定时唤醒后轮询触摸(触摸模式);定时模式无需
     * 任何 GPIO 唤醒源,唤醒判定全靠 RTC 定时器。 */
    _poweroff_sleep(period_us);   /* 不返回 */
}

/* 关机待机轮询:深睡唤醒后调用。
 *  - 最小初始化(GPIO + I2C)。
 *  - 定时模式:触摸无效,定时器睡满自动开机(返回继续正常启动);
 *    一次睡满 wake_after_s 秒,零中途唤醒(零微闪)。
 *  - 触摸模式:读 CHSC6X,长按开机(任一唤醒读到触摸 ≈ 按住 3s);
 *    长时间无触摸自动降频。
 *  - 否则重新深睡(不返回)。
 * 注意:
 *  - 不做任何完整系统初始化(LVGL/SD/服务),保持最短路程回睡。
 *  - 开头无条件 board_backlight_low_hold() 兜底:即使 RTC_PERIPH 保电
 *    后 hold 意外失效(GPIO43 复位为 UART0 TXD 高反亮),也在最早的
 *    应用代码点压回低电平,消除"关机页面残影整段亮"。 */
static void board_poweroff_poll(void)
{
    s_poweroff_wakes++;

    /* 兜底:唤醒后无条件重新拉低背光 + hold(幂等;已低则无操作)。
     * 保证 poll 期间与回睡瞬间背光物理黑。 */
    board_backlight_low_hold();

    board_gpio_init();      /* 触摸 INT(GPIO44) 输入 */
    board_i2c_bus_init();   /* I2C:读 CHSC6X 需要 */

    /* 定时模式:触摸无效。本系统深睡唯一唤醒源就是 RTC 定时器(未配置
     * 任何 GPIO 唤醒),非定时器唤醒已被 app_main 拦截走正常启动分支;
     * 能走到这里 = 定时器睡满 wake_after_s 秒 → 直接开机。不依赖
     * esp_rtc_get_time_us() 判断(唤醒后时基可能未恢复,曾导致
     * now<deadline 永远重睡、根本不开机)。 */
    if (s_poweroff_timed)
    {
        ESP_LOGI(TAG, "Power off (timed): timer expired -> power on");
        s_poweroff_pending      = false;
        s_poweroff_hold_wakeups = 0;
        s_poweroff_wakes        = 0;
        s_poweroff_timed        = false;
        s_poweroff_wake_secs    = 0;
        board_backlight_hold_release();
        return;  /* 继续正常启动 */
    }

    /* 触摸模式:读 CHSC6X,唤醒瞬间触摸状态可能尚未稳定,短重试 */
    int32_t x = 0, y = 0;
    bool touched = false;
    for (int i = 0; i < 10 && !touched; i++) {
        touched = eos_dev_touch_chsc6x_read(&x, &y);
        if (!touched) vTaskDelay(pdMS_TO_TICKS(3));
    }

    /* 长按检测:连续唤醒读到触摸即"长按中"。周期 3s,N=1 → 手指按住,
     * 到某次唤醒(≤3s)读到触摸即开机。深睡下瞬间点击无法命中唤醒窗口,
     * 长按的持续状态可被定时轮询稳定捕获,是可靠的开机手势。 */
    if (touched) {
        s_poweroff_hold_wakeups++;
    } else {
        s_poweroff_hold_wakeups = 0;
    }

    if (s_poweroff_hold_wakeups >= POWEROFF_HOLD_WAKEUPS_TO_BOOT) {
        ESP_LOGI(TAG, "Power off: hold detected (x=%ld y=%ld) -> power on",
                 (long)x, (long)y);
        s_poweroff_pending      = false;
        s_poweroff_hold_wakeups = 0;
        s_poweroff_wakes        = 0;
        s_poweroff_timed        = false;
        s_poweroff_wake_secs    = 0;
        board_backlight_hold_release();
        return;  /* 继续正常启动 */
    }

    /* 再睡:长时间无触摸自动降频(刚关机响应快,长时间待机更省电) */
    uint64_t period_us = POWEROFF_WAKEUP_PERIOD_US;
    if (s_poweroff_wakes >= POWEROFF_SLOW_AFTER_WAKES)
        period_us = POWEROFF_WAKEUP_PERIOD_SLOW_US;
    ESP_LOGI(TAG, "Power off: no boot yet (hold=%u), sleep %u ms (wake #%u)",
             (unsigned)s_poweroff_hold_wakeups,
             (unsigned)(period_us / 1000),
             (unsigned)s_poweroff_wakes);
    _poweroff_sleep(period_us);   /* 不返回 */
}

/* ════════════════════════════════════════════════════════════════
 *  Power OPS:把 PM 服务的熄屏/亮屏状态接到 GC9A01 背光。
 *  DEV_POWER_STATE_ON -> 背光 100%;SLEEP/AOD -> 背光 0%。
 *  这是"手掌覆盖熄屏/双击亮屏"生效的前提(此前 ops 为 NULL,
 *  eos_service_pm._pm_set_state 只打错误日志,屏幕无任何变化)。
 *  DEV_POWER_STATE_OFF(关机) -> 进入硬件深睡(esp_deep_sleep_start,不返回)。
 * ════════════════════════════════════════════════════════════════ */
static int board_power_set(dev_power_state_t state)
{
    /* 关机:进入深睡轮询(不返回) */
    if (state == DEV_POWER_STATE_OFF) {
        board_poweroff_enter_deep_sleep();
        return 0;  /* esp_deep_sleep_start 不返回 */
    }

    eos_dev_display_t *disp = eos_dev_display_get_instance();
    if (!disp || !disp->ops || !disp->ops->power_on || !disp->ops->power_off)
    {
        ESP_LOGE(TAG, "Power set(%d) failed: display HAL not ready", (int)state);
        return -1;
    }
    if (state == DEV_POWER_STATE_ON)
        disp->ops->power_on();
    else
        disp->ops->power_off();
    return 0;
}

/* 关机模式参数(PM 服务在深睡前调用):存入 RTC_DATA_ATTR 跨深睡保留。
 * timed=1: 定时模式,触摸无效,存秒数,深睡一次睡满自动开机;
 * timed=0: 触摸模式,3s 周期轮询触摸,长按开机。 */
static int board_power_setoff_params(bool timed, uint32_t wake_after_s)
{
    s_poweroff_timed = timed;
    if (timed)
    {
        s_poweroff_wake_secs = wake_after_s;
        ESP_LOGI(TAG, "Power off (timed): wake after %u s (RTC timer one-shot)",
                 (unsigned)wake_after_s);
    }
    else
    {
        s_poweroff_wake_secs = 0;
        ESP_LOGI(TAG, "Power off (touch): hold to boot");
    }
    return 0;
}

static const eos_dev_power_ops_t s_board_power_ops = {
    .set_power           = board_power_set,
    .set_poweroff_params = board_power_setoff_params,
};

/* ════════════════════════════════════════════════════════════════
 *  板级驱动注册(对接 ElenixOS 设备 HAL)
 * ════════════════════════════════════════════════════════════════ */
static void board_drivers_register(void)
{
    /* GC9A01 / CHSC6X / BM8563 已是真实驱动 */
    eos_dev_display_gc9a01_register();
    eos_dev_rtc_bm8563_init();
    /* Power ops 必须在 eos_init()(PM 服务读取 instance)之前注册 */
    eos_dev_power_register(&s_board_power_ops);
    /* 电池:ADC1_CH0 单次模式 + 校准;设计容量 500mAh(service 默认同值) */
    eos_dev_battery_register(&s_board_battery_ops, 500);
    ESP_LOGI(TAG, "Drivers registered (GC9A01/CHSC6X/BM8563 real, power->backlight, battery->ADC)");
}

/* ════════════════════════════════════════════════════════════════
 *  ESP-IDF 入口
 * ════════════════════════════════════════════════════════════════ */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ElenixOS ESP32-S3 boot ===");
    ESP_LOGI(TAG, "Board: XIAO ESP32-S3 + Round Display 1.28\"");

    /* 0a. 关机待机轮询唤醒:背光 GPIO43 深睡前已 pad hold 低电平。
     * 轮询路径【不释放】hold——一旦 gpio_deep_sleep_hold_dis() 释放,
     * GPIO43 在"重新拉低+hold"之前浮空被板上拉反亮,残留关机画面
     * 每 3s 闪一次(真机已见,闪烁内容即关机页面残影;轮询周期改 3s 后频率大降)。
     * 关键保障:board_poweroff_enter_deep_sleep 里
     * esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)
     * 保持 RTC 域 IO_MUX 不复位(否则唤醒后 GPIO43 复位成 UART0 TXD
     * 高反亮);board_poweroff_poll 开头再无条件重新拉低兜底。
     * 触摸模式长按 / 定时模式睡满 → board_poweroff_poll 内部解除 hold
     * 并清标志,返回后继续正常启动;未满则重新深睡(不返回)。 */
    if (s_poweroff_pending) {
        ESP_LOGI(TAG, "Wake from poweroff: cause=%d pending=%d timed=%d wake_secs=%u wakes=%u",
                 (int)esp_sleep_get_wakeup_cause(), (int)s_poweroff_pending,
                 (int)s_poweroff_timed, (unsigned)s_poweroff_wake_secs,
                 (unsigned)s_poweroff_wakes);
        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
            board_poweroff_poll();  /* 不释放 hold;长按/定时到点 → 内部 release 后返回 */
        } else {
            /* 非定时器唤醒(USB 重刷/BOOT 键等):取消关机待机,正常启动 */
            ESP_LOGW(TAG, "Power off pending but wake cause=%d -> normal boot",
                     (int)esp_sleep_get_wakeup_cause());
            s_poweroff_pending      = false;
            s_poweroff_hold_wakeups = 0;
            s_poweroff_wakes        = 0;
            gpio_deep_sleep_hold_dis();
            gpio_hold_dis(BOARD_GC9A01_BL_PIN);
        }
    } else {
        /* 正常启动:解除深睡 pad hold(否则后续 LEDC/GPIO 对背光的操作无效) */
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis(BOARD_GC9A01_BL_PIN);
    }

    /* 0. 创建 FreeRTOS 任务——必须在任何 internal 大分配之前!
     * 原因:ui_task 会执行 flash 写(如配置保存→SPIFFS)。flash 写期间 CPU cache
     * 被禁用,PSRAM 栈不可访问 → esp_task_stack_is_sane_cache_disabled() 断言崩溃。
     * 因此 ui_task 栈必须分配在 internal RAM;且 eos_init() 之后 internal RAM
     * 碎片化(largest≈7KB),48KB 栈无法分配。必须在 app_main 最开头、驱动/
     * SPIFFS 等 internal 占用之前分配(实测 CHSC6X+SPIFFS 会把 largest 从
     * ~51KB 啃到 ~45KB,导致 49152B 栈分配失败)。
     * ui_task 通过 s_eos_ready 信号量等待 Core 就绪,不依赖驱动初始化顺序。
     * input/service/script 不执行 flash 写,栈留在 PSRAM(8MB 充足)。
     * BOARD_TASK_*_STACK 为 words 单位,WithCaps 版要求 bytes(×4)。 */
    ESP_LOGI(TAG, "Heap before tasks: DRAM free=%u (largest=%u), PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_eos_ready = xSemaphoreCreateBinary();
    BaseType_t r;
    r = xTaskCreatePinnedToCoreWithCaps(ui_task, "ui", BOARD_TASK_UI_STACK * 4, NULL,
                                        BOARD_TASK_UI_PRIO, NULL, BOARD_TASK_UI_AFFINITY,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (r != pdPASS) ESP_LOGE(TAG, "FAILED: ui task create failed (stack=%u)", (unsigned)BOARD_TASK_UI_STACK);
    r = xTaskCreatePinnedToCoreWithCaps(input_task, "input", BOARD_TASK_INPUT_STACK * 4, NULL,
                                        BOARD_TASK_INPUT_PRIO, NULL, BOARD_TASK_INPUT_AFFINITY, MALLOC_CAP_SPIRAM);
    if (r != pdPASS) ESP_LOGE(TAG, "FAILED: input task create failed");
    r = xTaskCreatePinnedToCoreWithCaps(service_task, "service", BOARD_TASK_SERVICE_STACK * 4, NULL,
                                        BOARD_TASK_SERVICE_PRIO, NULL, BOARD_TASK_SERVICE_AFFINITY, MALLOC_CAP_SPIRAM);
    if (r != pdPASS) ESP_LOGE(TAG, "FAILED: service task create failed");
    r = xTaskCreatePinnedToCoreWithCaps(script_task, "script", BOARD_TASK_SCRIPT_STACK * 4, NULL,
                                        BOARD_TASK_SCRIPT_PRIO, NULL, BOARD_TASK_SCRIPT_AFFINITY, MALLOC_CAP_SPIRAM);
    if (r != pdPASS) ESP_LOGE(TAG, "FAILED: script task create failed");

    /* 1. 板级 GPIO + I2C(SPI 由 GC9A01 驱动初始化) */
    board_gpio_init();
    esp_err_t i2c_ret = board_i2c_bus_init();
    if (i2c_ret == ESP_OK) {
        board_i2c_scan();  /* [DIAG] 扫描扩展板 I2C 设备,验证物理连接 */
    }

    /* 1.5 NVS 初始化(phy 校准数据 / BT 配对 / Wi-Fi 配置存储;
     * 必须在 BT/Wi-Fi early init 之前调用,否则每次启动全校准、
     * NimBLE IRK 存储失败) */
    {
        esp_err_t nvs_ret = nvs_flash_init();
        if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS full/version mismatch, erasing and re-init");
            nvs_flash_erase();
            nvs_ret = nvs_flash_init();
        }
        if (nvs_ret == ESP_OK) {
            ESP_LOGI(TAG, "NVS initialized (phy cal / BT / Wi-Fi storage)");
        } else {
            ESP_LOGW(TAG, "NVS init failed: %s (BT/Wi-Fi storage unavailable)",
                     esp_err_to_name(nvs_ret));
        }
    }

    /* 2. LVGL 核心初始化(必须在 display 注册前) */
    lv_init();

    /* 3. GC9A01 LCD 真实驱动初始化(含 SPI bus + panel + 背光 PWM) */
    esp_err_t ret = eos_dev_display_gc9a01_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GC9A01 init failed: %s", esp_err_to_name(ret));
        /* 不 return,继续启动(Shell 仍可用) */
    }

    /* 4. 注册 LVGL display(flush_cb → esp_lcd_panel_draw_bitmap) */
    eos_dev_display_gc9a01_lvgl_init();

    /* 4.5 CHSC6X 触摸:创建 LVGL POINTER indev(read timer 自动轮询)
     * 必须在 eos_init() 前创建,Core 侧 eos_touch_get_indev() 才能找到 */
    eos_dev_touch_chsc6x_init();

    /* 5. 注册板级驱动到 ElenixOS HAL */
    board_drivers_register();

    /* 5.5 BLE 提前初始化:必须在 eos_init() 之前(internal RAM 尚连续、
     * largest≈45KB)完成 NimBLE/controller 初始化——eos_init() 之后 largest
     * 只剩 ≈7KB,controller 的大块 internal|DMA 分配必然 ESP_ERR_NO_MEM
     * (表现为打开蓝牙开关时 init 失败)。失败不致命:开关仍会懒初始化重试。 */
    eos_bt_esp32_early_init();

    /* 5.6 Wi-Fi 提前初始化:与蓝牙同理(esp_wifi_init 的 static RX buffer
     * 需 internal DMA 连续内存)。内部带内存门控:largest 不足时自动跳过、
     * 保持懒初始化,绝不挤占 eos_init()/LVGL UI 加载的 internal 空间。 */
    eos_net_wifi_esp32_early_init();

    /* 6. Shell:USB-CDC(永远可用,不依赖 SD) */
    board_shell_usb_cdc_start();

    /* 6.5 文件系统:真 microSD 优先挂载 /sdcard,无卡回退 SPIFFS
     * (config/state/资源依赖,须在 eos_init 前) */
    board_fs_mount();

    /* 7. FreeRTOS 任务已在 app_main 最开头创建(步骤 0:internal 最完整时
     * 分配 48KB ui_task 栈,此时才能成功;驱动/SPIFFS 之后 largest 只有 ~45KB)。 */

    /* 8. ElenixOS Core 初始化(持 LVGL 递归锁,避免与 ui_task 并发竞态) */
    lv_lock();
    eos_init();
    lv_unlock();
    ESP_LOGI(TAG, "ElenixOS core initialized");
    /* 放行 ui_task(LVGL 渲染/事件循环开始) */
    if (s_eos_ready != NULL) {
        xSemaphoreGive(s_eos_ready);
    }
    /* 诊断:eos_init() 之后的 internal RAM 状态(验证阈值 8KB 分流效果) */
    ESP_LOGI(TAG, "Heap after eos_init: DRAM free=%u (largest=%u), PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_LOGI(TAG, "Boot complete (GC9A01/CHSC6X/BM8563 real)");
}
