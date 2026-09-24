/**
 * @file board_xiao_esp32s3_round.h
 * @brief Seeed Studio XIAO ESP32-S3 + 1.28" Round Touch Display 引脚定义
 *
 * 数据源(AGENTS.md 第28节优先级):
 *   1. Seeed 官方库源码 Seeed_Arduino_RoundDisplay/src/lv_xiao_round_screen.h
 *      (XIAO_CS=D1, XIAO_DC=D3, XIAO_BL=D6, TOUCH_INT=D7, CHSC6X_I2C_ID=0x2e)
 *   2. Seeed 官方示例 examples/HardwareTest/lv_hardware_test.h
 *      (SD.begin(D2), I2C_BM8563(Wire), analogReadMilliVolts(D0))
 *   3. Seeed 官方 Wiki XIAO ESP32-S3 Pin Map (Dx → GPIO 映射)
 *
 * 关键修正(子报告1):
 *   - 触摸芯片是 CHSC6X(I2C 0x2e),非 CST816S(Wiki 与博客常混用)
 *   - SD CS = D2 = GPIO3
 *   - LCD CS = D1 = GPIO2,LCD DC = D3 = GPIO4
 *   - D6/D7 被 LCD 背光/触摸 INT 占用 → UART 不可用,Shell 走 USB-CDC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BOARD_XIAO_ESP32S3_ROUND_H
#define BOARD_XIAO_ESP32S3_ROUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ════════════════════════════════════════════════════════════════
 *  XIAO ESP32-S3 Pin → GPIO 映射(Seeed 官方 Wiki Pin Map)
 * ════════════════════════════════════════════════════════════════ */
#define XIAO_D0_GPIO        1    /* ADC(电池电压) */
#define XIAO_D1_GPIO        2    /* LCD CS */
#define XIAO_D2_GPIO        3    /* SD CS */
#define XIAO_D3_GPIO        4    /* LCD DC */
#define XIAO_D4_GPIO        5    /* I2C SDA(touch + RTC) */
#define XIAO_D5_GPIO        6    /* I2C SCL(touch + RTC) */
#define XIAO_D6_GPIO       43    /* UART TX ← 被 LCD 背光占用 */
#define XIAO_D7_GPIO       44    /* UART RX ← 被触摸 INT 占用 */
#define XIAO_D8_GPIO        7    /* SPI SCK */
#define XIAO_D9_GPIO        8    /* SPI MISO */
#define XIAO_D10_GPIO       9    /* SPI MOSI */
#define XIAO_D11_GPIO      42
#define XIAO_D12_GPIO      41
#define XIAO_USER_LED_GPIO 21
#define XIAO_BOOT_GPIO      0

/* ════════════════════════════════════════════════════════════════
 *  LCD GC9A01 — SPI 接口
 *  来源:lv_xiao_round_screen.h (XIAO_CS=D1, XIAO_DC=D3, XIAO_BL=D6)
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_GC9A01_SPI_HOST   SPI3_HOST
#define BOARD_GC9A01_CS_PIN    XIAO_D1_GPIO      /* D1 = GPIO2  */
#define BOARD_GC9A01_DC_PIN    XIAO_D3_GPIO      /* D3 = GPIO4  */
#define BOARD_GC9A01_RST_PIN   (-1)              /* GC9A01 仅软件复位(CantoMk6-main 真机验证);
                                                  * D2=GPIO3 是 SD CS,绝不能占用做 RST */
#define BOARD_GC9A01_BL_PIN    XIAO_D6_GPIO      /* D6 = GPIO43 */
#define BOARD_GC9A01_SCK_PIN   XIAO_D8_GPIO      /* D8 = GPIO7  */
#define BOARD_GC9A01_MOSI_PIN  XIAO_D10_GPIO     /* D10= GPIO9  */
#define BOARD_GC9A01_MISO_PIN  XIAO_D9_GPIO      /* D9 = GPIO8  */
#define BOARD_GC9A01_SPI_FREQ_HZ   20000000       /* 官方库 SPI_FREQ 20MHz */
#define BOARD_GC9A01_WIDTH       240
#define BOARD_GC9A01_HEIGHT     240

/* ════════════════════════════════════════════════════════════════
 *  Touch CHSC6X — I2C + INT
 *  来源:lv_xiao_round_screen.h (TOUCH_INT=D7, CHSC6X_I2C_ID=0x2e)
 *  注:Wiki 误称 CST816S,以源码为准
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_TOUCH_INT_PIN    XIAO_D7_GPIO      /* D7 = GPIO44 */
#define BOARD_TOUCH_I2C_ADDR   0x2e
#define BOARD_TOUCH_MAX_POINTS  1
#define BOARD_TOUCH_READ_LEN   5

/* ════════════════════════════════════════════════════════════════
 *  I2C Bus(共享:Touch CHSC6X + RTC BM8563)
 *  来源:lv_hardware_test.h (Wire 默认 SDA/SCL = D4/D5)
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_I2C_HOST         I2C_NUM_0
#define BOARD_I2C_SDA_PIN      XIAO_D4_GPIO      /* D4 = GPIO5  */
#define BOARD_I2C_SCL_PIN      XIAO_D5_GPIO      /* D5 = GPIO6  */
#define BOARD_I2C_FREQ_HZ      400000             /* 400kHz Fast-mode */

