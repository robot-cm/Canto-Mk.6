/**
 * @file cos_prof.c
 * @brief CantoMk6 一站式性能诊断实现(prof 命令)
 *
 * 覆盖:
 *   1. 采样概览(duration / frames / FPS)
 *   2. 帧耗时(count/avg/min/max/P50/P95/P99,原始样本精确分位)
 *   3. 阶段拆解(lvgl 渲染 / flush 提交 / flush 等待 / 业务 tick / 空闲)
 *   4. LVGL 侧(每帧 flush 区域数 / 重绘像素数 / us-per-px)
 *   5. 帧节拍 PACING(lv_timer_handler 调用数/间隔, ui 任务 busy vs 阻塞)
 *   6. CPU 专项(主频/DFS、per-core busy/idle、关键任务、ISR 估算、cache miss)
 *   7. PSRAM / Octal 专项(FB、带宽、flush 拆解、DMA、配置、内存分布)
 *   8. SPI / 显示(事务数/帧、字节/次、时钟)
 *   9. Diagnosis 自动结论 + 优化建议
 *
 * 采样期间不输出任何日志;报告在 prof 结束时一次性打印。
 */

#include "cos_prof.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include "cos_mem.h"

/* ────────────────────────────────────────────────────────────────
 *  平台相关采集(仅真机 ESP32-S3)
 * ──────────────────────────────────────────────────────────────── */
#ifdef COS_PLATFORM_ESP32
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_cpu.h"
#include "esp_attr.h"
#include "esp_memory_utils.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h" /* xTaskGetCoreID() */
#include "cos_net_wifi.h"
#include "cos_net_bt.h"
#endif

/* ────────────────────────────────────────────────────────────────
 *  常量
 * ──────────────────────────────────────────────────────────────── */
#define PROF_MAX_TASKS  12u
#define PROF_SAMPLE_CAP 4096u   /* 原始帧耗时样本上限(精确分位,16KB) */
#define PROF_DEFAULT_MS 3000u

/* ────────────────────────────────────────────────────────────────
 *  报告数据结构
 * ──────────────────────────────────────────────────────────────── */
typedef struct
{
    char     name[16];
    uint32_t cpu_pm;    /* 每千分比(‰) */
    uint32_t prio;
    uint32_t stack_hwm; /* 字节 */
    int      core;      /* 0/1, -1 = 任意(tskNO_AFFINITY) */
    bool     spiram;    /* 栈位于外部 PSRAM */
} prof_task_info_t;

typedef struct
{
    bool     valid;

    uint32_t dur_ms;
    uint32_t frames;

    uint32_t fmin_us;
    uint32_t fmax_us;
    uint64_t fsum_us;
    uint32_t p50_us;
    uint32_t p95_us;
    uint32_t p99_us;

    uint64_t render_us; /* 纯渲染 = lvgl - flush_sub - flush_wait */
    uint64_t sub_us;
    uint64_t wait_us;
    uint64_t app_us;
    uint64_t idle_us;

    uint64_t regions;   /* flush 区域总数(≈ 无效化区域数) */
    uint64_t pixels;    /* 重绘像素总数 */
    uint64_t tx;        /* SPI 事务总数 */
    uint64_t bytes;     /* 提交字节总数 */

    /* 帧节拍 */
    uint32_t iter_count;   /* 采样窗口内 lv_timer_handler 调用次数 */
    uint64_t lvgl_all_us;  /* 上述调用累计墙钟耗时 */
    uint32_t max_gap_us;   /* 相邻主循环迭代最大间隔 */
    uint32_t ui_run_us;    /* ui 任务窗口内运行时间(µs) */

    uint32_t freq_cur;
    uint32_t freq_min;
    uint32_t freq_max;
    uint32_t freq_changes;

    int      core_busy_pm[2];
    int      core_idle_pm[2];

    uint32_t n_tasks;
    prof_task_info_t tasks[PROF_MAX_TASKS];

    uint32_t isr_spi_per_s;
    uint32_t isr_est_pm;
    bool     wifi_on;
    bool     bt_on;
} prof_stats_t;

/* ────────────────────────────────────────────────────────────────
 *  采样状态
 * ──────────────────────────────────────────────────────────────── */
static volatile bool s_active = false;
/* 本迭代是否属于一次有效采样帧。用于消除"采样标志在两钩子之间翻转"导致的
 * 陈旧时间戳误差(会把整机 uptime 记成一次 lv_timer_handler 耗时)。 */
static bool s_frame_valid = false;

static uint32_t s_dur_ms     = PROF_DEFAULT_MS;
static uint32_t s_max_frames = 0;
static int64_t  s_start_us   = 0;

/* 每帧临时量 */
static int64_t  s_frame_begin_us = 0;
static int64_t  s_lvgl_begin_us  = 0;
static int64_t  s_app_begin_us   = 0;
static int64_t  s_sub_begin_us   = 0;
static int64_t  s_wait_begin_us  = 0;
static uint64_t s_cur_lvgl_us    = 0;
static uint64_t s_cur_app_us     = 0;
static uint64_t s_cur_sub_us     = 0;
static uint64_t s_cur_wait_us    = 0;
static uint64_t s_cur_bytes      = 0;
static uint32_t s_cur_regions    = 0;
static uint32_t s_cur_tx         = 0;

/* 窗口累计量(仅 ui_task 上下文写入) */
static volatile uint32_t s_frames = 0;
static uint32_t s_fmin_us = 0;
static uint32_t s_fmax_us = 0;
static uint64_t s_fsum_us = 0;
static uint64_t s_render_us = 0;
static uint64_t s_sub_us    = 0;
static uint64_t s_wait_us   = 0;
static uint64_t s_app_us    = 0;
static uint64_t s_idle_us   = 0;
static uint64_t s_regions   = 0;
static uint64_t s_pixels    = 0;
static uint64_t s_tx        = 0;
static uint64_t s_bytes     = 0;
static uint32_t s_freq_min  = 0;
static uint32_t s_freq_max  = 0;
static uint32_t s_freq_last = 0;
static uint32_t s_freq_changes = 0;

