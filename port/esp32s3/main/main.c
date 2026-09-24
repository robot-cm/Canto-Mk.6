/**
 * @file main.c
 * @brief CantoMk6 ESP32-S3 板级入口(XIAO ESP32-S3 + Round Display)
 *
 * 阶段:集成 GC9A01 真实驱动 + CHSC6X 真实触摸 + microSD(SDSPI 真 SD,
 * 优先挂载 /sdcard,无卡回退 SPIFFS;RTC 为 BM8563 真实驱动)。
 *   - SPI/I2C 总线初始化(SPI 由 GC9A01 驱动初始化,I2C 板级初始化)
 *   - GC9A01 LCD 真实驱动:init + register HAL + LVGL display
 *   - CHSC6X 触摸:LVGL POINTER indev(read timer 自动轮询)
 *   - FreeRTOS 任务:ui_task 调 lv_timer_handler
 *   - Shell:USB-CDC stub(D6/D7 被屏占)
 *   - 调用 cos_init()
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
/* 关机深睡:esp_deep_sleep_start / esp_sleep_get_wakeup_cause /
 * esp_sleep_enable_timer_wakeup;深睡期间用 pad hold 保持背光为低
 * (GPIO43 非 RTC GPIO,浮空会被板上拉反亮,见 board_backlight_low_hold) */
#include "esp_sleep.h"
#include "esp_timer.h" /* 真实毫秒基准(跨 Light Sleep 连续,IDF 内建补偿) */
#include "esp_rom_gpio.h"
#include "esp_pm.h" /* 智能模式默认 CPU profile:esp_pm_configure DFS 80-160MHz */
#include "soc/gpio_sig_map.h"
/* microSD(SDSPI,与 LCD 共用 SPI3 总线,见 board_sd_mount) */
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_dma_utils.h" /* esp_dma_is_buffer_alignment_satisfied 诊断 */
/* USB MSC App 的板级能力接口(SD 释放/恢复、卡句柄、JTAG 在线、PM 保持) */
#include "cos_usb_msc_board.h"
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
#include "ff.h" /* f_mount/FRESULT:SD 重新挂载(MSC 退出恢复)用 */
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
/* xTaskCreatePinnedToCoreWithCaps:任务栈可指定内存 cap(见下方任务创建) */
#include "freertos/idf_additions.h"

/* LVGL 9.x */
#include "lvgl.h"

/* CantoMk6 Core(平台无关) */
#include "cantomk6_os.h"
#include "cos_port.h"
#include "kernel/scheduler/cos_dispatcher.h" /* cos_dispatch_tick() 异步回调队列 */
#include "cos_dev_display.h"
#include "cos_dev_display_gc9a01.h"
#include "cos_dev_touch_chsc6x.h"
#include "cos_dev_time.h"
#include "cos_dev_rtc_bm8563.h"
#include "cos_dev_battery.h"
#include "cos_service_battery.h" /* cos_battery_raw_t / cos_battery_report_raw */
#include "cos_dev_power.h"
#include "cos_shell.h"
#include "cos_shell_framework.h"
#include "cos_service_config.h" /* cos_config_get_bool / COS_CONFIG_KEY_DEV_MODE_BOOL */
#include "cos_service_cc_snapshot.h" /* /sdcard/history/cc 四项设置快照 */
#include "cos_service_pm.h"     /* cos_pm_get_state():L1 Light Sleep / L2 待机判定 */
#include "framework/activity/cos_activity.h" /* L2 待机前快照当前 UI(恢复用) */
#include "framework/app/cos_app_list.h"     /* cos_app_list_get_last_launch_app_id */
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>   /* mkdir():/sdcard/history/deepsleep 历史目录 */
#include "cos_dev_sensor.h"

static const char *TAG = "Board";

/* cos_bt_esp32.c 导出:在 cos_init() 之前、internal RAM 尚连续时预初始化
 * NimBLE(controller 需 internal|DMA 大块连续内存,见 cos_bt_esp32.c 注释) */
extern void cos_bt_esp32_early_init(void);
#include "cos_net_wifi_esp32.h" /* cos_net_wifi_esp32_early_init():带内存门控 */

/* cos_init() 完成信号:ui_task 必须等 Core/UI 服务就绪后才开始 LVGL 渲染 */
static SemaphoreHandle_t s_cos_ready = NULL;

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
        /* ESP32-S3 只有 curve fitting 校准方案(依赖出厂 eFuse),无 line fitting
         * (后者是经典 ESP32 的 API)。config 带 .chan,须与上/下文通道一致。 */
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id  = ADC_UNIT_1,
            .chan     = BOARD_BATTERY_ADC_CH,
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        /* 创建失败时 s_batt_cali 保持 NULL → request_update 走线性估算兜底 */
        esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_batt_cali);
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

/* VBUS/USB 在位检测(XIAO 普通版无充电状态 GPIO)。
 * usb_serial_jtag_is_connected() 由 IDF 在启用 USB 串行控制台
 * (CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED)时才编译提供;本树默认控制台
 * 走 UART,该驱动未编入。未启用时返回 false —— 宁可不显示充电,也不误报。 */
static bool _board_vbus_present(void)
{
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED)
    extern bool usb_serial_jtag_is_connected(void);
    return usb_serial_jtag_is_connected();
#else
    return false;
#endif
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
     * 时视为已充满(充满后充电 IC 停充,不再显示绿色)。 */
    bool usb_in   = _board_vbus_present();
    bool charging = usb_in && (mv >= 0) && (mv < BOARD_BATTERY_FULL_MV);

    cos_battery_raw_t raw = {
        .percent    = (int8_t)percent,
        .voltage_mv = (int16_t)mv,
        .current_ma = -1,
        .charging   = charging,
    };
    cos_battery_report_raw(&raw);
}

