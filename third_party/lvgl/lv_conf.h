/**
 * @file lv_conf.h
 * @brief LVGL 9 配置(ESP32-S3 + FreeRTOS OSAL)
 *
 * 必须满足:
 *   1. src/config/eos_lvgl_requirements.h 的全部强制项
 *      (32 个 widgets + DRAW_SW + FLEX/GRID + THEME + OBSERVER
 *       + SNAPSHOT + QRCODE + TINY_TTF + LODEPNG + MONTSERRAT_14/30)
 *   2. LV_USE_OS = LV_OS_FREERTOS(启用真实 lv_lock / lv_unlock,
 *      ui_task 与 input_task 线程安全)
 *
 * 依据:third_party/lvgl/lv_conf_template.h + eos_lvgl_requirements.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#endif

/* ── 颜色 ─────────────────────────────────────────────── */
#define LV_COLOR_DEPTH          16    /* RGB565,GC9A01 */
/* 注意:不使用 LV_COLOR_16_SWAP(该宏在 lv_refr.c flush 前整体字节交换,
 * 生效路径不可控)。GC9A01 大端字节序由板级 flush_cb
 * (eos_dev_display_gc9a01.c)统一交换处理,渲染 buffer 保持标准小端
 * RGB565,各渲染路径一致。 */
#define LV_COLOR_CHROMA_KEY     lv_color_hex(0x00ff00)

/* ── 内存 ───────────────────────────────────────────────
 * LVGL 池放 PSRAM(AGENTS.md 第22节:大对象/缓存用 PSRAM):
 * LV_MEM_ADR=0 时 LVGL 用静态数组 work_mem_int(lv_mem_core_builtin.c),
 * 其前缀 LV_ATTRIBUTE_LARGE_RAM_ARRAY 指向 .ext_ram.bss(PSRAM)。
 * 效果:
 *   1) 释放 ~128KB internal DRAM —— 修复 eos_init 后 internal 被挤爆
 *      (DRAM free=63KB / largest=7KB) 导致的 ui/service/script 任务栈
 *      创建失败(ui 栈 8KB > largest 7KB);
 *   2) AppHeader 38KB 渐变缓冲(lv_malloc)不再因池不足而失败。
 * 渲染缓冲仍走 internal s_flush_buf(eos_dev_display_gc9a01.c,DMA 友好),
 * SPI DMA 路径不受影响。
 *
 * 512KB 依据:系统初始化(control_center 创建)实测消耗 ~130KB+,
 * 128KB 时 lv_realloc 失败 -> spec_attr OOM -> LV_ASSERT_MALLOC while(1)
 * 死循环 -> task_wdt 超时。512KB 为 UI 运行时(图层/动画/图片解码)留余量。
 * ─────────────────────────────────────────────────────── */
#ifdef ESP_PLATFORM
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY EXT_RAM_BSS_ATTR
#endif
#define LV_MEM_SIZE             (512U * 1024U)  /* 仅 LV_STDLIB_BUILTIN 时使用 */
#define LV_MEM_ADR              0

/* LVGL 9 标准库分配:改用 C malloc(esp-idf heap,>16KB 请求路由到 8MB PSRAM),
 * 摆脱 512KB 内置池限制,避免 layer/snapshot 等大分配失败。
 * (LV_MEM_CUSTOM 是 LVGL 8 的宏,LVGL 9 必须用 LV_USE_STDLIB_MALLOC) */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB

/* ── 操作系统(关键:启用 FreeRTOS OSAL) ──────────────── */
#define LV_USE_OS               LV_OS_FREERTOS

/* ── HAL ─────────────────────────────────────────────── */
#define LV_DEF_REFR_PERIOD      30   /* ms(AGENTS.md 第20节:稳定流畅优先) */
#define LV_DPI_DEF              130
#define LV_DRAW_BUF_STRIDE_ALIGN 1

/* ── 绘制引擎 ─────────────────────────────────────────── */
#define LV_USE_DRAW_SW          1

/* ── 32 个 widgets(eos_lvgl_requirements.h 强制) ─────── */
#define LV_USE_ANIMIMG          1
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BUTTON           1
#define LV_USE_BUTTONMATRIX     1
#define LV_USE_CALENDAR         1
#define LV_USE_CANVAS           1
#define LV_USE_CHART            1
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMAGE            1
#define LV_USE_IMAGEBUTTON      1
#define LV_USE_KEYBOARD         1
#define LV_USE_LABEL            1
#define LV_LABEL_TEXT_SELECTION 1
#define LV_USE_LED              1
#define LV_USE_LINE             1
#define LV_USE_LIST             1
#define LV_USE_MENU             1
#define LV_USE_MSGBOX           1
#define LV_USE_ROLLER           1
#define LV_USE_SCALE            1
#define LV_USE_SLIDER           1
#define LV_USE_SPAN             1
#define LV_USE_SPINBOX          1
#define LV_USE_SPINNER          1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         1
#define LV_USE_TABLE            1
#define LV_USE_TABVIEW          1
#define LV_USE_TILEVIEW         1
#define LV_USE_WIN              1

/* ── 布局(强制) ──────────────────────────────────────── */
#define LV_USE_FLEX             1
#define LV_USE_GRID             1

/* ── 主题 / Observer(强制) ───────────────────────────── */
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_OBSERVER         1

/* ── Snapshot / QRCode(强制) ─────────────────────────── */
#define LV_USE_SNAPSHOT         1
#define LV_USE_QRCODE           1

/* ── Tiny TTF(强制,中文显示) ────────────────────────── */
#define LV_USE_TINY_TTF         1
#define LV_TINY_TTF_FILE_SUPPORT 1

/* ── LodePNG(强制,图片解码) ─────────────────────────── */
#define LV_USE_LODEPNG          1

/* ── 字体(MONTSERRAT_14/30 为强制) ──────────────────── */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   1
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1   /* 强制 + 默认 */
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_26   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_30   1   /* 强制 */
#define LV_FONT_MONTSERRAT_32   1
#define LV_FONT_MONTSERRAT_34   1
#define LV_FONT_MONTSERRAT_36   1
#define LV_FONT_MONTSERRAT_38   1
#define LV_FONT_MONTSERRAT_40   1
#define LV_FONT_UNSCII_8        1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE   1

/* 压缩字体支持(必须开启)。
 * 必须开启,否则 lv_font_get_bitmap_fmt_txt() 对压缩字体直接返回 NULL
 * → draw_letter_cb 访问空 draw_buf → LoadProhibited panic
 * (实测:右滑打开 Control Center 渲染图标字符时崩溃重启)。
 * 压缩字体: eos_font_icon.c / eos_font_han_sans_22.c (bitmap_format=1,
 * lv_font_conv 默认 --compress; jbm_22/26/30 显式 --no-compress 不受影响)。 */
#define LV_USE_FONT_COMPRESSED  1

/* ── 日志 / 调试 ──────────────────────────────────────── */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1
#define LV_USE_ASSERT_HANDLER   1
/* 自定义断言处理:LVGL 断言失败时 abort() → ESP-IDF panic handler → 打印现场并自动重启。
 * 默认 handler 是 while(1) 死循环,会导致系统永久卡死(watchdog 无法自恢复)。
 * 日志实证:lv_obj_has_flag(NULL) assert 后 ui_task 卡死,设备不重启。 */
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0

#endif /* LV_CONF_H */
