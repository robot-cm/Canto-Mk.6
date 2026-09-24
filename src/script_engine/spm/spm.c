/**
 * @file spm.c
 * @brief Script Program Manager implementation
 */

#include "spm.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#define COS_LOG_TAG "SPM"
#include "cos_log.h"
#include "cos_mem.h"
#include "cos_event.h"
#include "sni_context.h"
#include "sni_callback_runtime.h"
#include "jerryscript.h"
#include "lvgl.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

static script_program_t *s_program_list = NULL;
static bool s_initialized = false;
static spm_error_t s_last_error = {0};
static bool s_has_last_error = false;

/* Function Implementations -----------------------------------*/
static void _lvgl_view_clean(void *view)
{
    if (view)
        lv_obj_clean((lv_obj_t *)view);
}

static void _program_list_add(script_program_t *prog);
static void _program_list_remove(script_program_t *prog);
static void _program_destroy(script_program_t *prog);
static cos_result_t _error_copy_from_core(script_program_t *prog);

static void _program_list_add(script_program_t *prog)
{
    if (!prog)
        return;
    prog->next = s_program_list;
    prog->prev = NULL;
    if (s_program_list)
        s_program_list->prev = prog;
    s_program_list = prog;
}

static void _program_list_remove(script_program_t *prog)
{
    if (!prog)
        return;
    if (prog->prev)
        prog->prev->next = prog->next;
    else
        s_program_list = prog->next;
    if (prog->next)
        prog->next->prev = prog->prev;
    prog->prev = NULL;
    prog->next = NULL;
}

static void _script_free(script_pkg_t *p)
{
    if (p->id)
    {
        cos_free((void *)p->id);
        p->id = NULL;
    }
    if (p->name)
    {
        cos_free((void *)p->name);
        p->name = NULL;
    }
    if (p->version)
    {
        cos_free((void *)p->version);
        p->version = NULL;
    }
    if (p->author)
    {
        cos_free((void *)p->author);
        p->author = NULL;
    }
    if (p->description)
    {
        cos_free((void *)p->description);
        p->description = NULL;
    }
    if (p->script_str)
    {
        cos_free((void *)p->script_str);
        p->script_str = NULL;
    }
    if (p->base_path)
    {
        cos_free((void *)p->base_path);
        p->base_path = NULL;
    }
    if (p->permissions)
    {
        for (uint8_t i = 0; i < p->permission_count; i++)
        {
            if (p->permissions[i])
                cos_free((void *)p->permissions[i]);
        }
        cos_free(p->permissions);
        p->permissions = NULL;
        p->permission_count = 0;
    }
}

static void _program_destroy(script_program_t *prog)
{
    if (!prog)
        return;
    COS_LOG_D("Destroying program %p type=%d state=%d sni_ctx=%p",
              (void *)prog,
              prog->type,
              prog->state,
              (void *)prog->sni_ctx);

    if (prog->sni_ctx)
    {
        sni_cb_context_cleanup_events(prog->sni_ctx);
        sni_context_sweep_all(prog->sni_ctx);
        sni_context_destroy(prog->sni_ctx);
        prog->sni_ctx = NULL;
    }
    if (jerry_value_is_object(prog->realm))
    {
        jerry_value_free(prog->realm);
        prog->realm = jerry_undefined();
    }
    _script_free(&prog->script);
    cos_free(prog);
}

static cos_result_t _error_copy_from_core(script_program_t *prog)
{
    if (!prog)
        return COS_ERR_SCRIPT_NULL_PACKAGE;
    prog->has_error = true;
    const char *err_info = script_engine_get_error_info();
    if (err_info)
        snprintf(prog->error.error_info, SPM_ERROR_INFO_MAX, "%s", err_info);
    else
        prog->error.error_info[0] = '\0';
    const script_error_location_t *loc = script_engine_get_error_location();
    if (loc)
        memcpy(&prog->error.error_location, loc, sizeof(script_error_location_t));
    const script_error_location_t *bt = script_engine_get_error_backtrace(&prog->error.backtrace_count);
    if (bt && prog->error.backtrace_count > 0)
    {
        uint32_t frames = prog->error.backtrace_count;
        if (frames > SPM_BACKTRACE_MAX_FRAMES)
            frames = SPM_BACKTRACE_MAX_FRAMES;
        memcpy(prog->error.backtrace, bt, frames * sizeof(script_error_location_t));
        prog->error.backtrace_count = frames;
    }
    return COS_OK;
}