/* 帧节拍累计 */
static uint32_t s_iter_count  = 0;
static uint64_t s_lvgl_all_us = 0;
static uint32_t s_max_gap_us  = 0;
static int64_t  s_prev_begin_us = 0;

/* 原始帧耗时样本(精确分位,PSRAM) */
static uint32_t *s_samples  = NULL;
static uint32_t  s_sample_n = 0;

/* 任务快照(start 时) */
static void *s_tA = NULL;         /* TaskStatus_t* (避免无 ESP 头时引用类型) */
static uint32_t s_tA_n = 0;
static uint32_t s_tA_total = 0;

/* 显示信息 */
static const void *s_fb1 = NULL;
static const void *s_fb2 = NULL;
static size_t s_fb_bytes = 0;
static int    s_fb_bufs  = 0;
static int    s_fb_w     = 0;
static int    s_fb_h     = 0;
static int    s_fb_bpp   = 16;
static uint32_t s_spi_clk = 0;
static int    s_tx_per_flush = 0;
static size_t s_max_tx_bytes = 0;

/* 最近一次完成的统计 */
static prof_stats_t s_last;

/* ────────────────────────────────────────────────────────────────
 *  小工具
 * ──────────────────────────────────────────────────────────────── */
static void p_out(cos_shell_output_cb_t out, void *user, const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (out)
        out(buf, user);
}

static int prof_cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* 已升序数组的第 pct 百分位 */
static uint32_t prof_pctl(const uint32_t *v, uint32_t n, int pct)
{
    if (n == 0)
        return 0;
    uint32_t idx = (uint32_t)((uint64_t)n * (uint32_t)pct / 100u);
    if (idx >= n)
        idx = n - 1u;
    return v[idx];
}

/* ────────────────────────────────────────────────────────────────
 *  采样钩子
 * ──────────────────────────────────────────────────────────────── */
bool cos_prof_is_sampling(void)
{
    return s_active;
}

/* 每个 *_begin 在非采样帧写 0,*_end 仅在 begin 时间戳有效时累加。
 * 这样即使采样标志在两钩子之间翻转,也不会把陈旧时间戳算进来。 */
void cos_prof_hook_frame_begin(void)
{
#ifdef COS_PLATFORM_ESP32
    s_frame_valid = s_active;
    if (!s_frame_valid)
        return;
    int64_t now = esp_timer_get_time();

    /* 帧节拍:相邻两次主循环迭代的间隔(检测主循环被阻塞) */
    if (s_prev_begin_us != 0)
    {
        uint32_t gap = (uint32_t)(now - s_prev_begin_us);
        if (gap > s_max_gap_us)
            s_max_gap_us = gap;
    }
    s_prev_begin_us = now;

    s_frame_begin_us = now;
    s_iter_count++;
    s_cur_lvgl_us = 0;
    s_cur_app_us  = 0;
    s_cur_sub_us  = 0;
    s_cur_wait_us = 0;
    s_cur_bytes   = 0;
    s_cur_regions = 0;
    s_cur_tx      = 0;

    /* DFS 观测:每帧采样一次当前主频 */
    uint32_t f = (uint32_t)esp_clk_cpu_freq();
    if (f != s_freq_last)
    {
        s_freq_changes++;
        s_freq_last = f;
    }
    if (s_freq_min == 0 || f < s_freq_min)
        s_freq_min = f;
    if (f > s_freq_max)
        s_freq_max = f;
#endif
}

void cos_prof_hook_frame_end(void)
{
#ifdef COS_PLATFORM_ESP32
    if (!s_frame_valid)
        return;
    s_frame_valid = false;
    if (s_cur_regions == 0)
        return; /* 本次迭代没有刷新,不计入帧 */

    int64_t now = esp_timer_get_time();
    uint32_t frame_us = (uint32_t)(now - s_frame_begin_us);

    s_frames++;
    s_fsum_us += frame_us;
    if (s_fmin_us == 0 || frame_us < s_fmin_us)
        s_fmin_us = frame_us;
    if (frame_us > s_fmax_us)
        s_fmax_us = frame_us;
    if (s_samples && s_sample_n < PROF_SAMPLE_CAP)
        s_samples[s_sample_n++] = frame_us;

    uint64_t lvgl  = s_cur_lvgl_us;
    uint64_t sub   = s_cur_sub_us;
    uint64_t wait  = s_cur_wait_us;
    uint64_t app   = s_cur_app_us;
    uint64_t render = (lvgl > sub + wait) ? (lvgl - sub - wait) : 0;
    uint64_t idle   = (frame_us > lvgl + app) ? (frame_us - lvgl - app) : 0;

    s_render_us += render;
    s_sub_us    += sub;
    s_wait_us   += wait;
    s_app_us    += app;
    s_idle_us   += idle;
    s_regions   += s_cur_regions;
    s_pixels    += s_cur_bytes / 2u;
    s_tx        += s_cur_tx;
    s_bytes     += s_cur_bytes;
#endif
}

void cos_prof_hook_lvgl_begin(void)
{
#ifdef COS_PLATFORM_ESP32
    s_lvgl_begin_us = s_frame_valid ? esp_timer_get_time() : 0;
#endif
}

void cos_prof_hook_lvgl_end(void)
{
#ifdef COS_PLATFORM_ESP32
    if (s_frame_valid && s_lvgl_begin_us != 0)
    {
        uint64_t dt = (uint64_t)(esp_timer_get_time() - s_lvgl_begin_us);
        s_cur_lvgl_us += dt;   /* 本迭代(用于帧阶段拆解) */
        s_lvgl_all_us += dt;   /* 全迭代累计(用于帧节拍) */
    }
    s_lvgl_begin_us = 0;
#endif
}

void cos_prof_hook_app_begin(void)
{
#ifdef COS_PLATFORM_ESP32
    s_app_begin_us = s_frame_valid ? esp_timer_get_time() : 0;
#endif
}

