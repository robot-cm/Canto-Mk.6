/**
 * @file cos_profmonitor.c
 * @brief ProfMonitor - native C app that monitors resource usage on demand.
 *
 * Round 240x240 UI: a big percentage number on top, a single blue usage
 * curve in the middle, and a right-arrow button on the lower-right that
 * cycles the monitored resource (CPU0 / CPU1 / SRAM / PSRAM).
 *
 * Lazy loading: sampling runs only while the app is in the foreground,
 * driven by an LVGL timer (no extra FreeRTOS task). On pause / destroy the
 * timer is deleted, so no background sampling ever occurs. The last viewed
 * resource type is persisted to sdcard/prof_monitor/latest.txt and restored
 * on next open.
 *
 * CPU load: this target runs SMP FreeRTOS, where esp_cpu_get_run_time_stats()
 * and ulTaskGetIdleRunTimeCounterForCore() are not available. Instead we take
 * snapshots with uxTaskGetSystemState() and derive per-core load from the idle
 * task's run-time counter (named "IDLE0"/"IDLE1"), so load = 100 * (1 - idle/total).
 */

#if defined(CONFIG_PROFMONITOR_APP_ENABLE) && CONFIG_PROFMONITOR_APP_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cos_core.h"
#include "cos_activity.h"
#include "cos_mem.h"
#include "cos_log.h"
#include "cos_font.h"
#include "ui/system/cos_round_clip.h"
#include "services/storage/cos_service_storage.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"

/* ---- config ---- */
#define PM_DIR          "sdcard/prof_monitor"
#define PM_FILE         "sdcard/prof_monitor/latest.txt"
#define PM_SAMPLES      60
#define PM_PERIOD_MS    200
#define PM_MAX_TASKS    64

/* curve plot rectangle (kept safely inside the 240x240 circle) */
#define PM_PLOT_X0      40
#define PM_PLOT_X1      200
#define PM_PLOT_Y0      108
#define PM_PLOT_Y1      192

typedef enum {
    PM_RES_CPU0 = 0,
    PM_RES_CPU1,
    PM_RES_SRAM,
    PM_RES_PSRAM,
    PM_RES_COUNT
} pm_res_t;

/* ---- state ---- */
static cos_activity_t *s_act = NULL;
static lv_timer_t     *s_timer = NULL;
static lv_obj_t       *s_pct = NULL;
static lv_obj_t       *s_name = NULL;     /* resource name under the percentage */
static lv_obj_t       *s_line = NULL;
static lv_point_precise_t s_pts[PM_SAMPLES];
static float           s_buf[PM_SAMPLES];
static int             s_head = 0;       /* next write index */
static int             s_filled = 0;     /* valid sample count */
static pm_res_t        s_res = PM_RES_CPU0;

/* per-core idle run-time counter snapshots (delta -> CPU load) */
static configRUN_TIME_COUNTER_TYPE s_prev_total = 0;
static configRUN_TIME_COUNTER_TYPE s_prev_idle[2] = {0, 0};
static bool             s_have_prev = false;
static TaskStatus_t     s_ts[PM_MAX_TASKS];

/* ---- persistence ---- */
static void _pm_persist(void)
{
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d\n", (int)s_res);
    cos_storage_mkdir_recursive(PM_DIR);
    cos_storage_write_file_immediate(PM_FILE, buf, (size_t)n);
}

static pm_res_t _pm_load_res(void)
{
    pm_res_t r = PM_RES_CPU0;
    if (cos_storage_is_file(PM_FILE)) {
        char *data = cos_storage_read_file(PM_FILE);
        if (data) {
            int v = atoi(data);
            if (v >= 0 && v < (int)PM_RES_COUNT) r = (pm_res_t)v;
            cos_free(data);
        }
    }
    return r;
}

static void _pm_reset_delta(void) { s_have_prev = false; }

/* ---- sampling (returns integer 0..100) ---- */
static int _pm_last(void)
{
    if (!s_filled) return 0;
    return (int)s_buf[(s_head + PM_SAMPLES - 1) % PM_SAMPLES];
}

static int _pm_sample(void)
{
    switch (s_res) {
    case PM_RES_CPU0:
    case PM_RES_CPU1: {
        int core = (s_res == PM_RES_CPU1) ? 1 : 0;
        if (core >= portNUM_PROCESSORS) return _pm_last();

        UBaseType_t got = uxTaskGetSystemState(s_ts, PM_MAX_TASKS, NULL);
        configRUN_TIME_COUNTER_TYPE total_now = 0;
        configRUN_TIME_COUNTER_TYPE idle_now[2] = {0, 0};
        for (UBaseType_t i = 0; i < got; i++) {
            total_now += s_ts[i].ulRunTimeCounter;
            const char *nm = s_ts[i].pcTaskName;
            if (nm && strncmp(nm, "IDLE", 4) == 0) {
                int c = 0;
                if (nm[4] >= '0' && nm[4] <= '9') c = nm[4] - '0';
                if (c >= 0 && c < 2) idle_now[c] = s_ts[i].ulRunTimeCounter;
            }
        }

        if (!s_have_prev) {
            s_prev_total = total_now;
            s_prev_idle[0] = idle_now[0];
            s_prev_idle[1] = idle_now[1];
            s_have_prev = true;
            return _pm_last();
        }

        configRUN_TIME_COUNTER_TYPE dt = total_now - s_prev_total;
        configRUN_TIME_COUNTER_TYPE didle = idle_now[core] - s_prev_idle[core];
        s_prev_total = total_now;
        s_prev_idle[0] = idle_now[0];
        s_prev_idle[1] = idle_now[1];

        if (dt == 0) return _pm_last();
        int load = (int)(100ULL * (dt - didle) / dt);
        if (load < 0)   load = 0;
        if (load > 100) load = 100;
        return load;
    }
    case PM_RES_SRAM: {
        uint32_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        if (total == 0) return 0;
        uint32_t free_s = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int used = (int)(100ULL * (total - free_s) / total);
        return used > 100 ? 100 : used;
    }
    case PM_RES_PSRAM: {
        uint32_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        if (total == 0) return 0;
        uint32_t free_s = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        int used = (int)(100ULL * (total - free_s) / total);
        return used > 100 ? 100 : used;
    }
    default:
        return 0;
    }
}

