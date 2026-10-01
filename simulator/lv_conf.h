/**
 * @file lv_conf.h
 * @brief LVGL 9 配置(Windows 模拟器 + SDL2)
 *
 * 以真机 port/esp32s3/main/lv_conf.h 为蓝本(1:1),差异仅三处:
 *   1. LV_USE_OS = LV_OS_NONE(模拟器单线程,无 FreeRTOS OSAL;
 *      lv_lock/lv_unlock 退化为空操作)
 *   2. 新增 LV_USE_SDL = 1(启用 LVGL 的 SDL 窗口/鼠标驱动,
 *      由 lv_sdl_window_create(240,240) 承担渲染,替代 GC9A01)
 *   3. 新增 LV_FS_DEFAULT_DRIVER_LETTER = 'S'(cos_lvgl_fs.c:31
 *      有编译期检查要求与 COS_LVGL_FS_LETTER 一致;LVGL 内置
 *      FS 驱动保持关闭,盘符 'S' 由 cos_lvgl_fs.c 自行注册)
 *
 * 必须满足(与真机相同):
 *   src/config/cos_lvgl_requirements.h 的全部强制项
 *   (32 个 widgets + DRAW_SW + FLEX/GRID + THEME + OBSERVER
 *    + SNAPSHOT + QRCODE + TINY_TTF + LODEPNG + MONTSERRAT_14/30)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── 颜色 ─────────────────────────────────────────────── */
#define LV_COLOR_DEPTH          16    /* RGB565,与 GC9A01 一致 */
/* 不使用 LV_COLOR_16_SWAP(与真机一致):渲染 buffer 保持标准
 * 小端 RGB565,字节交换由板级 flush_cb 负责;模拟器侧 SDL
 * 窗口按小端直接上屏,无需交换。 */
#define LV_COLOR_CHROMA_KEY     lv_color_hex(0x00ff00)

/* ── 内存 ───────────────────────────────────────────────
 * 与真机一致:lv_malloc/free/realloc 直接走 C 标准库。
 * 真机借 ESP-IDF 把大块路由进 PSRAM;模拟器直接用宿主堆,
 * 无 512KB 静态池限制。LV_MEM_SIZE 保留仅为兼容。 */
#define LV_MEM_SIZE             (512U * 1024U)
#define LV_MEM_ADR              0
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB

/* ── 操作系统(与真机唯一 OSAL 差异:单线程无锁) ──────── */
#define LV_USE_OS               LV_OS_NONE

/* ── SDL 驱动(模拟器专属) ────────────────────────────── */
/* 240×240 SDL 窗口 + 鼠标模拟触摸。渲染模式/缓冲数用默认
 * (PARTIAL / 1 buffer)。LV_SDL_DIRECT_EXIT:SDL_QUIT 时
 * lv_deinit() + exit(0),与模拟器单线程主循环匹配。
 * 滚轮:LVGL 默认 LV_SDL_MOUSEWHEEL_MODE=ENCODER,本工程不
 * 创建 mousewheel indev,表冠改由 main.c 的 SDL_AddEventWatch
 * 旁路直连 cos_crown_encoder_report()。 */
#define LV_USE_SDL              1
#define LV_SDL_INCLUDE_PATH     <SDL2/SDL.h>
#define LV_SDL_DIRECT_EXIT      1
/* 软件渲染(与真机 SW 渲染一致):LVGL 默认走 SDL_RENDERER_ACCELERATED
 * (Windows = D3D),会让 PrintWindow/BitBlt 类截图和录屏抓到纯黑窗口,
 * 妨碍可视化调试;240×240 用 SDL_RENDERER_SOFTWARE 性能无压力。 */
#define LV_SDL_ACCELERATED      0

/* ── LVGL 文件系统盘符 ──────────────────────────────────
 * cos_lvgl_fs.c 把 COS 存储后端注册为 'S' 盘(SD card),
 * 并编译期检查本宏必须与 COS_LVGL_FS_LETTER 一致。
 * LVGL 内置 FS 驱动(STDIO/POSIX/WIN32)全部保持默认关闭,
 * 盘符 'S' 的驱动由 cos_lvgl_fs.c 自行注册。 */
#define LV_FS_DEFAULT_DRIVER_LETTER 'S'

/* ── HAL ─────────────────────────────────────────────── */
/* 与真机相同的时序参数(1:1):触摸响应链路周期一致 */
#define LV_DEF_REFR_PERIOD      20
#define LV_INDEV_DEF_READ_PERIOD 15
#define LV_DPI_DEF              130
#define LV_DRAW_BUF_STRIDE_ALIGN 1

/* ── 绘制引擎 ─────────────────────────────────────────── */
#define LV_USE_DRAW_SW          1

/* ── 32 个 widgets(cos_lvgl_requirements.h 强制) ─────── */
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
/* 支持 lv_font_conv 生成的压缩字体(cos_font_jbm_* / han_sans_*) */
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
