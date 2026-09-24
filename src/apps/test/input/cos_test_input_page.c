/**
 * @file cos_test_input_page.c
 * @brief Input page test module
 */

#include "cos_config.h"
#if COS_ENABLE_TEST_APP

#include "cos_test_input_page.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cos_mem.h"
#include "cos_activity.h"
#include "cos_app_header.h"
#include "cos_crown.h"
#include "cos_input_page.h"
#include "cos_log.h"
#include "cos_basic_widgets.h"
#include "cos_lang.h"
#include "lvgl.h"
#include "cos_error.h"

/* Macros and Definitions -------------------------------------*/
#define COS_LOG_TAG "InputPageTest"

/* Variables --------------------------------------------------*/
typedef struct
{
    lv_obj_t *container;
    lv_obj_t *list;
    lv_obj_t *result_label;
    lv_obj_t *input_label;
    lv_obj_t *summary_label;
    struct
    {
        uint32_t total_tests;
        uint32_t passed_tests;
        uint32_t failed_tests;
    } stats;
} _test_context_t;

static _test_context_t _ctx = {0};
static char *_last_input_text = NULL;
static cos_input_result_t _last_input_result = COS_INPUT_RESULT_CANCEL;

/* Function Implementations -----------------------------------*/
static void _update_result(const char *text)
{
    if (_ctx.result_label)
    {
        lv_label_set_text(_ctx.result_label, text);
    }
    COS_LOG_I("%s", text);
}

static void _record_test(const char *name, bool passed, const char *details)
{
    _ctx.stats.total_tests++;
    if (passed)
    {
        _ctx.stats.passed_tests++;
    }
    else
    {
        _ctx.stats.failed_tests++;
    }

    char label_text[256];
    snprintf(label_text, sizeof(label_text), "%s: %s", name, passed ? "PASS" : "FAIL");

    lv_obj_t *btn = lv_list_add_button(_ctx.list, NULL, label_text);

    if (!passed)
    {
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFF0000), 0);
    }

    if (_ctx.summary_label && lv_obj_is_valid(_ctx.summary_label))
    {
        char summary_text[128];
        snprintf(summary_text,
                 sizeof(summary_text),
                 "Total: %u | Passed: %u | Failed: %u",
                 _ctx.stats.total_tests,
                 _ctx.stats.passed_tests,
                 _ctx.stats.failed_tests);
        lv_label_set_text(_ctx.summary_label, summary_text);
    }
}

/**
 * @brief Callback for input page when using callback mode
 */
static void _input_page_callback(const char *text, cos_input_result_t result, void *user_data)
{
    (void)user_data;

    if (_last_input_text)
    {
        cos_free(_last_input_text);
        _last_input_text = NULL;
    }

    if (text)
    {
        size_t text_len = strlen(text);
        _last_input_text = cos_malloc(text_len + 1);
        if (_last_input_text)
        {
            memcpy(_last_input_text, text, text_len + 1);
        }
    }

    if (!_last_input_text)
    {
        _last_input_text = cos_malloc(1);
        if (_last_input_text)
        {
            _last_input_text[0] = '\0';
        }
    }

    _last_input_result = result;

    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Input callback: text='%s', result=%d", _last_input_text, result);
    COS_LOG_I("%s", log_msg);
}

/**
 * @brief Test 1: Open input page with direct label write
 */
static void _test_input_page_with_label(lv_event_t *e)
{
    (void)e;

    COS_LOG_I("Test 1: Input page with label write");

    if (_ctx.input_label == NULL)
    {
        _update_result("ERROR: input_label is NULL");
        _record_test("Input Page with Label", false, "Label not initialized");
        return;
    }

    cos_result_t result = cos_input_page_open(_ctx.input_label);
    if (result == COS_OK)
    {
        _update_result("Input page opened with label");
        _record_test("Input Page with Label", true, "Page opened successfully");
    }
    else
    {
        _update_result("ERROR: Failed to open input page");
        _record_test("Input Page with Label", false, "Failed to open page");
    }
}

/**
 * @brief Test 2: Open input page with callback
 */
static void _test_input_page_with_callback(lv_event_t *e)
{
    (void)e;

    COS_LOG_I("Test 2: Input page with callback");

    cos_result_t result = cos_input_page_open_with_callback(NULL, _input_page_callback, NULL);

    if (result == COS_OK)
    {
        _update_result("Input page opened with callback");
        _record_test("Input Page with Callback", true, "Page opened successfully");
    }
    else
    {
        _update_result("ERROR: Failed to open input page");
        _record_test("Input Page with Callback", false, "Failed to open page");
    }
}

/**
 * @brief Test 3: Verify label update after input
 */