static void _pkg_clone(script_pkg_t *dst, const script_pkg_t *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->type = src->type;
    dst->id = src->id ? cos_strdup(src->id) : NULL;
    dst->name = src->name ? cos_strdup(src->name) : NULL;
    dst->version = src->version ? cos_strdup(src->version) : NULL;
    dst->author = src->author ? cos_strdup(src->author) : NULL;
    dst->description = src->description ? cos_strdup(src->description) : NULL;
    dst->script_str = src->script_str ? cos_strdup(src->script_str) : NULL;
    dst->base_path = src->base_path ? cos_strdup(src->base_path) : NULL;

    /* Clone permissions array */
    if (src->permissions && src->permission_count > 0)
    {
        dst->permissions = (const char **)cos_malloc(sizeof(const char *) * (src->permission_count + 1));
        if (dst->permissions)
        {
            for (uint8_t i = 0; i < src->permission_count; i++)
            {
                dst->permissions[i] = src->permissions[i] ? cos_strdup(src->permissions[i]) : NULL;
            }
            dst->permissions[src->permission_count] = NULL;
            dst->permission_count = src->permission_count;
        }
    }

    dst->min_api_level = src->min_api_level;
    dst->target_api_level = src->target_api_level;
}

/* SPM Lifecycle -----------------------------------------------*/

cos_result_t spm_init(void)
{
    if (s_initialized)
        return COS_OK;
    s_program_list = NULL;
    s_initialized = true;
    COS_LOG_I("SPM initialized");
    return COS_OK;
}

/* Program Lifecycle -------------------------------------------*/

script_program_t *spm_start_program(const script_pkg_t *pkg)
{
    if (!pkg || !pkg->script_str)
    {
        COS_LOG_E("spm_start_program: null package");
        return NULL;
    }
    if (!s_initialized)
    {
        COS_LOG_E("spm_start_program: SPM not initialized");
        return NULL;
    }

    script_program_t *prog = cos_malloc_zeroed(sizeof(script_program_t));
    if (!prog)
    {
        COS_LOG_E("spm_start_program: malloc failed");
        return NULL;
    }

    prog->type = pkg->type;
    prog->state = SCRIPT_PROGRAM_STATE_ACTIVE;
    prog->realm = jerry_undefined();
    _pkg_clone(&prog->script, pkg);

    prog->sni_ctx = sni_context_create();
    if (!prog->sni_ctx)
    {
        COS_LOG_E("spm_start_program: sni_context_create failed");
        _script_free(&prog->script);
        cos_free(prog);
        return NULL;
    }
    prog->sni_ctx->owner = prog;

    _program_list_add(prog);
    COS_LOG_I("Starting program %p type=%d id=%s", (void *)prog, prog->type, pkg->id ? pkg->id : "unknown");

    script_engine_set_current_program(prog);
    cos_result_t ret = script_engine_run(pkg);

    if (ret != COS_OK)
    {
        /* If fatal engine recovery happened, spm_handle_engine_reset()
         * already destroyed ALL programs (including this one). The prog
         * pointer is now freed — skip all cleanup. */
        if (ret == COS_ERR_SCRIPT_EXCEPTION && s_program_list == NULL)
        {
            const char *err = script_engine_get_error_info();
            if (err && err[0])
            {
                s_has_last_error = true;
                memset(&s_last_error, 0, sizeof(spm_error_t));
                snprintf(s_last_error.error_info, SPM_ERROR_INFO_MAX, "%s", err);
                s_last_error.error_type = COS_SCRIPT_FAULT_ENGINE_CRASH;
            }
            script_engine_set_current_program(NULL);
            return NULL;
        }

        /* Capture error info BEFORE script_engine_stop clears it */
        _error_copy_from_core(prog);
        /* Save a persistent copy before destroying the program so the
         * fault panel can read the backtrace after the program is gone. */
        memcpy(&s_last_error, &prog->error, sizeof(spm_error_t));
        s_has_last_error = true;

        script_engine_stop();
        COS_LOG_E("spm_start_program: execution failed ret=%d", ret);
        _program_list_remove(prog);
        _program_destroy(prog);
        script_engine_set_current_program(NULL);
        return NULL;
    }

    COS_LOG_I("Program %p started successfully realm=%u sni_ctx=%p",
              (void *)prog,
              (unsigned)prog->realm,
              (void *)prog->sni_ctx);
    /* Clear persistent error copy — a new program started, old errors are stale */
    s_has_last_error = false;
    memset(&s_last_error, 0, sizeof(spm_error_t));
    return prog;
}

