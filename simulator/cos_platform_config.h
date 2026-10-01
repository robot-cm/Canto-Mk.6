/**
 * @file cos_platform_config.h
 * @brief Windows 模拟器平台配置覆盖(CantoMk6)
 *
 * 机制:cos_config.h 用 __has_include 可选包含本文件,
 *       覆盖 cos_config_defaults.h 的 fallback 默认值。
 *       simulator/CMakeLists.txt 将本目录置于 include 路径首位,
 *       使 src/ 全部翻译单元解析到本文件而非 port/esp32s3/main/ 的版本。
 *
 * 与真机(port/esp32s3/main/cos_platform_config.h)的差异:
 *   - 无 COS_PLATFORM_ESP32(守卫 ESP 专属代码,src/ 内所有 ESP-IDF
 *     include 均有守卫;唯一例外 esp_heap_caps.h 由 simulator/esp_shim/ 提供)
 *   - RTOS: BARE_METAL(单线程,主循环驱动)
 *   - 内存: STDLIB_CLIB(宿主 malloc,无 PSRAM 分流)
 *   - 显示: 240×240 圆屏参数与真机一致(1:1),渲染由 LVGL SDL 驱动承担
 *   - FS:   POSIX + LVGL 盘符 'S'(由 lv_conf.h 的
 *           LV_FS_DEFAULT_DRIVER_LETTER='S' 匹配,cos_lvgl_fs.c 有
 *           编译期 #error 检查)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COS_PLATFORM_CONFIG_H
#define COS_PLATFORM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── 平台标识 ─────────────────────────────────────────── */
/* 注意:不要在这里定义 COS_SIMULATOR —— cos_core.c 在 include
 * cos_config.h 之前就检查它,必须由 CMake -D 注入。 */
#ifndef COS_PLATFORM
#define COS_PLATFORM "Native"
#endif

/* ── RTOS:BARE_METAL(模拟器单线程) ───────────────────── */
#ifndef COS_RTOS_TYPE
#define COS_RTOS_TYPE COS_RTOS_BARE_METAL
#endif

/* ── 显示:240×240 圆屏(与真机 1:1) ──────────────────── */
#ifndef COS_USE_VIRTUAL_DISPLAY
#define COS_USE_VIRTUAL_DISPLAY 0
#endif

#ifndef COS_DISPLAY_WIDTH
#define COS_DISPLAY_WIDTH 240
#endif

#ifndef COS_DISPLAY_HEIGHT
#define COS_DISPLAY_HEIGHT 240
#endif

/* 圆屏半径 = 240/2 = 120(与真机一致) */
#ifndef COS_DISPLAY_RADIUS
#define COS_DISPLAY_RADIUS 120
#endif

#ifndef COS_DISPLAY_BRIGHTNESS_MIN
#define COS_DISPLAY_BRIGHTNESS_MIN 5
#endif

#ifndef COS_DISPLAY_BRIGHTNESS_MAX
#define COS_DISPLAY_BRIGHTNESS_MAX 100
#endif

/* ── 文件系统:POSIX(宿主 stdio,根目录由 CMake 注入) ──── */
/* COS_SYS_ROOT_DIR 不在此定义:根 CMakeLists.txt 在 COS_PLATFORM=Native
 * 分支以 -DCOS_SYS_ROOT_DIR="${CMAKE_BINARY_DIR}/fs/" 注入,
 * 在这里定义会被 -D 优先级压制或产生重定义冲突,故保持未定义。 */
#ifndef COS_FS_TYPE
#define COS_FS_TYPE COS_FS_POSIX
#endif

#ifndef COS_LVGL_FS_LETTER
#define COS_LVGL_FS_LETTER 'S' /* S = SD card LVGL drive,与真机一致 */
#endif

/* ── 内存:C 标准库(宿主 malloc,无 PSRAM 分流) ────────── */
#ifndef COS_MEM_ALLOC_PROVIDER
#define COS_MEM_ALLOC_PROVIDER COS_MEM_PROVIDER_STDLIB_CLIB
#endif

/* 宿主无 PSRAM,图片缓存不进专用内存 */
#ifndef COS_CACHE_USE_DEDICATED_MEM
#define COS_CACHE_USE_DEDICATED_MEM 0
#endif

#ifndef COS_CACHE_SIZE
#define COS_CACHE_SIZE (128U * 1024U)
#endif

/* ── 字体:内置 C 字体(与真机一致,不依赖 SD 卡 .ef) ───── */
#ifndef COS_FONT_TYPE
#define COS_FONT_TYPE COS_FONT_C_MULTI
#endif

#ifndef COS_FONT_SD_ENABLE
#define COS_FONT_SD_ENABLE 0
#endif

/* ── 产品标识 ─────────────────────────────────────────── */
#ifndef CANTOMK6_WATCH_MARKETING_NAME
#define CANTOMK6_WATCH_MARKETING_NAME "Canto Mk.6"
#endif

#ifndef CANTOMK6_WATCH_MODEL_NUMBER
#define CANTOMK6_WATCH_MODEL_NUMBER "Canto Mk.6 1.28 Round"
#endif

#ifdef __cplusplus
}
#endif

#endif /* COS_PLATFORM_CONFIG_H */
