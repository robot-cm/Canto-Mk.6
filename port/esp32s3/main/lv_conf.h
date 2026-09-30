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
/* 注意:不再使用 LV_COLOR_16_SWAP。该宏会让 lv_refr.c 在 flush 前对
 * buffer 整体字节交换,与板级 flush_cb(eos_dev_display_gc9a01.c)的
 * 手动交换冲突造成双重交换,导致开机动画纯色错乱。
 * GC9A01 大端字节序统一由板级 flush_cb 处理:渲染 buffer 保持标准
 * 小端 RGB565,拷贝后交换字节再发送,纯色/图片/文字所有路径一致。 */
#define LV_COLOR_CHROMA_KEY     lv_color_hex(0x00ff00)

/* ── 内存 ───────────────────────────────────────────────
 * LVGL 池放 PSRAM(AGENTS.md 第22节:大对象/缓存用 PSRAM):
 * 启用 LV_MEM_CUSTOM，让 lv_malloc/free/realloc 直接走 C 标准库。
 * esp-idf 在 CONFIG_SPIRAM_USE_MALLOC=y 且 MALLOC_ALWAYSINTERNAL=16KB 时，
 * 会把大于 16KB 的请求路由到 8MB PSRAM；小对象仍可能留在 internal RAM。
 * 这样 LVGL 不再受 512KB 静态池限制，避免 snapshot/layer/canvas 等大对象
 * 分配失败。渲染缓冲仍走 internal s_flush_buf(eos_dev_display_gc9a01.c)，
 * SPI DMA 路径不受影响。
 * ─────────────────────────────────────────────────────── */
#ifdef ESP_PLATFORM
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY EXT_RAM_BSS_ATTR
#endif
#define LV_MEM_SIZE             (512U * 1024U)  /* CUSTOM 生效时不再使用，保留兼容 */
#define LV_MEM_ADR              0

/* LVGL 9:用 C malloc 替代 512KB 内置池(见 third_party/lvgl/lv_conf.h) */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB

/* ── 操作系统(关键:启用 FreeRTOS OSAL) ──────────────── */
#define LV_USE_OS               LV_OS_FREERTOS

/* ── HAL ─────────────────────────────────────────────── */
/* 触摸响应链路:CHSC6X 采样 → indev read timer → 事件 → 刷新 → 渲染+flush
 * LV_DEF_REFR_PERIOD 30→20ms:刷新检查更频繁,press 视觉反馈更早进入下一帧
 * LV_INDEV_DEF_READ_PERIOD 15ms:触摸采样周期减半,按下事件更早被 LVGL 捕获
 * 注意:SPI 26MHz 单缓冲下每帧 flush ~35ms,实际帧率仍受限于此(~28FPS),
 * 此处为保守优化(AGENTS.md 第20节),SPI 提速/双缓冲留待后续进取阶段 */
#define LV_DEF_REFR_PERIOD      20
#define LV_INDEV_DEF_READ_PERIOD 15
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
/* 支持 lv_font_conv 生成的压缩字体(eos_font_han_sans_22 等) */
#define LV_USE_FONT_COMPRESSED  1
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

/* ── 日志 / 调试 ──────────────────────────────────────── */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1
#define LV_USE_ASSERT_HANDLER   1
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0

#endif /* LV_CONF_H */