cos_result_t spm_suspend_program(script_program_t *prog)
{
    if (!prog)
        return COS_ERR_SCRIPT_NULL_PACKAGE;
    if (prog->state != SCRIPT_PROGRAM_STATE_ACTIVE)
        return COS_ERR_INVALID_STATE;

    script_engine_state_t core_state = script_engine_get_state();
    if (core_state != SCRIPT_ENGINE_STATE_IDLE)
    {
        COS_LOG_E("spm_suspend_program: Core state=%d not IDLE", core_state);
        return COS_ERR_INVALID_STATE;
    }

    if (prog->sni_ctx)
        sni_context_set_paused(prog->sni_ctx, true);
    prog->state = SCRIPT_PROGRAM_STATE_SUSPENDED;
    COS_LOG_I("Program %p suspended", (void *)prog);
    return COS_OK;
}

cos_result_t spm_resume_program(script_program_t *prog)
{
    if (!prog)
        return COS_ERR_SCRIPT_NULL_PACKAGE;
    if (prog->state != SCRIPT_PROGRAM_STATE_SUSPENDED)
        return COS_ERR_INVALID_STATE;

    script_engine_state_t core_state = script_engine_get_state();
    if (core_state != SCRIPT_ENGINE_STATE_IDLE)
    {
        COS_LOG_E("spm_resume_program: Core state=%d not IDLE", core_state);
        return COS_ERR_INVALID_STATE;
    }

    if (prog->sni_ctx)
        sni_context_set_paused(prog->sni_ctx, false);
    prog->state = SCRIPT_PROGRAM_STATE_ACTIVE;
    script_engine_set_current_program(prog);
    COS_LOG_I("Program %p resumed", (void *)prog);
    return COS_OK;
}

cos_result_t spm_terminate_program(script_program_t *prog)
{
    if (!prog)
        return COS_ERR_SCRIPT_NULL_PACKAGE;
    if (prog->state == SCRIPT_PROGRAM_STATE_TERMINATED)
        return COS_OK;

    COS_LOG_I("Terminating program %p type=%d state=%d", (void *)prog, prog->type, prog->state);
    prog->state = SCRIPT_PROGRAM_STATE_STOPPING;
    // Pause the program context to ensure no callbacks are triggered
    if (prog->sni_ctx)
        sni_context_set_paused(prog->sni_ctx, true);

    // Clean up LVGL widgets created by this program's JS session
    if (prog->cleanup_view)
    {
        prog->cleanup_view(prog->cleanup_user_data);
        prog->cleanup_view = NULL;
    }

    // Clear JS object native_ptrs BEFORE stopping the JS engine.
    // This prevents JerryScript GC (triggered by _collect_script_garbage
    // inside script_engine_stop) from calling native free callbacks
    // that would free sni_managed_resource_node_t nodes while they are
    // still linked in the context's resource lists.
    if (prog->sni_ctx)
        sni_context_clear_native_ptrs_all(prog->sni_ctx);

    // Release all JS callback references BEFORE stopping the engine.
    // Timer/animation callbacks hold JS function references that keep
    // bytecode alive. Freeing them here (with multiple GC passes) ensures
    // bytecode refcounts reach zero before script_engine_stop releases modules.
    if (prog->sni_ctx)
        sni_context_sweep_js_refs(prog->sni_ctx);

    // Let Core finish its stop/cleanup while the program object is still valid.
    script_engine_stop();
    // Remove the program from the list first so it can no longer be discovered
    _program_list_remove(prog);

    // Drop the Core's current-program pointer before freeing the program itself.
    script_engine_set_current_program(NULL);

    // Destroy the program resources after Core cleanup has finished.
    prog->state = SCRIPT_PROGRAM_STATE_TERMINATED;
    bool had_error = prog->has_error;
    _program_destroy(prog);

    /* If this program was healthy, any persistent error copy is stale */
    if (!had_error)
    {
        s_has_last_error = false;
        memset(&s_last_error, 0, sizeof(spm_error_t));
    }

    COS_LOG_I("Program terminated");
    return COS_OK;
}

