/**
 * @file eos_platform_config.h
 * @brief ESP32-S3 平台配置覆盖(ElenixOS)
 *
 * 机制:eos_config.h 用 __has_include 可选包含本文件,
 *       覆盖 eos_config_defaults.h 的 fallback 默认值
 *
 * 依据:
 *   - 子报告1:Round Display 1.28" 240×240 圆屏
 *   - AGENTS.md 第22节:Internal RAM 用于 DMA/高频,PSRAM 用于图片/buffer/cache
 *   - AGENTS.md 第4节:SD 卡放 APP/资源 → FATFS
 *   - ESP32-S3 用 FreeRTOS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EOS_PLATFORM_CONFIG_H
#define EOS_PLATFORM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── 平台标识 ─────────────────────────────────────────── */
#ifndef EOS_PLATFORM_ESP32
#define EOS_PLATFORM_ESP32 1
#endif

#ifndef EOS_PLATFORM
#define EOS_PLATFORM "ESP32"
#endif

/* ── RTOS:FreeRTOS(ESP32-S3) ──────────────────────────── */
/* 依据:src/config/eos_config_internal.h EOS_RTOS_FREERTOS=1 */
#ifndef EOS_RTOS_TYPE
#define EOS_RTOS_TYPE EOS_RTOS_FREERTOS
#endif

/* ── 显示:240×240 圆屏(GC9A01) ──────────────────────── */
/* 子报告1:Round Display 1.28" 240×240 65K 色 */
#ifndef EOS_USE_VIRTUAL_DISPLAY
#define EOS_USE_VIRTUAL_DISPLAY 0
#endif

#ifndef EOS_DISPLAY_WIDTH
#define EOS_DISPLAY_WIDTH 240
#endif

#ifndef EOS_DISPLAY_HEIGHT
#define EOS_DISPLAY_HEIGHT 240
#endif

/* 圆屏半径 = 240/2 = 120 */
#ifndef EOS_DISPLAY_RADIUS
#define EOS_DISPLAY_RADIUS 120
#endif

#ifndef EOS_DISPLAY_BRIGHTNESS_MIN
#define EOS_DISPLAY_BRIGHTNESS_MIN 5
#endif

#ifndef EOS_DISPLAY_BRIGHTNESS_MAX
#define EOS_DISPLAY_BRIGHTNESS_MAX 100
#endif

/* ── 文件系统：POSIX（ESP-IDF VFS 层，AGENTS.md 第4节） ─── */
/* 依据：
 *   - ESP32-S3 的 ESP-IDF 提供完整 POSIX 接口（fopen/FILE/DIR）
 *   - SD 卡通过 esp_vfs_fat_sdmmc_mount() 挂载到 /sdcard，
 *     上层直接用 POSIX fopen 访问，无需裸 FatFS
 *   - eos_config_internal.h 的 EOS_FS_POSIX=0 对应此分支
 */
#ifndef EOS_FS_TYPE
#define EOS_FS_TYPE EOS_FS_POSIX
#endif

/* SD 卡挂载点(ESP-IDF FATFS VFS 默认路径前缀) */
#ifndef EOS_SYS_ROOT_DIR
#define EOS_SYS_ROOT_DIR "/sdcard"
#endif

#ifndef EOS_LVGL_FS_LETTER
#define EOS_LVGL_FS_LETTER 'S'   /* S = SD card LVGL drive */
#endif

/* ── 内存:AUTO 分流(AGENTS.md 第22节) ───────────────── */
/* < 8KB → Internal RAM(DMA 友好 + SPIFFS 写数据源);≥ 8KB → PSRAM(大对象)。
 * 原阈值 32KB 使 8~32KB 的中块(eos_init 期间 UI/图片/缓存)全部滞留 internal RAM,
 * 造成 eos_init 后 internal 碎片化(free≈61KB 但 largest≈7KB),
 * ui_task 栈(48KB)无法从 internal 分配。降到 8KB 后中块转 PSRAM(8MB 充足),
 * internal 释放给 FreeRTOS 栈与内核对象。 */
#ifndef EOS_MEM_ALLOC_PROVIDER
#define EOS_MEM_ALLOC_PROVIDER EOS_MEM_PROVIDER_AUTO
#endif

#ifndef EOS_MEM_POOL_ALLOC_THRESHOLD
#define EOS_MEM_POOL_ALLOC_THRESHOLD (8 * 1024)
#endif

/* PSRAM 缓存(图片/buffer) */
#ifndef EOS_CACHE_USE_DEDICATED_MEM
#define EOS_CACHE_USE_DEDICATED_MEM 1
#endif

#ifndef EOS_CACHE_SIZE
#define EOS_CACHE_SIZE (256U * 1024U)   /* 256KB 图片缓存,走 PSRAM */
#endif

/* ── 字体:内置 C 字体编译进 Flash(XIP 映射,不占 PSRAM/DRAM) ──
 * 字体为系统级必需资源,全字号常驻,放 SD/PSRAM 既浪费又增加启动时序风险。
 * 遵循 AGENTS.md 第4节:Flash 放系统,PSRAM 放运行时大对象,SD 放 APP/资源。 */
#ifndef EOS_FONT_TYPE
#define EOS_FONT_TYPE EOS_FONT_C_MULTI
#endif

#ifndef EOS_FONT_SD_ENABLE
#define EOS_FONT_SD_ENABLE 0
#endif

/* ── 产品标识 ─────────────────────────────────────────── */
#ifndef ELENIX_WATCH_MARKETING_NAME
#define ELENIX_WATCH_MARKETING_NAME "Elenix Round"
#endif

#ifndef ELENIX_WATCH_MODEL_NUMBER
#define ELENIX_WATCH_MODEL_NUMBER "ESP32S3-Round-1.28"
#endif

#ifdef __cplusplus
}
#endif

#endif /* EOS_PLATFORM_CONFIG_H */