void cos_prof_hook_app_end(void)
{
#ifdef COS_PLATFORM_ESP32
    if (s_frame_valid && s_app_begin_us != 0)
        s_cur_app_us += (uint64_t)(esp_timer_get_time() - s_app_begin_us);
    s_app_begin_us = 0;
#endif
}

void cos_prof_hook_flush_submit_begin(void)
{
#ifdef COS_PLATFORM_ESP32
    s_sub_begin_us = s_frame_valid ? esp_timer_get_time() : 0;
#endif
}

void cos_prof_hook_flush_submit_end(size_t bytes, int tx_queued)
{
#ifdef COS_PLATFORM_ESP32
    if (!s_frame_valid || s_sub_begin_us == 0)
        return;
    s_cur_sub_us += (uint64_t)(esp_timer_get_time() - s_sub_begin_us);
    s_sub_begin_us = 0;
    s_cur_regions++;
    s_cur_bytes += bytes;
    if (tx_queued > 0)
        s_cur_tx += (uint32_t)tx_queued;
#else
    (void)bytes;
    (void)tx_queued;
#endif
}

void cos_prof_hook_flush_wait_begin(void)
{
#ifdef COS_PLATFORM_ESP32
    s_wait_begin_us = s_frame_valid ? esp_timer_get_time() : 0;
#endif
}

void cos_prof_hook_flush_wait_end(void)
{
#ifdef COS_PLATFORM_ESP32
    if (s_frame_valid && s_wait_begin_us != 0)
        s_cur_wait_us += (uint64_t)(esp_timer_get_time() - s_wait_begin_us);
    s_wait_begin_us = 0;
#endif
}

/* ────────────────────────────────────────────────────────────────
 *  显示信息
 * ──────────────────────────────────────────────────────────────── */
void cos_prof_set_display_info(const void *fb1, const void *fb2, size_t buf_bytes,
                               int bufs, int width, int height, int bits_per_pixel,
                               uint32_t spi_clk_hz, int tx_per_flush,
                               size_t max_tx_bytes)
{
    s_fb1 = fb1;
    s_fb2 = fb2;
    s_fb_bytes = buf_bytes;
    s_fb_bufs = bufs;
    s_fb_w = width;
    s_fb_h = height;
    s_fb_bpp = bits_per_pixel;
    s_spi_clk = spi_clk_hz;
    s_tx_per_flush = tx_per_flush;
    s_max_tx_bytes = max_tx_bytes;
}

/* ────────────────────────────────────────────────────────────────
 *  采样控制(真机)
 * ──────────────────────────────────────────────────────────────── */
#ifdef COS_PLATFORM_ESP32

static void prof_tasks_begin(void)
{
    if (s_tA)
    {
        cos_free(s_tA);
        s_tA = NULL;
    }
    s_tA_n = 0;
    s_tA_total = 0;

    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0)
        return;
    TaskStatus_t *a = (TaskStatus_t *)cos_malloc(n * sizeof(TaskStatus_t));
    if (!a)
        return;
    s_tA_n = (uint32_t)uxTaskGetSystemState(a, n, &s_tA_total);
    s_tA = a;
}

/* 计算两帧快照之间的每任务 CPU 增量,并填充 per-core 与 top 任务 */
static void prof_tasks_end(prof_stats_t *st)
{
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0)
        return;
    TaskStatus_t *b = (TaskStatus_t *)cos_malloc(n * sizeof(TaskStatus_t));
    if (!b)
        return;
    uint32_t total_b = 0;
    UBaseType_t nb = uxTaskGetSystemState(b, n, &total_b);

    TaskStatus_t *a = (TaskStatus_t *)s_tA;
    uint64_t core_idle[2] = {0, 0};

    uint32_t best[PROF_MAX_TASKS];
    uint64_t best_dr[PROF_MAX_TASKS];
    for (uint32_t i = 0; i < PROF_MAX_TASKS; i++)
    {
        best[i] = 0;
        best_dr[i] = 0;
    }

    for (UBaseType_t k = 0; k < nb; k++)
    {
        uint64_t dr = 0;
        if (a)
        {
            for (uint32_t j = 0; j < s_tA_n; j++)
            {
                if (a[j].xTaskNumber == b[k].xTaskNumber)
                {
                    dr = (uint64_t)(uint32_t)(b[k].ulRunTimeCounter - a[j].ulRunTimeCounter);
                    break;
                }
            }
        }

        BaseType_t cid = xTaskGetCoreID(b[k].xHandle);
        int core = (cid == tskNO_AFFINITY) ? -1 : (int)cid;

        bool is_idle = (strncmp(b[k].pcTaskName, "IDLE", 4) == 0);
        if (is_idle && (core == 0 || core == 1))
            core_idle[core] += dr;

        if (b[k].pcTaskName && strcmp(b[k].pcTaskName, "ui") == 0)
            st->ui_run_us = (uint32_t)dr;

        /* 插入 top 榜(替换最小值) */
        uint32_t min_i = 0;
        for (uint32_t i = 1; i < PROF_MAX_TASKS; i++)
            if (best_dr[i] < best_dr[min_i])
                min_i = i;
        if (dr > best_dr[min_i])
        {
            best_dr[min_i] = dr;
            best[min_i] = k;
        }
    }

    /* per-core busy = 1 - 该核 IDLE 任务运行时间 / 窗口。
     * 不能用"该核任务之和":未绑核任务(swdraw/tiT)会漏算,
     * 导致其实际所在核的 busy 被严重低估(实测 Core0 曾误报 2%)。 */
    {
        uint64_t win_us = (uint64_t)(st->dur_ms ? st->dur_ms : 1u) * 1000u;
        for (int c = 0; c < 2; c++)
        {
            int busy = 1000 - (int)(core_idle[c] * 1000u / win_us);
            if (busy < 0) busy = 0;
            if (busy > 1000) busy = 1000;
            st->core_busy_pm[c] = busy;
            st->core_idle_pm[c] = 1000 - busy;
        }
    }

    for (uint32_t i = 0; i < PROF_MAX_TASKS; i++)
    {
        for (uint32_t j = i + 1; j < PROF_MAX_TASKS; j++)
        {
            if (best_dr[j] > best_dr[i])
            {
                uint64_t t = best_dr[i]; best_dr[i] = best_dr[j]; best_dr[j] = t;
                uint32_t u = best[i]; best[i] = best[j]; best[j] = u;
            }
        }
    }

    uint32_t total = (total_b >= s_tA_total) ? (total_b - s_tA_total) : 1u;
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < PROF_MAX_TASKS && cnt < PROF_MAX_TASKS; i++)
    {
        if (best_dr[i] == 0)
            continue;
        TaskStatus_t *t = &b[best[i]];
        prof_task_info_t *ti = &st->tasks[cnt];
        snprintf(ti->name, sizeof(ti->name), "%s", t->pcTaskName ? t->pcTaskName : "?");
        ti->cpu_pm = (uint32_t)(best_dr[i] * 1000u / total);
        ti->prio = (uint32_t)t->uxCurrentPriority;
        ti->stack_hwm = (uint32_t)t->usStackHighWaterMark;
        BaseType_t cid = xTaskGetCoreID(t->xHandle);
        ti->core = (cid == tskNO_AFFINITY) ? -1 : (int)cid; /* 修复:未绑核 -> any */
        ti->spiram = (t->pxStackBase != NULL) && esp_ptr_external_ram(t->pxStackBase);
        cnt++;
    }
    st->n_tasks = cnt;

    cos_free(b);
}