/* ---- resource name for the current selection ---- */
static const char *_pm_res_name(void)
{
    switch (s_res) {
    case PM_RES_CPU0:  return "CPU0";
    case PM_RES_CPU1:  return "CPU1";
    case PM_RES_SRAM:  return "SRAM";
    case PM_RES_PSRAM: return "PSRAM";
    default:           return "";
    }
}

/* ---- timer tick: sample + redraw curve + number ---- */
static void _pm_tick(lv_timer_t *t)
{
    (void)t;
    int v = _pm_sample();

    s_buf[s_head] = (float)v;
    s_head = (s_head + 1) % PM_SAMPLES;
    if (s_filled < PM_SAMPLES) s_filled++;

    int count = s_filled;
    int start = (s_head - count + PM_SAMPLES) % PM_SAMPLES;
    int w = PM_PLOT_X1 - PM_PLOT_X0;
    int h = PM_PLOT_Y1 - PM_PLOT_Y0;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % PM_SAMPLES;
        int x = PM_PLOT_X0 + (count <= 1 ? 0 : (w * i) / (count - 1));
        float val = s_buf[idx];
        if (val < 0)   val = 0;
        if (val > 100) val = 100;
        int y = PM_PLOT_Y1 - (int)((val / 100.0f) * h);
        s_pts[i].x = x;
        s_pts[i].y = y;
    }
    lv_line_set_points(s_line, s_pts, (uint32_t)count);

    char txt[8];
    snprintf(txt, sizeof(txt), "%d%%", v);
    lv_label_set_text(s_pct, txt);
}

/* ---- right-arrow button: cycle resource type ---- */
static void _pm_switch_cb(lv_event_t *e)
{
    (void)e;
    s_res = (pm_res_t)((s_res + 1) % PM_RES_COUNT);
    s_head = 0;
    s_filled = 0;
    _pm_reset_delta();
    _pm_persist();
    if (s_name) lv_label_set_text(s_name, _pm_res_name());
    _pm_tick(NULL);   /* immediate visual feedback */
}

/* ---- UI (circular 240x240) ---- */
static void _build_ui(cos_activity_t *act)
{
    lv_obj_t *root = cos_activity_get_view(act);
    cos_round_clip(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* percentage number (top) */
    s_pct = lv_label_create(root);
    lv_label_set_text(s_pct, "0%");
    cos_label_set_font_size(s_pct, COS_FONT_SIZE_LARGE);
    lv_obj_set_style_text_color(s_pct, lv_color_white(), 0);
    lv_obj_align(s_pct, LV_ALIGN_TOP_MID, 0, 44);

    /* resource name (grey, just under the percentage) */
    s_name = lv_label_create(root);
    lv_label_set_text(s_name, _pm_res_name());
    cos_label_set_font_size(s_name, COS_FONT_SIZE_SMALL);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0x888888), 0);
    lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 84);

    /* single blue usage curve */
    s_line = lv_line_create(root);
    lv_obj_set_style_line_width(s_line, 2, 0);
    lv_obj_set_style_line_color(s_line, lv_color_hex(0x3B9EFF), 0);
    lv_obj_set_style_line_rounded(s_line, true, 0);

    /* right-arrow button (lower-right, inside the circle) */
    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_size(btn, 34, 34);
    lv_obj_align(btn, LV_ALIGN_CENTER, 68, 58);
    lv_obj_set_style_radius(btn, 17, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222831), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, _pm_switch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = lv_label_create(btn);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, lv_color_white(), 0);
    cos_label_set_font_size(arrow, COS_FONT_SIZE_MEDIUM);
    lv_obj_center(arrow);
}

/* ---- lifecycle ---- */
static void _on_enter(cos_activity_t *act)
{
    s_act = act;
    s_res = _pm_load_res();
    s_head = 0;
    s_filled = 0;
    _build_ui(act);
    _pm_reset_delta();
    s_timer = lv_timer_create(_pm_tick, PM_PERIOD_MS, NULL);
}

static void _on_pause(cos_activity_t *act)
{
    (void)act;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
}

static void _on_resume(cos_activity_t *act)
{
    (void)act;
    if (!s_timer) {
        _pm_reset_delta();
        s_timer = lv_timer_create(_pm_tick, PM_PERIOD_MS, NULL);
    }
}

static void _on_destroy(cos_activity_t *act)
{
    (void)act;
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    _pm_persist();          /* persist last viewed type on exit */
    s_pct = NULL;
    s_name = NULL;
    s_line = NULL;
}

static const cos_activity_lifecycle_t s_lifecycle = {
    .on_enter   = _on_enter,
    .on_pause   = _on_pause,
    .on_resume  = _on_resume,
    .on_destroy = _on_destroy,
};

void cos_profmonitor_enter(void)
{
    COS_LOG_I("ProfMonitor: enter");
    cos_activity_t *act = cos_activity_create(&s_lifecycle);
    cos_activity_set_type(act, COS_ACTIVITY_TYPE_APP);
    cos_activity_set_app_header_visible(act, false);
    cos_activity_set_title(act, "ProfMonitor");
    cos_activity_enter(act);
}

#endif /* CONFIG_PROFMONITOR_APP_ENABLE */