static const cos_battery_dev_ops_t s_board_battery_ops = {
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

/* 前向声明(实现均在文件后部的"关机/待机 Deep Sleep 骨架"附近):
 * ui_task/board_pm_step 在定义之前调用,必须先声明且带 static,
 * 否则 C 会按隐式(非 static)声明处理,后面 static 定义直接冲突。 */
static void board_pm_step(void);                  /* L1/L2 主状态机单步(ui_task 内) */
static void board_standby_enter_deep_sleep(void); /* L2 进入:固化历史后复用关机深睡骨架 */
static void board_standby_clock_gate(void);       /* L2 唤醒极简时钟(8s/5 击),超时回深睡 */
static void board_standby_try_restore_ui(void);   /* 完整启动后恢复深睡前 App(尽力而为) */

/* ════════════════════════════════════════════════════════════════
 *  FreeRTOS 任务
 * ════════════════════════════════════════════════════════════════ */

static void ui_task(void *arg)
{
    (void)arg;
    /* 等待 cos_init() 完成(Core/UI 服务就绪)后才开始 LVGL 渲染
     * (任务在 cos_init() 之前创建,见 app_main 中注释) */
    if (s_cos_ready != NULL) {
        xSemaphoreTake(s_cos_ready, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "ui_task started (LVGL %dx%d)", BOARD_GC9A01_WIDTH, BOARD_GC9A01_HEIGHT);
    const TickType_t period = pdMS_TO_TICKS(BOARD_LVGL_TIMER_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    int64_t last_tick_us = esp_timer_get_time(); /* 真实毫秒基准(Light Sleep 连续) */
    uint32_t ticks = 0;
    for (;;) {
        /* 注意:lv_timer_handler() 内部自带 lv_lock/lv_unlock(递归锁),
         * 这里不再外层加锁,避免锁嵌套混乱 */
        /* tick 必须前进真实经过的时间:用 esp_timer(毫秒)差分而非固定增量。
         * 历史 bug:每 5ms 循环只 lv_tick_inc(1) → 所有 LVGL timer 慢 5 倍
         * (JS lv.timer 1000ms 倒计时实际 5s,Breach 失准)。
         * 电池优化:board_pm_step() 会单步 Light Sleep(阻塞最长 500ms),期间
         * FreeRTOS tick 冻结——若沿用 tick 差分,LVGL 计时会显著漂慢(闹钟/
         * 双击窗口失效)。esp_timer 由 ESP-IDF 在 Light Sleep 内保持连续,故改用它。 */
        int64_t now_us = esp_timer_get_time();
        lv_tick_inc((uint32_t)((now_us - last_tick_us) / 1000));
        last_tick_us = now_us;
        /* 统一走 cos_main_loop():dispatch_tick + lv_timer_handler,并在开机动画
         * 完成后于此初始化 activity controller(主界面延迟显示的关键入口)。 */
        cos_main_loop();
        /* 电池优化:软件熄屏(SLEEP)时在此单步硬件 Light Sleep,并在无触摸累计
         * 达 L2 阈值(默认 15min,省电模式收紧到 10min)后自动转 L2 待机
         * (Deep Sleep,极简时钟唤醒,不返回)。
         * 注意:Light Sleep 期间本任务休眠,其余任务一并暂停,唤醒即恢复。 */
        board_pm_step();
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
    /* 触摸已由 LVGL POINTER indev 的 read timer 驱动(cos_dev_touch_chsc6x_init),
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
 *  cos_shell_framework_feed(),命令输出写回该口,即可直接敲命令
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

/* 运行时 DEV 开关持有的 shell 监听 task 句柄(NULL=未运行) */
static TaskHandle_t s_shell_usb_task = NULL;

static void shell_usb_line_handler(const char *line, void *user)
{
    (void)user;
    cos_shell_exec(line, shell_usb_out, NULL);
    cos_shell_framework_prompt(shell_usb_echo, NULL);
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
        cos_shell_framework_feed((char)c, shell_usb_echo, shell_usb_line_handler, NULL);
    }
}

static void board_shell_usb_set_enabled(bool enabled, void *user)
{
    (void)user;
    if (enabled)
    {
        if (s_shell_usb_task != NULL)
            return; /* already running (idempotent) */
        /* 运行时开启:创建阻塞式 USB-Serial-JTAG 监听 task,用户可在 idf.py
         * monitor 里敲命令。shell 只读写 USB 口,不碰 flash 写 → 栈放 PSRAM,
         * 别挤 internal RAM (cos_init 前 internal 必须为 ui_task 保留
         * 48KB+TCB 连续块)。 */
        BaseType_t r = xTaskCreatePinnedToCoreWithCaps(shell_usb_task, "shell_usb", 4096, NULL, 5,
                                                       &s_shell_usb_task, BOARD_TASK_INPUT_AFFINITY,
                                                       MALLOC_CAP_SPIRAM);
        if (r == pdPASS)
            ESP_LOGI(TAG, "Shell: USB-Serial-JTAG console ENABLED — type 'help' in idf.py monitor");
        else
        {
            s_shell_usb_task = NULL;
            ESP_LOGE(TAG, "Shell: USB-Serial-JTAG task create failed");
        }
    }
    else
    {
        if (s_shell_usb_task == NULL)
            return; /* already stopped (idempotent) */
        ESP_LOGI(TAG, "Shell: USB-Serial-JTAG console DISABLED");
        vTaskDelete(s_shell_usb_task);
        s_shell_usb_task = NULL;
    }
}

/* DEV 开关默认值:跟随构建模式(Dev 构建开 / Release 构建关) */
#if defined(COS_BUILD_RELEASE) && COS_BUILD_RELEASE
#define BOARD_DEV_MODE_BOOT_DEFAULT false
#else
#define BOARD_DEV_MODE_BOOT_DEFAULT true
#endif

static void board_shell_usb_cdc_start(void)
{
    cos_shell_framework_init();
    /* 运行时 DEV 开关(Control Center "DEV" 按钮):壳监听 task 由框架回调
     * 启停,串口日志级别随状态自动切换(ON->DEBUG, OFF->静默)。
     * 此处先按编译默认应用一次(确保 task 状态与框架一致);
     * 持久化配置在文件系统挂载后由 board_apply_dev_mode_config() 覆盖。 */
    cos_shell_framework_set_console_ctl(board_shell_usb_set_enabled, NULL);
    cos_shell_framework_set_console_enabled(BOARD_DEV_MODE_BOOT_DEFAULT);
}

/* 文件系统就绪后应用持久化的 DEV 开关(Control Center 保存的 dev_mode) */
static void board_apply_dev_mode_config(void)
{
    cos_shell_framework_set_console_enabled(
        cos_config_get_bool(COS_CONFIG_KEY_DEV_MODE_BOOL, BOARD_DEV_MODE_BOOT_DEFAULT));
}

/* ════════════════════════════════════════════════════════════════
 *  microSD 挂载(/sdcard)— 真 SD 优先,SPIFFS 兜底
 *  microSD 与 LCD 共用 SPI3 总线(SCK=D8/MOSI=D10/MISO=D9),SD CS=D2=GPIO3:
 *   - SPI bus 已由 cos_dev_display_gc9a01_init() 完成 spi_bus_initialize
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
static esp_err_t board_sd_get_dma_info(int slot, esp_dma_mem_info_t *dma_mem_info)
{
    (void)slot;
    dma_mem_info->extra_heap_caps = MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM;
    dma_mem_info->dma_alignment_bytes = 4;
    return ESP_OK;
}

/* ── USB MSC App 支持(全部为新增,用 CONFIG_USB_MSC_APP_ENABLE 包裹) ──
 * 只新增静态状态与 5 个查询/控制函数,不修改任何已有挂载/电源逻辑的分支行为。 */
/* 真 SD(非 SPIFFS 回退)—— 不依赖 USB MSC 配置:控制中心设置快照
 * (/sdcard/history/cc)需要在两种构建下都能判断"是否真卡"。 */
static bool s_sd_is_real = false;

bool board_sd_is_real(void)
{
    return s_sd_is_real;
}

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
static sdmmc_host_t               s_sd_host;        /* 挂载后含 SPI 设备句柄 */
static sdspi_device_config_t      s_sd_slot_cfg;
static esp_vfs_fat_mount_config_t s_sd_mount_cfg;
static sdmmc_card_t              *s_sd_card       = NULL;
static bool                       s_sd_vfs_mounted = false;
static bool                       s_usb_msc_hold   = false; /* MSC 会话电源保持 */

sdmmc_card_t *board_sd_get_card(void)
{
    return s_sd_card;
}

bool board_usb_serial_jtag_connected(void)
{
    /* 复用已有探测:控制台为 USB-Serial-JTAG 时才返回真实连接状态 */
    return _board_vbus_present();
}

void board_pm_usb_msc_hold(bool hold)
{
    s_usb_msc_hold = hold;
}

bool board_pm_usb_msc_is_held(void)
{
    return s_usb_msc_hold;
}

/* ── Spotify App 支持(全部为新增,用 CONFIG_USB_UAC_APP_ENABLE 包裹) ──
 * Spotify(UAC 耳机播放器)与 USB MSC 共用同一套板级能力:
 *   board_sd_is_real / board_sd_get_card / board_sd_release / board_sd_acquire /
 *   board_usb_serial_jtag_connected 直接复用(已在上方实现);
 * 下面只补充 Spotify 特有的两个薄封装,不修改任何既有逻辑分支。 */
#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE
static bool s_spotify_pm_hold = false; /* UAC 会话电源保持 */

bool board_spotify_usj_online(void)
{
    /* 与 board_usb_serial_jtag_connected() 语义相同:USB-Serial-JTAG 是否在线 */
    return _board_vbus_present();
}

void board_spotify_pm_hold(bool hold)
{
    s_spotify_pm_hold = hold;
}

bool board_spotify_pm_is_held(void)
{
    return s_spotify_pm_hold;
}
#endif /* CONFIG_USB_UAC_APP_ENABLE */

/* 让 FATFS 让位给 MSC:只卸载 FATFS 卷 + 注销 VFS 挂载点,
 * 刻意保留 sdmmc_card_t 与 SDSPI 设备,供 MSC 直读裸扇区。
 *
 * 【不能用 esp_vfs_fat_sdcard_unmount()】IDF 的 unmount_card_core()
 * (components/fatfs/vfs/vfs_fat_sdmmc.c) 会依次执行:
 *     f_mount(0, drv, 0);
 *     ff_diskio_unregister(pdrv);
 *     call_host_deinit(&card->host);   // sdspi_host_deinit → spi_bus_remove_device
 *     free(card);                      // 释放 sdmmc_card_t
 * 即 card 被释放、SPI 设备句柄失效。之后 MSC 回调再访问 s_card
 * (csd.capacity / sdmmc_read_sectors)就是 use-after-free + 已失效的 SPI 句柄,
 * PC 一读卡立即 panic 复位;而此刻 TinyUSB 已接管 USB PHY、console 已停,
 * panic 日志打不出来 —— 表现为「打开 App 就重启、无错误码、无日志」。
 *
 * esp_vfs_fat_unregister_path() 只释放 VFS/FATFS 上下文,不碰 card/host/diskio,
 * 因此随后 board_sd_acquire() 仍可用同一个 pdrv 重新 f_mount。 */
esp_err_t board_sd_release(void)
{
    if (!s_sd_is_real || s_sd_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_sd_vfs_mounted) {
        return ESP_OK;
    }

    (void)f_mount(NULL, "0:", 0);          /* 先卸载 FATFS 卷,解除对 FATFS 对象的引用 */

    esp_err_t err = esp_vfs_fat_unregister_path("/sdcard");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_unregister_path failed: %s", esp_err_to_name(err));
        return err;
    }
    s_sd_vfs_mounted = false;
    ESP_LOGI(TAG, "SD released to USB MSC (card+SPI host kept alive)");
    return ESP_OK;
}

/* MSC 结束:重新挂载 FATFS/VFS(不重新初始化 SPI 设备,避免句柄冲突) */
esp_err_t board_sd_acquire(void)
{
    if (!s_sd_is_real || s_sd_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sd_vfs_mounted) {
        return ESP_OK;
    }

    FATFS *fs = NULL;
    esp_err_t err = esp_vfs_fat_register("/sdcard", "0:", s_sd_mount_cfg.max_files, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_register failed: %s", esp_err_to_name(err));
        return err;
    }
    FRESULT fr = f_mount(fs, "0:", 1);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "f_mount failed: %d", (int)fr);
        esp_vfs_fat_unregister_path("/sdcard");
        return ESP_FAIL;
    }
    s_sd_vfs_mounted = true;
    ESP_LOGI(TAG, "SD re-mounted after USB MSC");
    return ESP_OK;
}
#endif /* CONFIG_USB_MSC_APP_ENABLE */

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

    /* 真卡标志(控制中心设置快照的落盘前提,与 USB MSC 是否编译无关) */
    s_sd_is_real = true;

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    /* 记录 SD 状态供 USB MSC App 使用(host 此时已写入 SPI 设备句柄) */
    s_sd_host         = host;
    s_sd_slot_cfg     = slot_config;
    s_sd_mount_cfg    = mount_config;
    s_sd_card         = card;
    s_sd_vfs_mounted  = true;
#endif

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
 *  cos_platform_config.h 定义 COS_SYS_ROOT_DIR="/sdcard",
 *  cos_fs_realpath() 会把所有 POSIX 绝对路径(/.sys/... 等)
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
/* 电池优化(用户确认):关机的触摸唤醒轮询周期 3s→8s,约 1 分钟后的降频慢周期
 * 5s→16s。Deep Sleep ~44µA 下,每次唤醒几十 ms@几十 mA,周期越长平均电流越低;
 * 手势语义不变:任一唤醒读到触摸即开机(等效长按 8s/16s,倒计时模式不受影响)。 */
#define POWEROFF_HOLD_WAKEUPS_TO_BOOT  1   /* 触摸模式:连续 N 次唤醒读到触摸即开机(周期 8s,N=1 ≈ 按住 8s) */
#define POWEROFF_WAKEUP_PERIOD_US      (8000 * 1000)  /* 触摸模式轮询周期:8s(原 3s) */
#define POWEROFF_SLOW_AFTER_WAKES      20  /* 触摸模式:20 次唤醒(~2.7 分钟)后降频 */
#define POWEROFF_WAKEUP_PERIOD_SLOW_US (16000 * 1000) /* 触摸模式待机轮询周期:16s(原 5s) */

/* ═══ 电池优化:L1 硬件轻睡 + L2 自动待机(15min) ═══
 * L1(短时未使用):PM 软件熄屏(SLEEP,默认 10s 无操作,超时在 PM 服务)后进
 *   ESP32-S3 硬件 Light Sleep —— board_pm_step() 单步:500ms RTC 定时唤醒推进
 *   LVGL 计时(esp_timer 跨轻睡连续,闹钟/双击窗口不失准)+ 触摸 INT(GPIO44)
 *   低电平即时唤醒。双击亮屏仍由 UI(PM 服务)判定;板级保证触摸后 600ms 内
 *   CPU 清醒(PM 双击窗口 400ms),两次 press 都落在真实运行期,不吞击。
 * L2(长时间未使用):L1 期间无触摸累计 15min → 自动待机 Deep Sleep
 *   (board_standby_enter_deep_sleep),复用关机 8s/16s 触摸轮询骨架;长按唤醒
 *   后先显示"极简时钟"8s(仅 LVGL 渲染),8s 内连续 5 击 → 真正唤醒主系统
 *   (跳过 Boot Anim 快速恢复);否则 8s 后自动回 Deep Sleep。 */
#define BOARD_L1_LS_PERIOD_US       (500 * 1000)      /* L1 Light Sleep 周期唤醒:500ms */
#define BOARD_L1_ACT_GUARD_MS       600               /* 触摸后保持 CPU 清醒窗口(> PM 双击窗口 400ms) */
#define BOARD_L2_IDLE_MS            (15 * 60 * 1000)  /* 熄屏累计无触摸 15min → L2 自动待机 Deep Sleep(省电模式经 cos_pm_set_l2_idle_ms 运行时收紧到 10min) */
#define BOARD_STANDBY_CLOCK_MS      8000              /* L2 长按唤醒后极简时钟窗口 8s;超时自动回 Deep Sleep */
#define BOARD_CLOCK_TAP_GAP_MS      500               /* 极简时钟 5 击:相邻两击间隔上限,超限重新计数 */
#define BOARD_CLOCK_TAPS_TO_BOOT    5                 /* 极简时钟内连续 5 击 → 真正唤醒主系统 */
#define BOARD_CLOCK_TEXT_LEN        16

/* 极简时钟(240x240 圆屏)三行居中排版:时间 / 日期 / 星期英文缩写。
 * 行盒高度直接取 Montserrat 字体的 line_height(montserrat_40 = 44px,
 * montserrat_28 = 30px,见 third_party/lvgl/src/font/lv_font_montserrat_40.c
 * 与 lv_font_montserrat_28.c 的 line_height 字段),数字与大写字母
 * 的墨迹在行盒内上下留白基本对称(font40: 上 7 下 8;font28: 上 5 下 5),
 * 所以按行盒排布即为光学居中:
 *
 *   行 1 时间 44px   60 .. 104   (font 40, "HH:MM")
 *       间隔 8px
 *   行 2 日期 30px  112 .. 142   (font 28, "MM/DD")
 *       间隔 8px
 *   行 3 星期 30px  150 .. 180   (font 28, "WED")
 *
 * 合计 44+8+30+8+30 = 120px,(240-120)/2 = 60 → 整块重心正好落在圆心
 * (120,120),上下留白均为 60px。
 *
 * 圆屏越界校验(半径 120,圆心 (120,120);半弦 = sqrt(120^2 - dy^2)):
 *   行 1 最远 dy = 60 → 半弦 104px,最宽 "44:44" = 116px(半宽 58)   ✔
 *   行 2 最远 dy = 22 → 半弦 118px,最宽 "44/44" =  85px(半宽 43)   ✔
 *   行 3 最远 dy = 60 → 半弦 104px,最宽 "WED"   =  74px(半宽 37)   ✔
 * 三行余量都在 45px 以上,字号维持现状即可,不会压出圆形可视区。
 * 标签用 LV_ALIGN_TOP_MID 定位 → 这里给的是"行盒顶边"的 y。 */
#define BOARD_CLOCK_TIME_Y          60
#define BOARD_CLOCK_DATE_Y          112
#define BOARD_CLOCK_WEEK_Y          150

/* 待机(L2)跨深睡状态:与手动关机(s_poweroff_*)互斥 */
RTC_DATA_ATTR static bool    s_standby_pending       = false; /* true=待机唤醒后先进极简时钟,5 击才进主系统 */
RTC_DATA_ATTR static uint8_t s_standby_hold_wakeups  = 0;     /* 待机长按唤醒计数 */
RTC_DATA_ATTR static uint32_t s_standby_wakes        = 0;     /* 待机轮询次数(降频) */
RTC_DATA_ATTR static char    s_standby_restore_ui[BOARD_CLOCK_TEXT_LEN] = "watchface"; /* 恢复目标:app id 或主界面 */
RTC_DATA_ATTR static bool    s_standby_boot_restore = false; /* 极简时钟 5 击后置位:本次启动结束后恢复深睡前 UI */

/* 关机待机状态(跨深睡保留) */
RTC_DATA_ATTR static bool    s_poweroff_pending      = false;
RTC_DATA_ATTR static bool    s_poweroff_timed        = false;  /* true=定时模式(触摸无效,到点自动开机) */
RTC_DATA_ATTR static uint32_t s_poweroff_wake_secs   = 0;      /* 定时模式: 自动开机延时秒数(RTC 定时器一次睡) */
RTC_DATA_ATTR static uint8_t s_poweroff_hold_wakeups = 0;  /* 连续长按唤醒计数 */
RTC_DATA_ATTR static uint32_t s_poweroff_wakes       = 0;

/* L1 实时状态(普通 static:Light Sleep 不重启;Deep Sleep 重启后自然复位) */
static int64_t s_l1_last_act_us = 0; /* 最后触摸/活动时刻(esp_timer us);超过 guard 才进 Light Sleep */

/* L2 自动待机阈值(默认 15min,见 BOARD_L2_IDLE_MS)。省电模式可运行时收紧/
 * 恢复(见 cos_pm_set_l2_idle_ms)。普通 static:深睡重启后复位回默认值。 */
static uint32_t s_l2_idle_ms = BOARD_L2_IDLE_MS;

/* 供上层(cos_service_power_save)经 extern 调用:查询/设置 L2 待机阈值。 */
uint32_t cos_pm_get_l2_idle_ms(void)
{
    return s_l2_idle_ms;
}

void cos_pm_set_l2_idle_ms(uint32_t idle_ms)
{
    if (idle_ms < 1000)
    {
        ESP_LOGW(TAG, "PM: invalid L2 idle %u ms ignored", (unsigned)idle_ms);
        return;
    }
    s_l2_idle_ms = idle_ms;
    ESP_LOGI(TAG, "PM: L2 auto-standby idle -> %u s", (unsigned)(s_l2_idle_ms / 1000));
}

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
 * _indev_pressed_cb -> cos_pm_wake_up() 假唤醒,用户看到
 * "按下关机却立即亮回系统"(像重启)。 */
static void _poweroff_sleep(uint64_t period_us)
{
    /* 清掉 L1/Light Sleep 及此前深睡残留的全部 wakeup 源,只保留本次
     * RTC 定时器唤醒:
     *  - L1 的 gpio_wakeup_enable(GPIO44)+esp_sleep_enable_gpio_wakeup()
     *    从不撤销,配置保留在 esp_sleep 内部;GPIO44 非 RTC GPIO,
     *    Deep Sleep 不支持,残留会导致 esp_deep_sleep_start() 失败
     *    (立即返回 → 重试 5 次 → esp_restart,表现"入睡即重启"——
     *    ee57376 无 L1,故此前正常)。
     *  - L1 的 500ms 定时唤醒同样会覆盖/污染本次深睡周期。 */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(period_us);
    for (int fail = 0; ; fail++)
    {
        esp_deep_sleep_start();  /* 成功则永不返回;失败返回 */
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

    /* 深睡前把控制中心四项设置(亮度/蓝牙/WiFi/电源模式)固化到 SD:
     * 深睡/关机是"整机断电"语义,延迟写(deferred writer)可能还没落卡。
     * 无真 SD 卡时空操作(回退 cfg.json),绝不阻塞关机流程。 */
    cos_cc_snapshot_capture_now();

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
    cos_dev_display_gc9a01_fill_black();
    cos_dev_display_gc9a01_display_off();

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
        touched = cos_dev_touch_chsc6x_read(&x, &y);
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
 *  L1 / L2 电池优化状态机(Light Sleep + 自动待机 Deep Sleep)
 * ════════════════════════════════════════════════════════════════
 *
 *  L1:PM 进入 SLEEP(熄屏,10s 无触摸)后,board_pm_step() 让整个芯片
 *      Light Sleep(500ms RTC 定时 + 触摸 INT(GPIO44) 低电平即时唤醒)。
 *      双击唤醒仍由 UI(PM 服务)判定;板级只在触摸/GPIO 唤醒后把
 *      s_l1_last_act_us 更新,保证 600ms guard 内 CPU 清醒,两次 press
 *      都落在真实运行期(LVGL 定时器仍推进,不吞双击)。
 *
 *  L2:Light Sleep 累计无触摸 15min → 自动待机 Deep Sleep。深睡期间
 *      GPIO44 无法唤醒(S3 仅 RTC GPIO0~21 可深睡唤醒),故复用关机骨架:
 *      8s/16s RTC 周期唤醒 → 读 CHSC6X 检测"长按"。长按被识别后不直接
 *      进主系统,而是先在 cos_init() 之前显示 8s "极简时钟"(纯 LVGL,
 *      时间读 RTC 设备,服务/JS/网络全未启动,极低开销);8s 内连续 5 击
 *      (相邻间隔 ≤500ms)→ 真正唤醒:清待机标志、跳过 Boot Anim、恢复
 *      深睡前使用的 App(/sdcard/history/deepsleep/latest_ui.txt 与
 *      s_standby_restore_ui 快照);否则 8s 到点自动回 Deep Sleep。
 *
 *  关机(s_poweroff_*)与待机(s_standby_*)共用同一"深睡+触摸轮询"骨架,
 *  靠 RTC_DATA_ATTR 标志区分唤醒后行为:关机=长按直接开机;
 *  待机=长按后先进极简时钟(5 击再决定)。互斥,不会同时置位。
 */

/* 前台若是 App(非表盘/启动器/锁屏等主界面),返回其可恢复 app id;
 * 否则返回 NULL(→ 恢复为表盘主界面)。仅作待机历史快照用。
 * 注意:运行期(ui_task)才能调用;极简时钟 gate 在 cos_init 之前,
 * 不需要也不允许调用本函数。 */
static const char *board_standby_foreground_app_id(void)
{
    cos_activity_t *top = cos_activity_get_current();
    if (!top)
        return NULL;
    cos_activity_type_t t = cos_activity_get_type(top);
    /* App 或 App 内部页(输入页/子页等)→ 用最近一次启动的 App id;
     * 表盘 / App 列表 / 表盘列表 / 锁屏均视为主界面,不恢复。 */
    if (t == COS_ACTIVITY_TYPE_APP || t == COS_ACTIVITY_TYPE_INPUT_PAGE) {
        const char *id = cos_app_list_get_last_launch_app_id();
        if (id && id[0] && strcmp(id, "watchface") != 0)
            return id;
    }
    return NULL;
}

/* 尽力写入 /sdcard/history/deepsleep/latest_ui.txt(SD 缺失/写失败静默) */
static void board_standby_write_history(void)
{
    if (mkdir("/sdcard/history", 0755) != 0) { /* 已存在或失败都不致命 */ }
    if (mkdir("/sdcard/history/deepsleep", 0755) != 0) { }

    FILE *f = fopen("/sdcard/history/deepsleep/latest_ui.txt", "w");
    if (!f) {
        ESP_LOGD(TAG, "Standby: cannot write history (SD not ready?)");
        return;
    }
    fprintf(f, "%s\n", s_standby_restore_ui);
    fclose(f);
}

/* L2 待机入口(从 board_pm_step 调用,ui_task 上下文,不返回):
 * 先把"深睡前前台 UI"固化到 RTC 变量 + SD 历史文件,再复用关机深睡
 * 骨架(背光 pad hold 拉低 + fill_black + DISPOFF + 8s/16s 触摸轮询)。 */
static void board_standby_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Standby: %u s idle -> L2 deep sleep (snapshot UI first)",
             (unsigned)(cos_pm_get_l2_idle_ms() / 1000));

    /* 前台是 App → 记 app id;表盘/启动器 → watchface(正常启动即主界面) */
    const char *fg = board_standby_foreground_app_id();
    if (fg && strlen(fg) < sizeof(s_standby_restore_ui)) {
        snprintf(s_standby_restore_ui, sizeof(s_standby_restore_ui), "%s", fg);
    } else {
        snprintf(s_standby_restore_ui, sizeof(s_standby_restore_ui), "watchface");
    }
    board_standby_write_history();
    ESP_LOGI(TAG, "Standby: restore target '%s' (also at /sdcard/history/deepsleep/latest_ui.txt)",
             s_standby_restore_ui);

    /* L2 待机同样是深睡:先把四项设置固化到 SD(无卡则空操作) */
    cos_cc_snapshot_capture_now();

    s_standby_pending      = true;  /* 唤醒后:长按 → 极简时钟,5 击才进主系统 */
    s_standby_boot_restore = false; /* 5 击在 gate 内部再置位 */
    s_standby_hold_wakeups = 0;
    s_standby_wakes        = 0;
    /* L2 待机永远走"触摸轮询 + 长按唤醒"语义:清除任何残留的定时关机状态
     * (RTC_DATA_ATTR 跨深睡保留;若上次定时关机未到点被强制复位,timed=1
     * 残留会让本次待机错误地"一次性睡满自动开机"而非长按唤醒)。 */
    s_poweroff_timed     = false;
    s_poweroff_wake_secs = 0;
    board_poweroff_enter_deep_sleep();  /* 不返回(深睡轮询,8s/16s) */
}

/* 开发/测试用:立即进入 L2 待机硬件深睡(走极简时钟 + 5 击唤醒路径),
 * 跳过 15min 空闲等待。供 shell 命令 `power standby` 调用。 */
void cos_board_enter_standby_deep_sleep(void)
{
    board_standby_enter_deep_sleep();
}

/* 极简时钟(L2 待机长按唤醒):纯 LVGL 渲染,位于 cos_init() 之前,
 * 系统服务/JS/网络均未启动。时间直接读 RTC 设备实例(HAL 已在
 * board_drivers_register 注册),避免拉启 time 服务。 */
/* TODO(待机/功耗):极简时钟阶段"单核运行"暂未实现。评估结论——收益小
 * (仅 Clock-Only 渲染,双核空闲核本就 WFI)、改启动链风险高(需禁用另一核
 * 并重建调度),故保持双核、只跑轻量渲染循环。后续若实测待机电流不达标再议。 */
static void board_standby_clock_gate(void)
{
    ESP_LOGI(TAG, "Standby: long-press wake -> minimal clock %u ms "
             "(%u taps to fully boot)", (unsigned)BOARD_STANDBY_CLOCK_MS,
             (unsigned)BOARD_CLOCK_TAPS_TO_BOOT);

    /* 周几:设备层 day_of_week 语义 1=MON..7=SUN(或 0=SUN..6=SAT),
     * v%7 在两种语义下都映射到"周日开头"表 → 稳定英文星期。 */
    static const char *const s_weekday_en[7] =
        { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

    /* 深睡唤醒刚复位过面板,清一次 GRAM 避免残留 */
    cos_dev_display_gc9a01_fill_black();

    /* 建立纯 LVGL 极简时钟屏:Montserrat 字体编译期自带,无需字体服务 */
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 三行:时间 / 日期 / 星期,统一 240 宽 + 居中文本 → 水平居中;
     * y 由 BOARD_CLOCK_*_Y 给出(见上面排版校验),整块垂直居中。 */
    lv_obj_t *time_lb = lv_label_create(scr);
    lv_obj_set_style_text_font(time_lb, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(time_lb, lv_color_white(), 0);
    lv_obj_set_width(time_lb, BOARD_GC9A01_WIDTH);
    lv_obj_set_style_text_align(time_lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(time_lb, LV_ALIGN_TOP_MID, 0, BOARD_CLOCK_TIME_Y);

    lv_obj_t *date_lb = lv_label_create(scr);
    lv_obj_set_style_text_font(date_lb, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(date_lb, lv_color_white(), 0);
    lv_obj_set_width(date_lb, BOARD_GC9A01_WIDTH);
    lv_obj_set_style_text_align(date_lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(date_lb, LV_ALIGN_TOP_MID, 0, BOARD_CLOCK_DATE_Y);

    lv_obj_t *week_lb = lv_label_create(scr);
    lv_obj_set_style_text_font(week_lb, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(week_lb, lv_color_white(), 0);
    lv_obj_set_width(week_lb, BOARD_GC9A01_WIDTH);
    lv_obj_set_style_text_align(week_lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(week_lb, LV_ALIGN_TOP_MID, 0, BOARD_CLOCK_WEEK_Y);

    /* 占位文本与真实内容等宽同形,首帧(读 RTC 前)也是三行居中的,
     * 不会出现上重下空的跳变 */
    lv_label_set_text(time_lb, "--:--");
    lv_label_set_text(date_lb, "--/--");
    lv_label_set_text(week_lb, "---");
    lv_screen_load(scr);
    lv_refr_now(NULL);   /* 同步刷一帧(服务未启动,不跑 lv_timer_handler) */

    /* 时间短字符串 + RTC 直接读取 */
    char time_str[BOARD_CLOCK_TEXT_LEN];
    char date_str[BOARD_CLOCK_TEXT_LEN];

    int64_t start_us    = esp_timer_get_time();
    int64_t deadline_us = start_us + (int64_t)BOARD_STANDBY_CLOCK_MS * 1000;
    int64_t last_render_us = 0;
    int64_t last_tap_us    = 0;
    int     tap_count      = 0;
    bool    prev_touched   = false;

    while (1) {
        int64_t now_us = esp_timer_get_time();
        if (now_us >= deadline_us)
            break;   /* 8s 窗口结束,无 5 击 → 回 Deep Sleep */

        /* 触摸轮询:上升沿计一次击。初始的"长按"在 gate 前已释放,
         * 不会被误计;相邻两击间隔 >500ms 则重新计数。 */
        int32_t x = 0, y = 0;
        bool touched = cos_dev_touch_chsc6x_read(&x, &y);
        if (touched && !prev_touched) {
            if (tap_count == 0 ||
                (now_us - last_tap_us) <= (int64_t)BOARD_CLOCK_TAP_GAP_MS * 1000) {
                tap_count++;
            } else {
                tap_count = 1;   /* 超时,序列作废重计 */
            }
            last_tap_us = now_us;
            ESP_LOGI(TAG, "Standby clock: tap %d/%d", tap_count,
                     (int)BOARD_CLOCK_TAPS_TO_BOOT);
            if (tap_count >= BOARD_CLOCK_TAPS_TO_BOOT) {
                /* 5 击 → 真正唤醒:清待机、快启动(跳过 Boot Anim)+ 恢复 UI */
                lv_obj_del(scr);
                s_standby_pending      = false;
                s_standby_boot_restore = true;  /* 主系统起来后恢复上次 UI */
                ESP_LOGI(TAG, "Standby clock: %d taps -> fully boot "
                         "(restore '%s')", (int)BOARD_CLOCK_TAPS_TO_BOOT,
                         s_standby_restore_ui);
                return;   /* 继续正常 app_main 启动(cos_init) */
            }
        }
        prev_touched = touched;

        /* 每秒刷新时间/日期(文本未变时不动,LVGL 无连续刷屏) */
        if ((now_us - last_render_us) >= 1000 * 1000) {
            last_render_us = now_us;
            cos_dev_time_t *td = cos_dev_time_get_instance();
            if (td && td->ops && td->ops->get_datetime) {
                cos_datetime_t dt = td->ops->get_datetime();
                snprintf(time_str, sizeof(time_str), "%02u:%02u",
                         (unsigned)dt.hour, (unsigned)dt.min);
                /* 日期用"月/日"(MM/DD):极简时钟只有三行,不留年份;
                 * 年份对"看一眼时间"无价值,月/日信息量更大 */
                snprintf(date_str, sizeof(date_str), "%02u/%02u",
                         (unsigned)dt.month, (unsigned)dt.day);
                lv_label_set_text(time_lb, time_str);
                lv_label_set_text(date_lb, date_str);
                lv_label_set_text(week_lb,
                    s_weekday_en[((unsigned)dt.day_of_week) % 7]);
                lv_refr_now(NULL);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* 8s 窗口结束未到 5 击:清屏并回 Deep Sleep(待机标志保持 → 下次唤醒
     * 仍先进极简时钟;board_poweroff_enter_deep_sleep 不返回) */
    ESP_LOGI(TAG, "Standby clock: timeout, back to L2 deep sleep");
    lv_obj_del(scr);
    s_standby_boot_restore = false;
    board_poweroff_enter_deep_sleep();   /* 不返回 */
}

/* 主系统启动完成后的 App 恢复:极简时钟 5 击才会走到这里。
 * 等 root/watchface 就绪(无动画后 root 立即出现)再启动深睡前 App;
 * App 不存在/启动失败则保持表盘主界面。 */
static void board_standby_try_restore_ui(void)
{
    if (!s_standby_boot_restore)
        return;
    s_standby_boot_restore = false;   /* 一次性 */
    if (s_standby_restore_ui[0] == '\0' ||
        strcmp(s_standby_restore_ui, "watchface") == 0)
        return;

    /* 等 ui_task 处理完 root 启动(避开切换动画窗口,最多 ~5s) */
    for (int i = 0; i < 100; i++) {
        cos_activity_t *top = cos_activity_get_current();
        if (top && !cos_activity_is_transition_in_progress())
            break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    cos_result_t r = cos_app_launch_immediately(s_standby_restore_ui);
    if (r == COS_OK) {
        ESP_LOGI(TAG, "Standby restore: opened '%s'",
                 s_standby_restore_ui);
    } else {
        ESP_LOGW(TAG, "Standby restore: '%s' unavailable (%d), stay watchface",
                 s_standby_restore_ui, (int)r);
    }
}

/* L1/L2 主状态机单步:ui_task 每轮(cos_main_loop 后)调用。
 * 仅 PM 处于 SLEEP(熄屏)时工作;亮屏/AOD 直接返回不干预。 */
static void board_pm_step(void)
{
    static bool            s_l1_gpio_wake_armed = false;
    static int64_t         s_l2_idle_start_us   = 0;  /* 连续 L1 无触摸累计起点 */
    static cos_pm_state_t  s_prev_state         = COS_PM_DISPLAY_ON;

    cos_pm_state_t st  = cos_pm_get_state();
    int64_t now_us = esp_timer_get_time();

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    /* USB MSC 会话进行中:禁止 Light Sleep(会中断 USB 枚举/传输),
     * 让 UI 保持常醒以便显示状态页与响应拔线。
     * 若会话保持中 VBUS 已掉(拔线),立即唤醒显示,交由 App 层 tick
     * 检测 tud_mounted() 为假并自动断开恢复,避免熄屏后看似卡死需重启。 */
    if (board_pm_usb_msc_is_held()) {
        if (!_board_vbus_present()) {
            cos_pm_wake_up();
        }
        return;
    }
#endif

    /* 状态边界跟踪 */
    if (st != COS_PM_SLEEP) {
        if (s_prev_state != st) {
            s_prev_state = st;          /* 进入 ON/AOD:CPU 常醒 */
            s_l1_last_act_us = now_us;
        }
        s_l2_idle_start_us = 0;
        return;
    }
    if (s_prev_state != COS_PM_SLEEP) {
        s_prev_state      = COS_PM_SLEEP;
        s_l1_last_act_us  = now_us;    /* 刚熄屏视作"有活动",先撑 guard */
        s_l2_idle_start_us = 0;
    }

    /* 手指仍压在屏上(手掌覆盖熄屏后未抬起等):持续按活动算,不睡 */
    if (gpio_get_level(BOARD_TOUCH_INT_PIN) == 0) {
        s_l1_last_act_us = now_us;
        s_l2_idle_start_us = 0;
        return;
    }

    /* 触摸后 600ms guard:PM 双击窗口 400ms,保证两次 press 都在真实运行期 */
    if (now_us - s_l1_last_act_us < (int64_t)BOARD_L1_ACT_GUARD_MS * 1000)
        return;

    /* 连续无触摸计时(准入 guard 通过才起表,L2 从熄屏算满 15min) */
    if (s_l2_idle_start_us == 0)
        s_l2_idle_start_us = s_l1_last_act_us;

    /* L2:累计 s_l2_idle_ms(默认 15min,省电模式收紧到 10min)无触摸 →
     * 自动待机深睡(固化 UI 历史后不返回) */
    if (now_us - s_l2_idle_start_us >= (int64_t)s_l2_idle_ms * 1000) {
        board_standby_enter_deep_sleep();   /* 不返回 */
    }

    /* L1 Light Sleep:500ms RTC 周期 + 触摸 INT(GPIO44)低电平即时唤醒。
     * 注:Deep Sleep 仅 RTC GPIO0~21 可 GPIO 唤醒;Light Sleep 任意 GPIO
     * 均可(gpio_wakeup_enable + esp_sleep_enable_gpio_wakeup)。
     * 若平台不支持/未配置成功,退化为 500ms 轮询(双击唤醒可能漏,降级可接受)。 */
    if (!s_l1_gpio_wake_armed) {
        esp_err_t we = gpio_wakeup_enable(BOARD_TOUCH_INT_PIN, GPIO_INTR_LOW_LEVEL);
        if (we == ESP_OK)
            esp_sleep_enable_gpio_wakeup();
        else
            ESP_LOGW(TAG, "L1: GPIO wakeup unsupported(%d), timer-poll only",
                     (int)we);
        s_l1_gpio_wake_armed = true;   /* 仅尝试一次 */
    }
    esp_sleep_enable_timer_wakeup(BOARD_L1_LS_PERIOD_US);

    esp_err_t ls = esp_light_sleep_start();   /* 阻塞 ~500ms 或触摸唤醒 */
    if (ls != ESP_OK) {
        /* 有外设(如 WiFi/BT modem)持锁无法轻睡:本轮跳过,下轮再试 */
        ESP_LOGD(TAG, "L1: light sleep skipped (%d)", (int)ls);
        return;
    }
    /* 进轻睡成功:打唤醒原因便于验证(GPIO=触摸唤醒/TIMER=周期唤醒)。
     * 仅 DEBUG 级别,避免 500ms 一次刷屏;验证时开 log level debug。 */
    ESP_LOGD(TAG, "L1: light sleep entered (wake=%d)",
             (int)esp_sleep_get_wakeup_cause());

    /* 唤醒原因:GPIO=触摸(重新计时);定时器=无触摸(累计继续) */
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        s_l1_last_act_us   = esp_timer_get_time();
        s_l2_idle_start_us = 0;        /* 触摸打断 L2 计时 */
    }
}

/* ════════════════════════════════════════════════════════════════
 *  Power OPS:把 PM 服务的熄屏/亮屏状态接到 GC9A01 背光 + 面板显示。
 *  DEV_POWER_STATE_ON  -> 开面板(0x29)+ 恢复背光
 *  DEV_POWER_STATE_SLEEP -> 灭背光 + 关面板(0x28)
 *  DEV_POWER_STATE_AOD -> 面板保持开(仅背光为 0,与 SLEEP 同样处理)
 *  熄屏时额外关面板(此前只灭背光):面板静态电流是熄屏(Light Sleep)期间
 *  最大的一项耗电(约 2-4mA),关掉后整段待机只有深睡/轻睡的芯片电流。
 *  GRAM 内容保留,唤醒只需一条 0x29,无需重跑 init 序列。
 *  DEV_POWER_STATE_OFF(关机) -> 进入硬件深睡(esp_deep_sleep_start,不返回)。
 * ════════════════════════════════════════════════════════════════ */
static int board_power_set(dev_power_state_t state)
{
    /* 关机:进入深睡轮询(不返回) */
    if (state == DEV_POWER_STATE_OFF) {
        board_poweroff_enter_deep_sleep();
        return 0;  /* esp_deep_sleep_start 不返回 */
    }

    cos_dev_display_t *disp = cos_dev_display_get_instance();
    if (!disp || !disp->ops || !disp->ops->power_on || !disp->ops->power_off)
    {
        ESP_LOGE(TAG, "Power set(%d) failed: display HAL not ready", (int)state);
        return -1;
    }
    if (state == DEV_POWER_STATE_SLEEP)
    {
        /* 熄屏:先灭背光再关面板,避免"先黑屏再灭背光"的可见闪烁 */
        disp->ops->power_off();
        cos_dev_display_gc9a01_display_off();
    }
    else
    {
        /* 亮屏/AOD:先开面板再恢复背光(顺序反了会看到一帧黑) */
        cos_dev_display_gc9a01_display_on();
        disp->ops->power_on();
    }
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

static const cos_dev_power_ops_t s_board_power_ops = {
    .set_power           = board_power_set,
    .set_poweroff_params = board_power_setoff_params,
};

/* ════════════════════════════════════════════════════════════════
 *  板级驱动注册(对接 CantoMk6 设备 HAL)
 * ════════════════════════════════════════════════════════════════ */
static void board_drivers_register(void)
{
    /* GC9A01 / CHSC6X / BM8563 已是真实驱动 */
    cos_dev_display_gc9a01_register();
    cos_dev_rtc_bm8563_init();
    /* Power ops 必须在 cos_init()(PM 服务读取 instance)之前注册 */
    cos_dev_power_register(&s_board_power_ops);
    /* 电池:ADC1_CH0 单次模式 + 校准;实际电芯 180~220mAh(XIAO 小电池),
     * 取中值 200mAh 作为容量口径(影响百分比/低电阈值/续航估算)。如后期实测
     * 标定,改这里一个数即可。 */
    cos_dev_battery_register(&s_board_battery_ops, 200);
    ESP_LOGI(TAG, "Drivers registered (GC9A01/CHSC6X/BM8563 real, power->backlight, battery->ADC)");
}

/* 智能模式(省电/性能均未启用)默认 CPU profile:DFS 80-160MHz 按需调频。
 * 必须在 cos_init() 之前调用:power_save / beast_mode 服务首次切换模式时会把
 * "当前 PM 配置"快照为 _pm_normal_config,退出时用它恢复。先在此配好智能
 * profile,两个服务快照到的就是 DFS 80-160,退出后回落回 DFS 智能档,
 * 而不是回落到 sdkconfig 的固定 160MHz(未 configure 时 esp_pm_get_configuration
 * 读到空配置,服务自身会回落为固定 160)。
 * 自动 Light-sleep 保持关闭:省电服务实测 light_sleep_enable=true 会让系统在
 * UI 活跃期也进 Light-sleep(USB-Serial-JTAG 控制台失活/背光明灭闪烁);
 * 熄屏轻睡由 board_pm_step() 手动 500ms 打点,与 DFS 正交。 */
static void board_smart_cpu_profile_apply(void)
{
    esp_pm_config_t cfg = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Smart CPU profile (DFS 80-160MHz) failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Smart CPU profile: DFS %u-%uMHz (auto light-sleep off)",
             (unsigned)cfg.min_freq_mhz, (unsigned)cfg.max_freq_mhz);
}

/* ════════════════════════════════════════════════════════════════
 *  ESP-IDF 入口
 * ════════════════════════════════════════════════════════════════ */
/* 复位原因 → 可读文本。
 * 用途:USB-Serial-JTAG console 会被 TinyUSB 接管而静默,崩溃时 panic 回溯
 * 打不出来;下次开机打印这行即可判定上次复位是否 PANIC/WDT/BROWNOUT。 */
static const char *board_reset_reason_str(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_UNKNOWN:   return "unknown";
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external pin";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "PANIC (crash/abort)";
    case ESP_RST_INT_WDT:   return "INT watchdog";
    case ESP_RST_TASK_WDT:  return "TASK watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    default:                return "?";
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== CantoMk6 ESP32-S3 boot ===");
    ESP_LOGI(TAG, "Reset reason: %d (%s)", (int)esp_reset_reason(),
             board_reset_reason_str(esp_reset_reason()));
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
            /* 一并清除定时模式残留:否则本次"非定时器复位"后 timed=1/
             * wake_secs 保持,下次触摸关机或 L2 待机将错误地"睡满自动开机" */
            s_poweroff_timed        = false;
            s_poweroff_wake_secs    = 0;
            gpio_deep_sleep_hold_dis();
            gpio_hold_dis(BOARD_GC9A01_BL_PIN);
            /* L2 待机复用关机骨架(s_poweroff_pending 也会置位),这里一并
             * 取消:USB/BOOT 唤醒一律正常开机,不进极简时钟 gate */
            s_standby_pending      = false;
            s_standby_boot_restore = false;
        }
    } else {
        /* 正常启动:解除深睡 pad hold(否则后续 LEDC/GPIO 对背光的操作无效) */
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis(BOARD_GC9A01_BL_PIN);
    }

    /* 0. 创建 FreeRTOS 任务——必须在任何 internal 大分配之前!
     * 原因:ui_task 会执行 flash 写(如配置保存→SPIFFS)。flash 写期间 CPU cache
     * 被禁用,PSRAM 栈不可访问 → esp_task_stack_is_sane_cache_disabled() 断言崩溃。
     * 因此 ui_task 栈必须分配在 internal RAM;且 cos_init() 之后 internal RAM
     * 碎片化(largest≈7KB),48KB 栈无法分配。必须在 app_main 最开头、驱动/
     * SPIFFS 等 internal 占用之前分配(实测 CHSC6X+SPIFFS 会把 largest 从
     * ~51KB 啃到 ~45KB,导致 49152B 栈分配失败)。
     * ui_task 通过 s_cos_ready 信号量等待 Core 就绪,不依赖驱动初始化顺序。
     * input/service/script 不执行 flash 写,栈留在 PSRAM(8MB 充足)。
     * BOARD_TASK_*_STACK 为 words 单位,WithCaps 版要求 bytes(×4)。 */
    ESP_LOGI(TAG, "Heap before tasks: DRAM free=%u (largest=%u), PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_cos_ready = xSemaphoreCreateBinary();
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
    esp_err_t ret = cos_dev_display_gc9a01_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GC9A01 init failed: %s", esp_err_to_name(ret));
        /* 不 return,继续启动(Shell 仍可用) */
    }

    /* 4. 注册 LVGL display(flush_cb → esp_lcd_panel_draw_bitmap) */
    cos_dev_display_gc9a01_lvgl_init();

    /* 4.5 CHSC6X 触摸:创建 LVGL POINTER indev(read timer 自动轮询)
     * 必须在 cos_init() 前创建,Core 侧 cos_touch_get_indev() 才能找到 */
    cos_dev_touch_chsc6x_init();

    /* 5. 注册板级驱动到 CantoMk6 HAL */
    board_drivers_register();

    /* 5.5 BLE 提前初始化:必须在 cos_init() 之前(internal RAM 尚连续、
     * largest≈45KB)完成 NimBLE/controller 初始化——cos_init() 之后 largest
     * 只剩 ≈7KB,controller 的大块 internal|DMA 分配必然 ESP_ERR_NO_MEM
     * (表现为打开蓝牙开关时 init 失败)。失败不致命:开关仍会懒初始化重试。 */
    cos_bt_esp32_early_init();

    /* 5.6 Wi-Fi 提前初始化:与蓝牙同理(esp_wifi_init 的 static RX buffer
     * 需 internal DMA 连续内存)。内部带内存门控:largest 不足时自动跳过、
     * 保持懒初始化,绝不挤占 cos_init()/LVGL UI 加载的 internal 空间。 */
    cos_net_wifi_esp32_early_init();

    /* 6. Shell:USB-CDC(永远可用,不依赖 SD) */
    board_shell_usb_cdc_start();

    /* 6.5 文件系统:真 microSD 优先挂载 /sdcard,无卡回退 SPIFFS
     * (config/state/资源依赖,须在 cos_init 前) */
    board_fs_mount();
    /* 6.6 应用持久化的 DEV 开关(依赖配置文件在 fs 上,须在 mount 后) */
    board_apply_dev_mode_config();

    /* 7. FreeRTOS 任务已在 app_main 最开头创建(步骤 0:internal 最完整时
     * 分配 48KB ui_task 栈,此时才能成功;驱动/SPIFFS 之后 largest 只有 ~45KB)。 */

    /* 7.5 L2 待机唤醒的"极简时钟"gate:仅 s_standby_pending(15min 自动待机
     * 后长按唤醒)时执行。此处位于 cos_init() 之前:LVGL/触摸/RTC 设备均已
     * 就绪,但 time/JS/网络等服务尚未启动,时间直接读 RTC 设备,开销极低。
     * 8s 内连续 5 击 → 清待机标志返回(走正常 cos_init 完整启动);
     * 否则 8s 到点自动回 Deep Sleep(不返回)。 */
    if (s_standby_pending) {
        board_standby_clock_gate();   /* 5 击内部清标志;超时深睡不返回 */
    }
    /* 极简时钟 5 击 = "真正唤醒":跳过 Boot Anim 快速进入主系统(开关保留,
     * 仅此路径自动跳过;普通开机仍显示开机动画) */
    if (s_standby_boot_restore) {
        cos_boot_anim_skip_request();
    }

    /* 7.8 SD 快照恢复:把 /sdcard/history/cc/settings.txt 里的亮度/蓝牙/
     * WiFi/电源模式覆盖回系统。必须在 cos_init() 之前:
     *   - cos_init() -> cos_service_config_init() 会用 cfg.json 设一次亮度/蓝牙;
     *   - Control Center widget 在 cos_init() 内创建,会读取服务状态决定开关;
     *   - power_save/beast_mode 服务 init 时会快照"当前 PM 配置",此处先恢复
     *     模式,快照到的才是正确基线。
     * 无真 SD 卡或无快照文件时本函数直接返回,cfg.json 路径完全不变。
     * 唤醒路径(deep sleep 重启)也走这里 —— 唤醒后再次应用同一份快照是幂等的。 */
    {
        cos_result_t snap_ret = cos_cc_snapshot_restore_now();
        ESP_LOGI(TAG, "CC snapshot restore: %s",
                 snap_ret == COS_OK ? "applied" : "no snapshot / no SD (cfg.json in use)");
    }

    /* 7.9 智能模式默认 CPU profile:DFS 80-160MHz。必须早于 cos_init()
     * (见 board_smart_cpu_profile_apply 注释:省电/性能服务在 init 时快照
     * "当前 PM 配置",退出时回落到它 —— 即回落回 DFS 智能档而非固定 160)。 */
    board_smart_cpu_profile_apply();

    /* 8. CantoMk6 Core 初始化(持 LVGL 递归锁,避免与 ui_task 并发竞态) */
    lv_lock();
    cos_init();
    lv_unlock();
    ESP_LOGI(TAG, "CantoMk6 core initialized");
    /* 放行 ui_task(LVGL 渲染/事件循环开始) */
    if (s_cos_ready != NULL) {
        xSemaphoreGive(s_cos_ready);
    }
    /* 8.5 L2 完整唤醒的 App 恢复:极简时钟 5 击后置位 s_standby_boot_restore,
     * root/watchface 就绪后尝试回到深睡前正在使用的 App(尽力而为) */
    board_standby_try_restore_ui();
    /* 诊断:cos_init() 之后的 internal RAM 状态(验证阈值 8KB 分流效果) */
    ESP_LOGI(TAG, "Heap after cos_init: DRAM free=%u (largest=%u), PSRAM free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_LOGI(TAG, "Boot complete (GC9A01/CHSC6X/BM8563 real)");
}
