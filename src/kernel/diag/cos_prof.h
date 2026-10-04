/**
 * @file cos_prof.h
 * @brief CantoMk6 一站式性能诊断(prof 命令)公共接口
 *
 * 设计:
 *   - 采样状态机与报告生成在 cos_prof.c(Core,平台无关骨架 +
 *     #ifdef COS_PLATFORM_ESP32 的真机专项采集)。
 *   - 业务代码(主循环 / 显示驱动)只调用这里轻量的 hook;未采样时
 *     hook 立即返回,零开销。
 *   - 一条 `prof` 命令内部完成:start 采样 -> 采集窗口 -> 打印全量报告。
 *
 * 采样期间不产生任何日志/输出;报告在采样结束后一次性打印。
 */

#ifndef COS_PROF_H
#define COS_PROF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "cos_shell.h" /* cos_shell_output_cb_t */

/* ────────────────────────────────────────────────────────────────
 *  Shell 命令入口(cmd_prof 调用)
 *  prof                   默认采样 3s 后打印全量报告
 *  prof -t <ms>           指定采样时长(默认 3000ms)
 *  prof -n <frames>       指定采样帧数上限(与 -t 先到者为准)
 *  prof status            不采样,打印上一次/当前实时快照
 *  prof reset             清空统计
 * ──────────────────────────────────────────────────────────────── */
void cos_prof_cli(cos_shell_output_cb_t out, void *user, int argc, char **argv);

/* ────────────────────────────────────────────────────────────────
 *  采样钩子(由主循环 / 显示驱动调用;未采样时立即返回)
 * ──────────────────────────────────────────────────────────────── */
bool cos_prof_is_sampling(void);

/* 每个 UI 主循环迭代:frame_begin ... frame_end 包住整轮 */
void cos_prof_hook_frame_begin(void);
void cos_prof_hook_frame_end(void);

/* LVGL 渲染(lv_timer_handler)耗时 */
void cos_prof_hook_lvgl_begin(void);
void cos_prof_hook_lvgl_end(void);

/* 业务 tick(cos_dispatch_tick)耗时 */
void cos_prof_hook_app_begin(void);
void cos_prof_hook_app_end(void);

/* flush 提交(拷贝 + 字节交换 + 入队);tx_queued 为本帧排队的 SPI 事务数 */
void cos_prof_hook_flush_submit_begin(void);
void cos_prof_hook_flush_submit_end(size_t bytes, int tx_queued);

/* flush 等待(DMA 完成同步) */
void cos_prof_hook_flush_wait_begin(void);
void cos_prof_hook_flush_wait_end(void);

/* ────────────────────────────────────────────────────────────────
 *  显示信息注册(显示驱动初始化后调用一次)
 *  fb1/fb2        : 双缓冲指针(可含 NULL)
 *  buf_bytes      : 单缓冲字节数
 *  bufs           : 缓冲个数
 *  width/height   : 面板分辨率
 *  bits_per_pixel : 16 = RGB565
 *  spi_clk_hz     : SPI 时钟
 *  tx_per_flush   : 每次 flush 入队的 SPI 事务数(GC9A01 = 6)
 *  max_tx_bytes   : 单次像素事务最大字节数
 * ──────────────────────────────────────────────────────────────── */
void cos_prof_set_display_info(const void *fb1, const void *fb2, size_t buf_bytes,
                               int bufs, int width, int height, int bits_per_pixel,
                               uint32_t spi_clk_hz, int tx_per_flush,
                               size_t max_tx_bytes);

#ifdef __cplusplus
}
#endif

#endif /* COS_PROF_H */