static void prof_start(uint32_t dur_ms, uint32_t max_frames)
{
    s_dur_ms = dur_ms;
    s_max_frames = max_frames;

    s_frames = 0;
    s_fmin_us = 0;
    s_fmax_us = 0;
    s_fsum_us = 0;
    s_render_us = 0;
    s_sub_us = 0;
    s_wait_us = 0;
    s_app_us = 0;
    s_idle_us = 0;
    s_regions = 0;
    s_pixels = 0;
    s_tx = 0;
    s_bytes = 0;
    s_iter_count = 0;
    s_lvgl_all_us = 0;
    s_max_gap_us = 0;
    s_prev_begin_us = 0;
    s_sample_n = 0;
    s_freq_last = (uint32_t)esp_clk_cpu_freq(); /* 基线:首帧不计为 DFS 变化 */
    s_freq_min = s_freq_last;
    s_freq_max = s_freq_last;
    s_freq_changes = 0;

    if (!s_samples)
    {
        s_samples = (uint32_t *)heap_caps_malloc(PROF_SAMPLE_CAP * 4, MALLOC_CAP_SPIRAM);
        if (!s_samples)
            s_samples = (uint32_t *)heap_caps_malloc(PROF_SAMPLE_CAP * 4,
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    s_start_us = esp_timer_get_time();
    prof_tasks_begin();
    s_active = true;
}

static void prof_finish(void)
{
    s_active = false;
    /* 让可能正在执行的 ui_task 帧钩子收尾,避免读到撕裂的累计值 */
    vTaskDelay(pdMS_TO_TICKS(2));
    int64_t end_us = esp_timer_get_time();

    memset(&s_last, 0, sizeof(s_last));
    s_last.dur_ms = (uint32_t)((end_us - s_start_us) / 1000);
    s_last.frames = s_frames;
    s_last.fmin_us = s_fmin_us;
    s_last.fmax_us = s_fmax_us;
    s_last.fsum_us = s_fsum_us;
    s_last.render_us = s_render_us;
    s_last.sub_us = s_sub_us;
    s_last.wait_us = s_wait_us;
    s_last.app_us = s_app_us;
    s_last.idle_us = s_idle_us;
    s_last.regions = s_regions;
    s_last.pixels = s_pixels;
    s_last.tx = s_tx;
    s_last.bytes = s_bytes;
    s_last.iter_count = s_iter_count;
    s_last.lvgl_all_us = s_lvgl_all_us;
    s_last.max_gap_us = s_max_gap_us;
    s_last.freq_cur = (uint32_t)esp_clk_cpu_freq();
    s_last.freq_min = s_freq_min;
    s_last.freq_max = s_freq_max;
    s_last.freq_changes = s_freq_changes;

    /* 精确分位:对原始样本排序 */
    if (s_samples && s_sample_n > 0)
    {
        qsort(s_samples, s_sample_n, sizeof(uint32_t), prof_cmp_u32);
        s_last.p50_us = prof_pctl(s_samples, s_sample_n, 50);
        s_last.p95_us = prof_pctl(s_samples, s_sample_n, 95);
        s_last.p99_us = prof_pctl(s_samples, s_sample_n, 99);
    }
    else
    {
        s_last.p50_us = s_last.p95_us = s_last.p99_us =
            s_last.frames ? (uint32_t)(s_last.fsum_us / s_last.frames) : 0;
    }
    if (s_samples)
    {
        heap_caps_free(s_samples);
        s_samples = NULL;
    }

    prof_tasks_end(&s_last);

    /* ISR 估算:每次 SPI DMA 事务完成触发一次 DMA ISR,按 ~6us/次估算 */
    uint32_t dur = s_last.dur_ms ? s_last.dur_ms : 1u;
    s_last.isr_spi_per_s = (uint32_t)(s_last.tx * 1000u / dur);
    s_last.isr_est_pm = (uint32_t)(s_last.tx * 6u * 1000u / ((uint64_t)dur * 1000u));
    s_last.wifi_on = cos_net_wifi_is_enabled();
    s_last.bt_on = cos_net_bt_is_enabled();

    s_last.valid = true;

    if (s_tA)
    {
        cos_free(s_tA);
        s_tA = NULL;
    }
}

/* ── cache miss / PSRAM 带宽基准(报告阶段运行,~ms 级) ── */
static volatile uint32_t s_bench_sink;

static IRAM_ATTR uint32_t prof_stall_loop(const uint32_t *p, size_t n, int reps)
{
    uint32_t s = 0;
    for (int r = 0; r < reps; r++)
        for (size_t i = 0; i < n; i++)
            s += p[i];
    s_bench_sink = s;
    return s;
}

#define PROF_STALL_WORDS (8192u) /* 32KB 上限,内部 RAM 不足时逐级减半 */

/* 固定负载:同一循环分别跑在 INTERNAL / PSRAM 数据上,比较 cycle 倍数。
 * 返回 false 表示内部 RAM 装不下参照缓冲(此时不输出假的 1.0x)。 */
static bool prof_cache_stall(uint32_t *int_x10, uint32_t *psram_x10)
{
    *int_x10 = 10;
    *psram_x10 = 10;

    size_t words = PROF_STALL_WORDS;
    uint32_t *ib = NULL;
    while (words >= 512u) /* 最小 2KB */
    {
        ib = (uint32_t *)heap_caps_malloc(words * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (ib)
            break;
        words >>= 1;
    }
    if (!ib)
        return false;

    uint32_t *pb = (uint32_t *)heap_caps_malloc(words * 4, MALLOC_CAP_SPIRAM);
    if (!pb)
    {
        heap_caps_free(ib);
        return false;
    }
    for (size_t i = 0; i < words; i++)
    {
        ib[i] = (uint32_t)i;
        pb[i] = (uint32_t)i;
    }

    uint32_t c0 = esp_cpu_get_cycle_count();
    prof_stall_loop(ib, words, 8);
    uint32_t c1 = esp_cpu_get_cycle_count();
    uint32_t ci = c1 - c0;

    c0 = esp_cpu_get_cycle_count();
    prof_stall_loop(pb, words, 8);
    c1 = esp_cpu_get_cycle_count();
    uint32_t cp = c1 - c0;

    if (ci > 0)
    {
        uint32_t r = (uint32_t)((uint64_t)cp * 10u / ci);
        if (r < 10) r = 10;
        *psram_x10 = r;
    }

    heap_caps_free(ib);
    heap_caps_free(pb);
    return true;
}

/* 读/写带宽(MB/s = bytes/us);取 3 次最好值以降低与 UI 并发带来的噪声 */
static uint32_t prof_bw_once(uint32_t *buf, size_t n, bool write)
{
    uint64_t dt;
    if (write)
    {
        int64_t t0 = esp_timer_get_time();
        for (size_t i = 0; i < n; i++)
            buf[i] = (uint32_t)i;
        dt = (uint64_t)(esp_timer_get_time() - t0);
    }
    else
    {
        uint32_t sum = 0;
        int64_t t0 = esp_timer_get_time();
        for (size_t i = 0; i < n; i++)
            sum += buf[i];
        dt = (uint64_t)(esp_timer_get_time() - t0);
        s_bench_sink = sum;
    }
    return dt ? (uint32_t)((uint64_t)(n * 4u) / dt) : 0;
}

static void prof_psram_bw(uint32_t *read_mbps, uint32_t *write_mbps)
{
    *read_mbps = 0;
    *write_mbps = 0;
    const size_t bytes = 512u * 1024u;
    uint32_t *buf = (uint32_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!buf)
        return;
    size_t n = bytes / 4;

    for (int r = 0; r < 3; r++)
    {
        uint32_t w = prof_bw_once(buf, n, true);
        uint32_t rd = prof_bw_once(buf, n, false);
        if (w > *write_mbps) *write_mbps = w;
        if (rd > *read_mbps) *read_mbps = rd;
    }

    heap_caps_free(buf);
}

#endif /* COS_PLATFORM_ESP32 */

/* ────────────────────────────────────────────────────────────────
 *  报告生成
 * ──────────────────────────────────────────────────────────────── */
static const char *prof_mem_region(const void *p)
{
#ifdef COS_PLATFORM_ESP32
    if (p == NULL)
        return "?";
    if (esp_ptr_external_ram(p))
        return "SPIRAM";
    if (esp_ptr_internal(p))
        return "INT";
    return "?";
#else
    (void)p;
    return "?";
#endif
}

static void prof_report(cos_shell_output_cb_t out, void *user, bool from_status)
{
    if (!s_last.valid)
    {
        p_out(out, user, "========== PROF REPORT ==========");
        p_out(out, user, "no sample yet. run 'prof' to start sampling.");
        return;
    }

    const prof_stats_t *s = &s_last;
    uint32_t frames = s->frames ? s->frames : 1u;
    uint32_t dur_ms = s->dur_ms ? s->dur_ms : 1u;
    uint32_t dur_us = dur_ms * 1000u;

    uint32_t avg_frame_us = (uint32_t)(s->fsum_us / frames);
    uint32_t render_avg  = (uint32_t)(s->render_us / frames);
    uint32_t wait_avg    = (uint32_t)(s->wait_us / frames);
    uint32_t pixels_avg  = (uint32_t)(s->pixels / frames);
    uint32_t uspx_x100   = pixels_avg ? (uint32_t)((uint64_t)render_avg * 100u / pixels_avg) : 0;

    p_out(out, user, "========== PROF REPORT ==========");
    p_out(out, user, "Sample: %u.%02us | %u frames | FPS %u.%u%s",
          dur_ms / 1000u, (dur_ms % 1000u) / 10u,
          (unsigned)s->frames,
          (unsigned)((uint64_t)frames * 1000u / dur_ms) / 10u,
          (unsigned)((uint64_t)frames * 1000u / dur_ms) % 10u,
          from_status ? "  [last sample]" : "");
    p_out(out, user, "----------------------------------");

    /* 1/2. 帧耗时 */
    p_out(out, user, "FRAME");
    p_out(out, user, "  count %u | ms: avg %u.%u | min %u.%u | max %u.%u",
          (unsigned)s->frames,
          avg_frame_us / 1000u, (avg_frame_us % 1000u) / 100u,
          s->fmin_us / 1000u, (s->fmin_us % 1000u) / 100u,
          s->fmax_us / 1000u, (s->fmax_us % 1000u) / 100u);
    p_out(out, user, "  P50 %u.%u | P95 %u.%u | P99 %u.%u",
          s->p50_us / 1000u, (s->p50_us % 1000u) / 100u,
          s->p95_us / 1000u, (s->p95_us % 1000u) / 100u,
          s->p99_us / 1000u, (s->p99_us % 1000u) / 100u);
    p_out(out, user, "  breakdown(us): lvgl %u | fl_sub %u | fl_wait %u | app %u | idle %u",
          (unsigned)(s->render_us / frames),
          (unsigned)(s->sub_us / frames),
          (unsigned)(s->wait_us / frames),
          (unsigned)(s->app_us / frames),
          (unsigned)(s->idle_us / frames));
    if (s->frames == 0)
        p_out(out, user, "  (no refresh frames: screen static during sample window)");

    /* 帧节拍:主循环是否健康 / ui 任务是否被阻塞 */
    p_out(out, user, "PACING");
    p_out(out, user, "  lv_timer_handler %u calls (%u/s) | total %ums (%u%% of window)",
          (unsigned)s->iter_count,
          (unsigned)((uint64_t)s->iter_count * 1000u / dur_ms),
          (unsigned)(s->lvgl_all_us / 1000u),
          (unsigned)(s->lvgl_all_us * 100u / (dur_us ? dur_us : 1u)));
    p_out(out, user, "  refresh %u | no-refresh %u | max loop gap %u.%ums",
          (unsigned)s->frames,
          (unsigned)(s->iter_count > s->frames ? s->iter_count - s->frames : 0),
          s->max_gap_us / 1000u, (s->max_gap_us % 1000u) / 100u);
    if (s->lvgl_all_us > 0)
    {
        uint32_t ratio = (uint32_t)((uint64_t)s->ui_run_us * 100u / s->lvgl_all_us);
        const char *verdict = (ratio >= 80u) ? "CPU-bound (not blocked)"
                            : (ratio <= 40u) ? "mostly blocked (waiting on lock/sem)"
                                             : "mixed busy/blocked";
        p_out(out, user, "  ui runtime %ums vs handler wall %ums -> %s",
              (unsigned)(s->ui_run_us / 1000u), (unsigned)(s->lvgl_all_us / 1000u), verdict);
    }

    /* 4. LVGL 侧 */
    p_out(out, user, "LVGL");
    p_out(out, user, "  inv %u.%u/frame | redraw %uk px/frame | %u.%u us/px",
          (unsigned)(s->regions / frames),
          (unsigned)((s->regions % frames) * 10u / frames),
          (unsigned)(pixels_avg / 1000u),
          uspx_x100 / 100u, uspx_x100 % 100u);

    /* 5. CPU 专项 */
    p_out(out, user, "CPU");
    if (s->freq_changes == 0)
    {
        p_out(out, user, "  freq: %u MHz (no DFS change)",
              (unsigned)(s->freq_cur / 1000000u));
    }
    else
    {
        p_out(out, user, "  freq: %u MHz (DFS changed %u times: %u-%u MHz)",
              (unsigned)(s->freq_cur / 1000000u), (unsigned)s->freq_changes,
              (unsigned)(s->freq_min / 1000000u), (unsigned)(s->freq_max / 1000000u));
    }
#ifdef COS_PLATFORM_ESP32
    {
        esp_pm_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (esp_pm_get_configuration(&cfg) == ESP_OK)
            p_out(out, user, "  DFS cfg: %d-%d MHz (auto light-sleep %s)",
                  (int)cfg.min_freq_mhz, (int)cfg.max_freq_mhz,
                  cfg.light_sleep_enable ? "on" : "off");
    }
#endif
    p_out(out, user, "  Core0: busy %d.%d%% idle %d.%d%%",
          s->core_busy_pm[0] / 10, s->core_busy_pm[0] % 10,
          s->core_idle_pm[0] / 10, s->core_idle_pm[0] % 10);
    p_out(out, user, "  Core1: busy %d.%d%% idle %d.%d%%",
          s->core_busy_pm[1] / 10, s->core_busy_pm[1] % 10,
          s->core_idle_pm[1] / 10, s->core_idle_pm[1] % 10);

    /* 关键任务 */
    p_out(out, user, "  tasks:");
    for (uint32_t i = 0; i < s->n_tasks; i++)
    {
        const prof_task_info_t *t = &s->tasks[i];
        char core_s[8];
        if (t->core < 0) snprintf(core_s, sizeof(core_s), "any");
        else snprintf(core_s, sizeof(core_s), "c%d", t->core);
        p_out(out, user, "    %-10s %u.%u%%@%s p%u stk%u.%uk %s",
              t->name, t->cpu_pm / 10, t->cpu_pm % 10, core_s, (unsigned)t->prio,
              (unsigned)(t->stack_hwm / 1024u), (unsigned)((t->stack_hwm % 1024u) / 100u),
              t->spiram ? "SPIRAM" : "INT");
    }
    if (s->n_tasks == 0)
        p_out(out, user, "    (run-time stats unavailable)");

    /* ISR */
    p_out(out, user, "  ISR: wifi %s | bt %s | spi_dma %u/s | est %u.%u%% cpu",
          s->wifi_on ? "on(n/a)" : "0/s",
          s->bt_on ? "on(n/a)" : "0/s",
          (unsigned)s->isr_spi_per_s,
          s->isr_est_pm / 10, s->isr_est_pm % 10);

    /* cache miss stall + PSRAM 专项 */
    p_out(out, user, "PSRAM / OCTAL");
#ifdef COS_PLATFORM_ESP32
    {
        uint32_t ix10 = 10, px10 = 10;
        if (prof_cache_stall(&ix10, &px10))
            p_out(out, user, "  cache stall: internal %u.%ux | psram %u.%ux (penalty %u.%ux)",
                  ix10 / 10, ix10 % 10, px10 / 10, px10 % 10, px10 / 10, px10 % 10);
        else
            p_out(out, user, "  cache stall: n/a (INTERNAL free too low for reference buffer)");

        uint32_t rd = 0, wr = 0;
        prof_psram_bw(&rd, &wr);
        p_out(out, user, "  BW: best-of-3 %u MB/s read + %u MB/s write", (unsigned)rd, (unsigned)wr);
    }
#endif

    if (s_fb1 || s_fb2)
    {
        const void *fb = s_fb1 ? s_fb1 : s_fb2;
        p_out(out, user, "  FB: @%p %s %dx%d RGB565 | %d buf | %uKB",
              fb, prof_mem_region(fb), s_fb_w, s_fb_h, s_fb_bufs,
              (unsigned)((s_fb_bytes * (size_t)(s_fb_bufs ? s_fb_bufs : 1)) / 1024u));
    }
    else
    {
        p_out(out, user, "  FB: (not registered)");
    }

    /* flush 拆解 */
    {
        uint32_t sub_ms_x10  = (uint32_t)(s->sub_us / frames / 100u);
        uint32_t wait_ms_x10 = (uint32_t)(s->wait_us / frames / 100u);
        uint64_t theory_us = 0;
        if (s_spi_clk > 0)
            theory_us = (uint64_t)(s->bytes / frames) * 8u * 1000000u / s_spi_clk;
        uint64_t total_us = s->sub_us / frames + s->wait_us / frames;
        uint64_t overhead_us = (total_us > theory_us) ? (total_us - theory_us) : 0;
        p_out(out, user, "  flush: submit %u.%ums | wait %u.%ums | theory_spi %u.%ums | overhead %u.%ums",
              sub_ms_x10 / 10, sub_ms_x10 % 10,
              wait_ms_x10 / 10, wait_ms_x10 % 10,
              (unsigned)(theory_us / 1000u), (unsigned)((theory_us % 1000u) / 100u),
              (unsigned)(overhead_us / 1000u), (unsigned)((overhead_us % 1000u) / 100u));
    }

    {
        int desc = s_tx_per_flush * (s_fb_bufs ? s_fb_bufs : 1);
        p_out(out, user, "  DMA: %d desc (%d/flush x%d) | bounce=no",
              desc, s_tx_per_flush, s_fb_bufs ? s_fb_bufs : 1);
    }
    p_out(out, user, "  SPIRAM_FETCH_INSTR=%s | SPIRAM_RODATA=%s",
#if defined(CONFIG_SPIRAM_FETCH_INSTRUCTIONS) && CONFIG_SPIRAM_FETCH_INSTRUCTIONS
          "on",
#else
          "off",
#endif
#if defined(CONFIG_SPIRAM_RODATA) && CONFIG_SPIRAM_RODATA
          "on");
#else
          "off");
#endif

#ifdef COS_PLATFORM_ESP32
    {
        size_t ifree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        size_t ilarge = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        size_t imin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        size_t pfree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t plarge = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        size_t pmin = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
        p_out(out, user, "  MEM INT: free %uk (largest %uk, min %uk)",
              (unsigned)(ifree / 1024u), (unsigned)(ilarge / 1024u), (unsigned)(imin / 1024u));
        p_out(out, user, "  MEM PSRAM: free %u.%uM (largest %u.%uM, min %u.%uM)",
              (unsigned)(pfree / 1048576u), (unsigned)((pfree % 1048576u) / 104858u),
              (unsigned)(plarge / 1048576u), (unsigned)((plarge % 1048576u) / 104858u),
              (unsigned)(pmin / 1048576u), (unsigned)((pmin % 1048576u) / 104858u));
    }
#endif

    /* 7. SPI / 显示 */
    p_out(out, user, "SPI");
    {
        uint32_t tx_per_frame = (uint32_t)(s->tx / frames);
        uint32_t bytes_per_tx = s->tx ? (uint32_t)(s->bytes / s->tx) : 0;
        p_out(out, user, "  %u tx/frame | %u B/tx | %u MHz",
              (unsigned)tx_per_frame, (unsigned)bytes_per_tx,
              (unsigned)(s_spi_clk / 1000000u));
    }

    /* 9. Diagnosis */
    p_out(out, user, "----------------------------------");
    {
        uint64_t theory_us = 0;
        if (s_spi_clk > 0)
            theory_us = (uint64_t)(s->bytes / frames) * 8u * 1000000u / s_spi_clk;
        int idle_avg = (s->core_idle_pm[0] + s->core_idle_pm[1]) / 2;
        bool has_core = (s->core_busy_pm[0] + s->core_busy_pm[1] +
                         s->core_idle_pm[0] + s->core_idle_pm[1]) > 0;
        uint32_t fps_x10 = (uint32_t)((uint64_t)frames * 10000u / dur_ms);

        bool psram_bn  = has_core && (theory_us > 0) &&
                         ((uint64_t)wait_avg * 10u > theory_us * 13u) && (idle_avg > 300);
        bool lvgl_heavy = (render_avg > 5000u) && (wait_avg < 1000u);
        bool cpu_bn    = has_core && (idle_avg < 150) && (render_avg > 8000u);
        bool isr_bn    = (s->isr_est_pm > 100u);
        bool jitter    = (s->p50_us > 0) && (s->p99_us > s->p50_us * 2u);
        bool dfs       = (s->freq_changes > 0);
        bool sparse    = (s->frames > 0) && (fps_x10 < 50u); /* < 5 fps */

        const char *title = "no obvious bottleneck";
        if (psram_bn) title = "PSRAM / Octal bandwidth bottleneck";
        else if (lvgl_heavy) title = "LVGL per-frame compute dominant";
        else if (cpu_bn) title = "CPU bottleneck (render-heavy)";
        else if (isr_bn) title = "interrupt storm";
        else if (jitter) title = "occasional contention (jitter)";
        else if (dfs) title = "DFS frequency scaling";

        p_out(out, user, "DIAGNOSIS: %s", title);

        if (psram_bn)
        {
            p_out(out, user, "  -> flush_wait %ums >> theory %ums; overhead from PSRAM read/writeback",
                  (unsigned)(wait_avg / 1000u), (unsigned)(theory_us / 1000u));
            p_out(out, user, "  -> 建议1: framebuffer 改单缓冲放 INTERNAL,或降为半屏 partial");
            p_out(out, user, "  -> 建议2: 检查 CONFIG_SPIRAM_MODE_OCT 频率/引脚,必要时降 SPI 分频");
        }
        else if (lvgl_heavy)
        {
            p_out(out, user, "  -> render %ums/frame, %u.%u us/px (正常 <1.0); flush/PSRAM/SPI 可忽略",
                  (unsigned)(render_avg / 1000u), uspx_x100 / 100u, uspx_x100 % 100u);
            p_out(out, user, "  -> 建议1: 核对 lv_conf: LV_COLOR_DEPTH=16 / LV_COLOR_16_SWAP / LV_DRAW_SW_DRAW_UNIT_CNT / LV_USE_DRAW_SW_COMPLEX");
            p_out(out, user, "  -> 建议2: 在 lv_timer_handler 内细分计时(lv_refr_now vs 其它定时器回调),定位耗时来源");
        }
        else if (cpu_bn)
        {
            p_out(out, user, "  -> idle %d%% 低, lvgl 渲染 avg %uus 高", idle_avg / 10, (unsigned)render_avg);
            p_out(out, user, "  -> 建议1: 精简无效化区域/降低动画分辨率,减少每帧重绘");
            p_out(out, user, "  -> 建议2: 把 ui 任务与渲染拆到不同核,均衡 Core%d",
                  (s->core_busy_pm[1] >= s->core_busy_pm[0]) ? 1 : 0);
        }
        else if (isr_bn)
        {
            p_out(out, user, "  -> ISR 估算 %d.%d%% cpu, 中断过于频繁", s->isr_est_pm / 10, s->isr_est_pm % 10);
            p_out(out, user, "  -> 建议1: 增大 SPI 事务粒度(减少 DMA 中断次数)");
            p_out(out, user, "  -> 建议2: 关闭空闲无线/外设中断源");
        }
        else if (jitter)
        {
            p_out(out, user, "  -> P99 %ums >> P50 %ums, 存在偶发竞争/长尾",
                  (unsigned)(s->p99_us / 1000u), (unsigned)(s->p50_us / 1000u));
            p_out(out, user, "  -> 建议: 排查总线竞争 / 中断风暴 / DFS 抖动 / 偶发大区域重绘");
        }
        else if (dfs)
        {
            p_out(out, user, "  -> 主频采样期间变化 %u 次(%u-%u MHz)",
                  (unsigned)s->freq_changes,
                  (unsigned)(s->freq_min / 1000000u), (unsigned)(s->freq_max / 1000000u));
            p_out(out, user, "  -> 建议: 关键渲染路径持 esp_pm 锁,避免降频导致掉帧");
        }
        else
        {
            p_out(out, user, "  -> 指标均衡,未发现明显单点瓶颈");
        }

        if (sparse)
            p_out(out, user, "  note: refresh 仅 %u 帧/%us(UI 基本静态);FPS 低本身不是卡顿,单帧成本才是关键",
                  (unsigned)s->frames, (unsigned)(dur_ms / 1000u));
        if (s->max_gap_us > (uint32_t)(dur_us / 10u))
            p_out(out, user, "  note: 主循环最大间隔 %ums,存在明显阻塞(检查 Light Sleep/长任务)",
                  (unsigned)(s->max_gap_us / 1000u));

#ifdef COS_PLATFORM_ESP32
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 16u * 1024u)
            p_out(out, user, "  -> 注意: INTERNAL largest < 16KB,内部 RAM 偏紧,警惕分配失败");
#endif
    }
    p_out(out, user, "==================================");
}

/* ────────────────────────────────────────────────────────────────
 *  CLI
 * ──────────────────────────────────────────────────────────────── */
static void prof_reset(void)
{
    s_active = false;
    if (s_tA)
    {
        cos_free(s_tA);
        s_tA = NULL;
    }
    s_tA_n = 0;
    s_tA_total = 0;
    memset(&s_last, 0, sizeof(s_last));
}

void cos_prof_cli(cos_shell_output_cb_t out, void *user, int argc, char **argv)
{
    if (argc >= 2)
    {
        if (strcmp(argv[1], "reset") == 0)
        {
            prof_reset();
            p_out(out, user, "[prof] stats cleared");
            return;
        }
        if (strcmp(argv[1], "status") == 0)
        {
            prof_report(out, user, true);
            return;
        }
    }

#ifndef COS_PLATFORM_ESP32
    (void)argc;
    (void)argv;
    p_out(out, user, "[prof] full profiler is only available on real ESP32-S3 hardware");
    p_out(out, user, "  (simulator has no PSRAM/Octal/DMA/FreeRTOS run-time stats backend)");
    p_out(out, user, "  usage: prof [-t ms] [-n frames] | prof status | prof reset");
#else
    uint32_t dur_ms = PROF_DEFAULT_MS;
    uint32_t max_frames = 0;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
        {
            long v = strtol(argv[++i], NULL, 10);
            if (v > 0 && v <= 60000)
                dur_ms = (uint32_t)v;
        }
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
        {
            long v = strtol(argv[++i], NULL, 10);
            if (v > 0 && v <= 100000)
                max_frames = (uint32_t)v;
        }
        else if (strcmp(argv[i], "start") == 0)
        {
            /* 兼容 start 形式:等价于默认采样 */
        }
        else
        {
            p_out(out, user, "usage: prof [-t ms] [-n frames] | prof status | prof reset");
            return;
        }
    }

    if (s_active)
    {
        p_out(out, user, "[prof] already sampling, wait for it to finish...");
        return;
    }

    prof_start(dur_ms, max_frames);
    p_out(out, user, "[prof] sampling %u ms (max %u frames)...",
          (unsigned)dur_ms, (unsigned)(max_frames ? max_frames : 0));

    int64_t deadline = s_start_us + (int64_t)dur_ms * 1000;
    for (;;)
    {
        if (!s_active)
            break;
        if (esp_timer_get_time() >= deadline)
            break;
        if (max_frames != 0 && s_frames >= max_frames)
            break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    prof_finish();
    prof_report(out, user, false);
#endif
}
