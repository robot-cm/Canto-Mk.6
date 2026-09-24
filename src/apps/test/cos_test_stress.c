/**
 * @file cos_test_stress.c
 * @brief SPM stress test - memory leak check via unit test framework
 */

#include "cos_test_stress.h"
#if COS_ENABLE_TEST_APP

/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include "cos_log.h"
#include "cos_test_framework.h"
#include "cos_activity.h"
#include "cos_app.h"
#include "cos_service_storage.h"
#include "cos_mem.h"
#include "spm.h"
#include "cos_pkg_mgr.h"
#include "lvgl.h"

/* Macros and Definitions -------------------------------------*/
#define COS_LOG_TAG "StressTest"
#define SPM_STRESS_TEST_APP_ID "com.cantomk6os.test"
#define SPM_STRESS_MAX_CYCLES 30
#define SPM_STRESS_PHASE_CREATE 0
#define SPM_STRESS_PHASE_BACK 1
#define SPM_STRESS_PHASE_WAIT 2
#define SPM_STRESS_DELAY 100

/* Variables --------------------------------------------------*/

typedef struct
{
    lv_timer_t *timer;
    int cycle;
    int phase;
    script_pkg_t pkg;
    cos_activity_t *activity;
    unsigned long prev_alloc;
} stress_ctx_t;

static stress_ctx_t s_stress = {0};
static bool s_stress_passed = true;

/* Function Implementations -----------------------------------*/

static unsigned long _stress_get_alloc(void)
{
    if (!jerry_feature_enabled(JERRY_FEATURE_HEAP_STATS))
        return 0;
    jerry_heap_stats_t stats = {0};
    jerry_heap_stats(&stats);
    return stats.allocated_bytes;
}

static void _stress_report_header(void)
{
    COS_LOG_I("SPM STRESS TEST -- Memory Leak Check");
    COS_LOG_I("  App: " SPM_STRESS_TEST_APP_ID);
    COS_LOG_I("  Cycles: %d", SPM_STRESS_MAX_CYCLES);
}

static void _stress_timer_cb(lv_timer_t *t);

static void _stress_cleanup(void)
{
    if (s_stress.timer)
    {
        lv_timer_delete(s_stress.timer);
        s_stress.timer = NULL;
    }
    cos_pkg_free(&s_stress.pkg);
    memset(&s_stress.pkg, 0, sizeof(s_stress.pkg));
    s_stress.activity = NULL;
}

static bool _stress_load_pkg(void)
{
    char manifest_path[COS_FS_PATH_MAX];
    snprintf(manifest_path,
             sizeof(manifest_path),
             COS_APP_INSTALLED_DIR SPM_STRESS_TEST_APP_ID "/" COS_APP_MANIFEST_FILE_NAME);

    memset(&s_stress.pkg, 0, sizeof(s_stress.pkg));
    s_stress.pkg.type = SCRIPT_TYPE_APPLICATION;
    if (script_engine_get_manifest(manifest_path, &s_stress.pkg) != COS_OK)
    {
        COS_LOG_E("[STRESS] Failed to read manifest: %s", manifest_path);
        return false;
    }

    char script_path[COS_FS_PATH_MAX];
    snprintf(script_path,
             sizeof(script_path),
             COS_APP_INSTALLED_DIR SPM_STRESS_TEST_APP_ID "/" COS_APP_SCRIPT_ENTRY_FILE_NAME);

    char base_path[COS_FS_PATH_MAX];
    snprintf(base_path, sizeof(base_path), COS_APP_INSTALLED_DIR SPM_STRESS_TEST_APP_ID "/");
    s_stress.pkg.base_path = cos_strdup(base_path);
    s_stress.pkg.script_str = cos_storage_read_file(script_path);

    if (!s_stress.pkg.script_str)
    {
        COS_LOG_E("[STRESS] Failed to read script: %s", script_path);
        cos_pkg_free(&s_stress.pkg);
        memset(&s_stress.pkg, 0, sizeof(s_stress.pkg));
        return false;
    }

    return true;
}