void spm_terminate_programs_by_type(script_pkg_type_t type)
{
    script_program_t *prog = s_program_list;
    while (prog)
    {
        script_program_t *next = prog->next;
        if (type == SCRIPT_TYPE_UNKNOWN || prog->type == type)
            spm_terminate_program(prog);
        prog = next;
    }
}

/* JS Callback Gate --------------------------------------------*/

jerry_value_t spm_call(script_program_t *prog,
                       jerry_value_t func,
                       jerry_value_t this_val,
                       const jerry_value_t args_p[],
                       jerry_length_t args_count)
{
    if (!prog || prog->state != SCRIPT_PROGRAM_STATE_ACTIVE)
    {
        COS_LOG_W("spm_call: program not ACTIVE (state=%d), rejecting", prog ? prog->state : -1);
        return jerry_undefined();
    }
    return script_engine_call_raw(func, this_val, args_p, args_count);
}

/* Query APIs --------------------------------------------------*/

script_program_t *spm_get_active_program(void)
{
    script_program_t *prog = s_program_list;
    while (prog)
    {
        if (prog->state == SCRIPT_PROGRAM_STATE_ACTIVE)
            return prog;
        prog = prog->next;
    }
    return NULL;
}

script_program_t *spm_get_program_by_type(script_pkg_type_t type)
{
    script_program_t *prog = s_program_list;
    while (prog)
    {
        if (prog->type == type && prog->state != SCRIPT_PROGRAM_STATE_TERMINATED)
            return prog;
        prog = prog->next;
    }
    return NULL;
}

script_program_t *spm_get_program_by_id(const char *id)
{
    if (!id)
        return NULL;
    script_program_t *prog = s_program_list;
    while (prog)
    {
        if (prog->script.id && strcmp(prog->script.id, id) == 0 && prog->state == SCRIPT_PROGRAM_STATE_ACTIVE)
        {
            return prog;
        }
        prog = prog->next;
    }
    return NULL;
}

const char *spm_get_program_error_info(script_program_t *prog)
{
    if (!prog || !prog->has_error)
        return "";
    return prog->error.error_info;
}

cos_script_error_type_t spm_get_program_error_type(script_program_t *prog)
{
    if (!prog || !prog->has_error)
        return COS_SCRIPT_FAULT_ERROR_UNKNOWN;
    return prog->error.error_type;
}

const script_error_location_t *spm_get_program_error_location(script_program_t *prog)
{
    if (!prog || !prog->has_error)
        return NULL;
    return &prog->error.error_location;
}

/* Simplified WatchFace APIs ----------------------------------*/

static script_program_t *s_wf_program = NULL;

cos_result_t spm_watchface_start(const script_pkg_t *pkg, void *view)
{
    if (!pkg || !pkg->script_str)
        return COS_ERR_SCRIPT_NULL_PACKAGE;
    if (s_wf_program)
    {
        spm_terminate_program(s_wf_program);
        s_wf_program = NULL;
    }
    script_engine_stop();
    s_wf_program = spm_start_program(pkg);
    if (s_wf_program && view)
    {
        s_wf_program->cleanup_view = _lvgl_view_clean;
        s_wf_program->cleanup_user_data = view;
    }
    return s_wf_program ? COS_OK : COS_FAILED;
}