static void _test_verify_label_update(lv_event_t *e)
{
    (void)e;

    COS_LOG_I("Test 3: Verify label update after input");

    if (_ctx.input_label == NULL)
    {
        _update_result("ERROR: input_label is NULL");
        _record_test("Label Update Verification", false, "Label not initialized");
        return;
    }

    const char *label_text = lv_label_get_text(_ctx.input_label);
    bool has_text = label_text && strlen(label_text) > 0;

    char log_msg[256];
    snprintf(log_msg,
             sizeof(log_msg),
             "Label text verification: text='%s', has_text=%d",
             label_text ? label_text : "(null)",
             has_text);

    _update_result(log_msg);
    _record_test("Label Update Verification", has_text, has_text ? "Label has text" : "Label is empty");
}

/**
 * @brief Test 4: Verify callback data
 */
static void _test_verify_callback_data(lv_event_t *e)
{
    (void)e;

    COS_LOG_I("Test 4: Verify callback data");

    bool has_callback_data = _last_input_text && strlen(_last_input_text) > 0;

    char log_msg[256];
    snprintf(log_msg,
             sizeof(log_msg),
             "Callback data: text='%s', result=%d",
             _last_input_text ? _last_input_text : "(null)",
             _last_input_result);

    _update_result(log_msg);
    _record_test("Callback Data",
                 has_callback_data,
                 has_callback_data ? "Callback received data" : "Callback data empty");
}

/**
 * @brief Create test screen
 */
static lv_obj_t *_create_input_test_scr(void)
{
    cos_activity_t *activity = cos_activity_create(NULL);
    if (!activity)
    {
        return NULL;
    }

    lv_obj_t *view = cos_activity_get_view(activity);
    if (!view)
    {
        return NULL;
    }

    cos_activity_set_title(activity, "Input Page Test");
    cos_activity_set_type(activity, COS_ACTIVITY_TYPE_APP);
    cos_activity_set_user_data(activity, (void *)view);

    return view;
}

void cos_test_input_page_start(void)
{
    lv_obj_t *scr = _create_input_test_scr();
    if (!scr)
    {
        COS_LOG_E("Failed to create test screen");
        return;
    }

    /* Create list container */
    lv_obj_t *list = cos_list_create(scr);
    _ctx.list = list;
    _ctx.container = scr;

    /* Add section header */
    lv_obj_t *label = lv_list_add_text(list, "Input Page Tests");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    /* Add test buttons */
    lv_obj_t *btn;

    btn = lv_list_add_button(list, NULL, "Test 1: Open with Label");
    lv_obj_add_event_cb(btn, _test_input_page_with_label, LV_EVENT_CLICKED, NULL);

    btn = lv_list_add_button(list, NULL, "Test 2: Open with Callback");
    lv_obj_add_event_cb(btn, _test_input_page_with_callback, LV_EVENT_CLICKED, NULL);

    btn = lv_list_add_button(list, NULL, "Test 3: Verify Label Update");
    lv_obj_add_event_cb(btn, _test_verify_label_update, LV_EVENT_CLICKED, NULL);

    btn = lv_list_add_button(list, NULL, "Test 4: Verify Callback Data");
    lv_obj_add_event_cb(btn, _test_verify_callback_data, LV_EVENT_CLICKED, NULL);

    /* Add separator */
    lv_list_add_text(list, "");

    /* Add result display section */
    lv_list_add_text(list, "Test Results:");

    _ctx.result_label = lv_label_create(list);
    lv_label_set_text(_ctx.result_label, "Ready for testing");
    lv_obj_set_width(_ctx.result_label, lv_pct(100));
    lv_label_set_long_mode(_ctx.result_label, LV_LABEL_LONG_WRAP);

    /* Add separator */
    lv_list_add_text(list, "");

    /* Add input label for display */
    lv_list_add_text(list, "Input Label:");

    _ctx.input_label = lv_label_create(list);
    lv_label_set_text(_ctx.input_label, "Empty");
    lv_obj_set_width(_ctx.input_label, lv_pct(100));
    lv_label_set_long_mode(_ctx.input_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_ctx.input_label, lv_color_hex(0xFF9F0A), 0);

    /* Add separator */
    lv_list_add_text(list, "");

    /* Add summary section */
    lv_list_add_text(list, "Test Summary:");

    lv_obj_t *summary_label = lv_label_create(list);
    char summary_text[128];
    snprintf(summary_text,
             sizeof(summary_text),
             "Total: %u | Passed: %u | Failed: %u",
             _ctx.stats.total_tests,
             _ctx.stats.passed_tests,
             _ctx.stats.failed_tests);
    lv_label_set_text(summary_label, summary_text);
    _ctx.summary_label = summary_label;

    cos_crown_encoder_set_target_obj(list);

    cos_activity_t *activity = cos_activity_create(NULL);
    cos_activity_set_view(activity, scr);
    cos_activity_enter(activity);
}

#endif /* COS_ENABLE_TEST_APP */