/* ════════════════════════════════════════════════════════════════
 *  RTC BM8563(= PCF8563 兼容)— I2C
 *  来源:lv_hardware_test.h (I2C_BM8563_DEFAULT_ADDRESS, Wire)
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_RTC_I2C_ADDR     0x51              /* BM8563 默认地址 */

/* ════════════════════════════════════════════════════════════════
 *  microSD 卡 — SPI(与 LCD 共用 SPI 总线,需互斥)
 *  来源:lv_hardware_test.h (SD.begin(D2))
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_SD_SPI_HOST      SPI3_HOST         /* 与 LCD 同一 SPI 总线 */
#define BOARD_SD_CS_PIN        XIAO_D2_GPIO      /* D2 = GPIO3 */
#define BOARD_SD_SCK_PIN       XIAO_D8_GPIO      /* 共用 LCD SCK */
#define BOARD_SD_MOSI_PIN      XIAO_D10_GPIO     /* 共用 LCD MOSI */
#define BOARD_SD_MISO_PIN      XIAO_D9_GPIO      /* 共用 LCD MISO */
#define BOARD_SD_SPI_FREQ_HZ   20000000          /* 与 LCD 同频 */

/* ════════════════════════════════════════════════════════════════
 *  电池 ADC
 *  来源:lv_hardware_test.h (analogReadMilliVolts(D0))
 *  电压范围 1850~2100 mV → 0~100%
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_BATTERY_ADC_CH   ADC_CHANNEL_0     /* GPIO1 = ADC1_CH0 (adc_channel_t,oneshot API) */
#define BOARD_BATTERY_ADC_PIN  XIAO_D0_GPIO      /* D0 = GPIO1 */
#define BOARD_BATTERY_VMIN_MV  1850   /* 分压后 D0 电压下限(0%) */
#define BOARD_BATTERY_VMAX_MV  2100   /* 分压后 D0 电压上限(100%) */
#define BOARD_BATTERY_FULL_MV  2090   /* ≥此电压视为已充满(不再显示充电) */
#define BOARD_BATTERY_SAMPLES  20

/* ════════════════════════════════════════════════════════════════
 *  系统状态 LED
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_LED_PIN          XIAO_USER_LED_GPIO  /* GPIO21 */

/* ════════════════════════════════════════════════════════════════
 *  SD 卡容量(Seeed 官方 Wiki: TF Card Slot for up to 32GB FAT)
 * ════════════════════════════════════════════════════════════════ */
#define BOARD_SD_MAX_SIZE_GB   32

/* ════════════════════════════════════════════════════════════════
 *  FreeRTOS 任务配置(板级 main.c 使用)
 *  依据 AGENTS.md 第20节(动画性能)+ 第22节(内存策略)
 * ════════════════════════════════════════════════════════════════ */
/* 12288 words=48KB internal:2026-09-06 真机实测 PSRAM 版 S3 内部最大连续
 * region≈32KB(bootloader/静态占用后),48KB 必然分配失败(FAILED: ui task
 * create)。32KB(8192 words)在 Settings->WiFi lv_snapshot 转场曾爆栈。
 * 现用 24KB internal:重载已上移 cos_init(24KB main 栈),ui_task 只跑
 * cos_main_loop/lv_timer_handler;后续如 lv_snapshot 仍爆,方案是 ui 栈迁
 * PSRAM + 独立 internal 小栈任务做 flash 写。 */
#define BOARD_TASK_UI_STACK      6144   /* LVGL + GC9A01 flush(internal RAM,24KB) */
#define BOARD_TASK_UI_PRIO       5
#define BOARD_TASK_UI_AFFINITY    1      /* Core 1 */
#define BOARD_TASK_INPUT_STACK    4096   /* CST816 轮询 + lv_indev 喂入 */
#define BOARD_TASK_INPUT_PRIO     4
#define BOARD_TASK_INPUT_AFFINITY 0      /* Core 0 */
#define BOARD_TASK_SERVICE_STACK  4096   /* Wi-Fi / SOCKS5 / RTC 维护 */
#define BOARD_TASK_SERVICE_PRIO   3
#define BOARD_TASK_SERVICE_AFFINITY 0
#define BOARD_TASK_SCRIPT_STACK   16384  /* JerryScript(JS APP 运行) */
#define BOARD_TASK_SCRIPT_PRIO    4
#define BOARD_TASK_SCRIPT_AFFINITY 1

/* LVGL 刷新周期(AGENTS.md 第20节:稳定流畅优先) */
#define BOARD_LVGL_TICK_MS       1
#define BOARD_LVGL_TIMER_PERIOD_MS 5

#ifdef __cplusplus
}
#endif

#endif /* BOARD_XIAO_ESP32S3_ROUND_H */