cos_result_t spm_watchface_pause(void)
{
    if (!s_wf_program)
        return COS_ERR_INVALID_STATE;
    script_engine_state_t state = script_engine_get_state();
    if (state == SCRIPT_ENGINE_STATE_RUNNING)
        script_engine_request_stop();
    cos_result_t ret = spm_suspend_program(s_wf_program);
    return ret;
}

cos_result_t spm_watchface_resume(void)
{
    if (!s_wf_program)
        return COS_ERR_INVALID_STATE;
    cos_result_t ret = spm_resume_program(s_wf_program);
    return ret;
}

cos_result_t spm_watchface_destroy(void)
{
    if (s_wf_program)
    {
        spm_terminate_program(s_wf_program);
        s_wf_program = NULL;
    }
    script_engine_stop();
    return COS_OK;
}

void spm_watchface_set_view_cleanup(void *view)
{
    if (s_wf_program && view)
    {
        s_wf_program->cleanup_view = _lvgl_view_clean;
        s_wf_program->cleanup_user_data = view;
    }
}

bool spm_watchface_has_context(void)
{
    return s_wf_program != NULL && s_wf_program->state != SCRIPT_PROGRAM_STATE_TERMINATED;
}

cos_result_t spm_app_run(const script_pkg_t *pkg)
{
    if (!pkg || !pkg->script_str)
        return COS_ERR_SCRIPT_NULL_PACKAGE;
    return spm_start_program(pkg) ? COS_OK : COS_FAILED;
}

cos_result_t spm_app_stop(void)
{
    script_program_t *prog = spm_get_program_by_type(SCRIPT_TYPE_APPLICATION);
    if (!prog)
        return COS_OK;
    cos_result_t ret = spm_terminate_program(prog);
    return ret;
}

cos_result_t spm_app_suspend(void)
{
    script_program_t *prog = spm_get_program_by_type(SCRIPT_TYPE_APPLICATION);
    if (!prog)
        return COS_OK; /* nothing running, nothing to suspend */
    return spm_suspend_program(prog);
}

cos_result_t spm_app_resume(void)
{
    script_program_t *prog = spm_get_program_by_type(SCRIPT_TYPE_APPLICATION);
    if (!prog)
        return COS_ERR_INVALID_STATE;
    if (prog->state != SCRIPT_PROGRAM_STATE_SUSPENDED)
        return COS_ERR_INVALID_STATE;
    return spm_resume_program(prog);
}

const spm_error_t *spm_get_last_error(void)
{
    script_program_t *prog = s_program_list;
    while (prog)
    {
        if (prog->has_error)
        {
            return &prog->error;
        }
        prog = prog->next;
    }
    /* Fallback: return the persistent copy saved before program destruction. */
    if (s_has_last_error)
    {
        return &s_last_error;
    }
    return NULL;
}

void spm_handle_engine_reset(void)
{
    COS_LOG_W("SPM: emergency reset — destroying all programs");

    script_program_t *prog = s_program_list;
    while (prog)
    {
        script_program_t *next = prog->next;

        if (prog->cleanup_view)
        {
            prog->cleanup_view(prog->cleanup_user_data);
            prog->cleanup_view = NULL;
        }

        if (prog->sni_ctx)
        {
            sni_context_clear_native_ptrs_all(prog->sni_ctx);
            sni_context_sweep_js_refs(prog->sni_ctx);
            sni_cb_context_cleanup_events(prog->sni_ctx);
            sni_context_sweep_all(prog->sni_ctx);
            sni_context_destroy(prog->sni_ctx);
            prog->sni_ctx = NULL;
        }

        if (jerry_value_is_object(prog->realm))
        {
            jerry_value_free(prog->realm);
            prog->realm = jerry_undefined();
        }

        _script_free(&prog->script);
        cos_free(prog);
        prog = next;
    }

    s_program_list = NULL;
    s_wf_program = NULL;
    s_has_last_error = false;
    memset(&s_last_error, 0, sizeof(spm_error_t));

    COS_LOG_I("SPM: all programs destroyed, engine reset ready");
}