static void _stress_app_on_enter(cos_activity_t *a)
{
    (void)a;
    unsigned long before = _stress_get_alloc();

    cos_result_t ret = spm_app_run(&s_stress.pkg);
    if (ret != COS_OK)
    {
        COS_LOG_E("[STRESS] Cycle %d: run failed ret=%d", s_stress.cycle, ret);
    }

    s_stress.prev_alloc = before;
    cos_pkg_free(&s_stress.pkg);
    memset(&s_stress.pkg, 0, sizeof(s_stress.pkg));
    s_stress.phase = SPM_STRESS_PHASE_BACK;
}

static void _stress_app_on_destroy(cos_activity_t *a)
{
    (void)a;
    spm_app_stop();
}

static const cos_activity_lifecycle_t _stress_lifecycle = {
    .on_enter = _stress_app_on_enter,
    .on_destroy = _stress_app_on_destroy,
};

static void _stress_timer_cb(lv_timer_t *t)
{
    (void)t;

    switch (s_stress.phase)
    {
        case SPM_STRESS_PHASE_CREATE:
        {
            if (!_stress_load_pkg())
            {
                _stress_cleanup();
                return;
            }

            s_stress.activity = cos_activity_create(&_stress_lifecycle);
            if (!s_stress.activity)
            {
                COS_LOG_E("[STRESS] Failed to create activity");
                _stress_cleanup();
                return;
            }

            lv_obj_t *view = cos_activity_get_view(s_stress.activity);
            lv_obj_set_size(view, COS_DISPLAY_WIDTH, COS_DISPLAY_HEIGHT);
            cos_activity_set_type(s_stress.activity, COS_ACTIVITY_TYPE_APP);
            cos_activity_set_title(s_stress.activity, SPM_STRESS_TEST_APP_ID);
            cos_activity_enter(s_stress.activity);
            break;
        }

        case SPM_STRESS_PHASE_BACK:
        {
            if (cos_activity_is_transition_in_progress())
                return;
            cos_activity_back();
            s_stress.phase = SPM_STRESS_PHASE_WAIT;
            break;
        }

        case SPM_STRESS_PHASE_WAIT:
        {
            if (cos_activity_is_transition_in_progress())
                return;
            if (cos_activity_get_current() == s_stress.activity)
                return;

            unsigned long after = _stress_get_alloc();
            long delta = (long)after - (long)s_stress.prev_alloc;

            COS_LOG_I("[STRESS] Cycle %2d: alloc=%lu delta=%ld %s",
                      s_stress.cycle,
                      after,
                      delta,
                      delta > 0 ? "(LEAK?)" : "");

            if (delta > 0)
            {
                s_stress_passed = false;
            }

            s_stress.cycle++;
            s_stress.activity = NULL;

            if (s_stress.cycle >= SPM_STRESS_MAX_CYCLES)
            {
                COS_LOG_I("[STRESS] Test complete. %d cycles.", SPM_STRESS_MAX_CYCLES);
                COS_LOG_I("[STRESS] Final alloc=%lu", after);
                cos_test_record("SPM Stress: memory leak check",
                                s_stress_passed,
                                s_stress_passed ? "No heap growth detected" : "Heap grew");
                _stress_cleanup();
                return;
            }

            s_stress.phase = SPM_STRESS_PHASE_CREATE;
            break;
        }
    }
}

static bool _test_spm_stress(void)
{
    _stress_report_header();
    memset(&s_stress, 0, sizeof(s_stress));
    s_stress_passed = true;
    s_stress.phase = SPM_STRESS_PHASE_CREATE;
    s_stress.timer = lv_timer_create(_stress_timer_cb, SPM_STRESS_DELAY, NULL);
    if (!s_stress.timer)
    {
        COS_LOG_E("[STRESS] Failed to create timer");
        cos_test_record("SPM Stress: memory leak check", false, "Timer creation failed");
        return false;
    }
    return true;
}

void cos_test_stress_register_tests(void)
{
    cos_test_register("SPM Stress: memory leak check", _test_spm_stress);
}

#endif /* COS_ENABLE_TEST_APP */
