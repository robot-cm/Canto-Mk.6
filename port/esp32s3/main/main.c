/**
 * @file main.c
 * @brief ElenixOS ESP32-S3 板级入口(XIAO ESP32-S3 + Round Display)
 *
 * 阶段:集成 GC9A01 真实驱动 + CHSC6X 真实触摸(SD/RTC 仍为 stub,阶段C 后续)
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
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
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
#include "eos_dev_power.h"
#include "eos_shell.h"
#include "eos_shell_framework.h"
#include <string.h>
#include "eos_dev_sensor.h"

static const char *TAG = "Board";

/* eos_init() 完成信号:ui_task 必须等 Core/UI 服务就绪后才开始 LVGL 渲染 */
static SemaphoreHandle_t s_eos_ready = NULL;

/* ════════════════════════════════════════════════════════════════
 *  非 LCD 设备 OPS stub(阶段C 后续替换为真实驱动)
 *  GC9A01 / CHSC6X / BM8563(RTC) 已是真实驱动
 *  battery/power/sensor 仍 stub
 * ════════════════════════════════════════════════════════════════ */
#if 0 /* TODO 阶段C: 取消注释以注册 */
static void _stub_battery_request_update(void)
{
    /* TODO 阶段C: ADC 读取 D0,analogReadMilliVolts,20 次均值 */
}

static void _stub_power_set(dev_power_state_t s) { (void)s; /* TODO: PMIC */ }
#endif

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
    TickType_t last = xTaskGetTickCount();
    uint32_t ticks = 0;
    for (;;) {
        /* 注意:lv_timer_handler() 内部自带 lv_lock/lv_unlock(递归锁),
         * 这里不再外层加锁,避免锁嵌套混乱 */
        lv_tick_inc(BOARD_LVGL_TICK_MS);
        /* 统一走 eos_main_loop():dispatch_tick + lv_timer_handler,并在开机动画
         * 完成后于此初始化 activity controller(主界面延迟显示的关键入口)。 */
        eos_main_loop();
        if ((++ticks % 200) == 0) {
            /* 心跳诊断:证明 ui_task 循环活着(200×5ms=1s 一次) */
            ESP_LOGI(TAG, "ui heartbeat: %u ticks, DRAM free=%u",
                     (unsigned)ticks,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        vTaskDelayUntil(&last, period);
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
 *  SPIFFS 挂载(/sdcard)— 模拟 SD 卡根目录
 *  eos_platform_config.h 定义 EOS_SYS_ROOT_DIR="/sdcard",
 *  eos_fs_realpath() 会把所有 POSIX 绝对路径(/.sys/... 等)
 *  自动映射到 /sdcard 下,故挂载此处即可覆盖 config/state/资源。
 *  首次挂载失败会自动格式化(format_if_mount_failed)。
 * ════════════════════════════════════════════════════════════════ */
static esp_err_t board_fs_mount(void)
{
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
        ESP_LOGI(TAG, "SPIFFS mounted: /sdcard (%u/%u KB)",
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    } else {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ════════════════════════════════════════════════════════════════
 *  板级驱动注册(对接 ElenixOS 设备 HAL)
 * ════════════════════════════════════════════════════════════════ */
static void board_drivers_register(void)
{
    /* GC9A01 / CHSC6X / BM8563 已是真实驱动 */
    eos_dev_display_gc9a01_register();
    eos_dev_rtc_bm8563_init();
    /* TODO 阶段C: eos_dev_battery_register / power / sensor */
    ESP_LOGI(TAG, "Drivers registered (GC9A01/CHSC6X/BM8563 real)");
}

/* ════════════════════════════════════════════════════════════════
 *  ESP-IDF 入口
 * ════════════════════════════════════════════════════════════════ */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ElenixOS ESP32-S3 boot ===");
    ESP_LOGI(TAG, "Board: XIAO ESP32-S3 + Round Display 1.28\"");

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

    /* 6. Shell:USB-CDC(永远可用,不依赖 SD) */
    board_shell_usb_cdc_start();

    /* 6.5 SPIFFS 挂载到 /sdcard(config/state/资源依赖,须在 eos_init 前) */
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
