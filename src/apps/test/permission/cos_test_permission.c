/**
 * @file cos_test_permission.c
 * @brief Comprehensive permission system test module
 */

#include "cos_test_permission.h"
#include "cos_config.h"
#if COS_ENABLE_TEST_APP

/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include "cos_activity.h"
#include "cos_app_header.h"
#include "cos_crown.h"
#include "cos_service_permission.h"
#include "cos_log.h"
#include "cos_basic_widgets.h"
#include "cos_lang.h"
#include "lvgl.h"
#include "cos_test_framework.h"

/* Macros and Definitions -------------------------------------*/
#define COS_LOG_TAG "PermTest"
#define TEST_APP_ID_1 "com.test.app1"
#define TEST_APP_ID_2 "com.test.app2"

/* Variables --------------------------------------------------*/
typedef struct
{
    lv_obj_t *container;
    lv_obj_t *list;
    lv_obj_t *result_label;
    struct
    {
        uint32_t total_tests;
        uint32_t passed_tests;
        uint32_t failed_tests;
    } stats;
} _test_context_t;

static _test_context_t _ctx = {0};

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

    cos_test_record(name, passed, details);

    if (_ctx.list)
    {
        char label_text[256];
        snprintf(label_text, sizeof(label_text), "%s: %s", name, passed ? "PASS" : "FAIL");

        lv_obj_t *btn = lv_list_add_button(_ctx.list, NULL, label_text);

        if (!passed)
        {
            lv_obj_set_style_text_color(btn, lv_color_hex(0xFF0000), 0);
        }
    }
}

/* ---- Test Cases ---- */

/**
 * @brief Test: Default permission state is DENIED for new apps
 */
static bool _test_default_state_denied(void)
{
    /* Use a unique app ID to avoid conflicts with existing data */
    const char *app_id = "com.test.unique.default";

    /* Get permission state before setting anything */
    cos_perm_state_t state = cos_permission_get(app_id, COS_PERM_CATEGORY_LOCATION);

    bool passed = (state == COS_PERM_STATE_DENIED);
    _record_test("Default State is DENIED",
                 passed,
                 passed ? "New app permissions default to DENIED" : "Default state incorrect");
    return passed;
}

/**
 * @brief Test: Set and get permission state
 */
static bool _test_set_and_get(void)
{
    const char *app_id = TEST_APP_ID_1;

    /* Set location permission to ALLOW_ALWAYS */
    bool set_ok = cos_permission_set(app_id, COS_PERM_CATEGORY_LOCATION, COS_PERM_STATE_ALLOW_ALWAYS);
    if (!set_ok)
    {
        _record_test("Set Permission", false, "Failed to set permission");
        return false;
    }

    /* Read it back */
    cos_perm_state_t state = cos_permission_get(app_id, COS_PERM_CATEGORY_LOCATION);
    bool passed = (state == COS_PERM_STATE_ALLOW_ALWAYS);
    _record_test("Set and Get Permission",
                 passed,
                 passed ? "Permission state persisted correctly" : "State mismatch after set");
    return passed;
}

/**
 * @brief Test: All permission states can be set and retrieved
 */
static bool _test_all_states(void)
{
    const char *app_id = "com.test.states";
    bool all_passed = true;

    cos_perm_state_t test_states[] = {COS_PERM_STATE_DENIED,
                                      COS_PERM_STATE_ALLOW_ONCE,
                                      COS_PERM_STATE_ALLOW_FOREGROUND,
                                      COS_PERM_STATE_ALLOW_ALWAYS};

    const char *state_names[] = {"DENIED", "ALLOW_ONCE", "ALLOW_FOREGROUND", "ALLOW_ALWAYS"};

    for (int i = 0; i < 4; i++)
    {
        bool ok = cos_permission_set(app_id, COS_PERM_CATEGORY_SENSOR, test_states[i]);
        if (!ok)
        {
            all_passed = false;
            break;
        }

        cos_perm_state_t read_back = cos_permission_get(app_id, COS_PERM_CATEGORY_SENSOR);
        if (read_back != test_states[i])
        {
            all_passed = false;
            break;
        }
    }

    char details[128];
    snprintf(details,
             sizeof(details),
             "%s",
             all_passed ? "All 4 states can be set and read back correctly"
                        : "State round-trip failed for one or more states");
    _record_test("All Grant States", all_passed, details);
    return all_passed;
}

/**
 * @brief Test: Different apps have isolated permission stores
 */
static bool _test_app_isolation(void)
{
    /* Set permission for app1 */
    cos_permission_set(TEST_APP_ID_1, COS_PERM_CATEGORY_NOTIFICATION, COS_PERM_STATE_ALLOW_ALWAYS);

    /* App2 should still have default DENIED */
    cos_perm_state_t app2_state = cos_permission_get(TEST_APP_ID_2, COS_PERM_CATEGORY_NOTIFICATION);

    bool passed = (app2_state == COS_PERM_STATE_DENIED);
    _record_test("App Isolation",
                 passed,
                 passed ? "Different apps have independent permission stores" : "App isolation violated");
    return passed;
}

/**
 * @brief Test: Same app can have different permissions per category
 */
static bool _test_category_independence(void)
{
    const char *app_id = "com.test.categories";

    cos_permission_set(app_id, COS_PERM_CATEGORY_LOCATION, COS_PERM_STATE_ALLOW_ALWAYS);
    cos_permission_set(app_id, COS_PERM_CATEGORY_STORAGE, COS_PERM_STATE_DENIED);
    cos_permission_set(app_id, COS_PERM_CATEGORY_AUDIO, COS_PERM_STATE_ALLOW_FOREGROUND);

    cos_perm_state_t loc = cos_permission_get(app_id, COS_PERM_CATEGORY_LOCATION);
    cos_perm_state_t sto = cos_permission_get(app_id, COS_PERM_CATEGORY_STORAGE);
    cos_perm_state_t aud = cos_permission_get(app_id, COS_PERM_CATEGORY_AUDIO);

    bool passed =
        (loc == COS_PERM_STATE_ALLOW_ALWAYS && sto == COS_PERM_STATE_DENIED && aud == COS_PERM_STATE_ALLOW_FOREGROUND);
    _record_test("Category Independence",
                 passed,
                 passed ? "Each category has independent state" : "Categories not independent");
    return passed;
}

/**
 * @brief Test: Revoke all permissions for an app
 */
static bool _test_revoke_all(void)
{
    const char *app_id = "com.test.revoke";

    /* Set multiple permissions */
    cos_permission_set(app_id, COS_PERM_CATEGORY_LOCATION, COS_PERM_STATE_ALLOW_ALWAYS);
    cos_permission_set(app_id, COS_PERM_CATEGORY_BLUETOOTH, COS_PERM_STATE_ALLOW_FOREGROUND);
    cos_permission_set(app_id, COS_PERM_CATEGORY_CONTACTS, COS_PERM_STATE_ALLOW_ONCE);

    /* Verify they were set */
    if (cos_permission_get(app_id, COS_PERM_CATEGORY_LOCATION) != COS_PERM_STATE_ALLOW_ALWAYS
        || cos_permission_get(app_id, COS_PERM_CATEGORY_BLUETOOTH) != COS_PERM_STATE_ALLOW_FOREGROUND
        || cos_permission_get(app_id, COS_PERM_CATEGORY_CONTACTS) != COS_PERM_STATE_ALLOW_ONCE)
    {
        _record_test("Revoke All Permissions", false, "Pre-condition: failed to set permissions");
        return false;
    }

    /* Revoke all */
    cos_permission_revoke_all(app_id);

    /* Verify all are now DENIED */
    bool loc_revoked = (cos_permission_get(app_id, COS_PERM_CATEGORY_LOCATION) == COS_PERM_STATE_DENIED);
    bool bt_revoked = (cos_permission_get(app_id, COS_PERM_CATEGORY_BLUETOOTH) == COS_PERM_STATE_DENIED);
    bool contacts_revoked = (cos_permission_get(app_id, COS_PERM_CATEGORY_CONTACTS) == COS_PERM_STATE_DENIED);

    bool passed = (loc_revoked && bt_revoked && contacts_revoked);
    _record_test("Revoke All Permissions",
                 passed,
                 passed ? "All permissions revoked successfully" : "Some permissions survived revocation");
    return passed;
}

/**
 * @brief Test: Permission name string to category enum conversion
 */
static bool _test_name_to_category(void)
{
    bool all_passed = true;

    struct
    {
        const char *name;
        cos_perm_category_t expected;
    } cases[] = {
        {"location", COS_PERM_CATEGORY_LOCATION},
        {"sensor", COS_PERM_CATEGORY_SENSOR},
        {"notification", COS_PERM_CATEGORY_NOTIFICATION},
        {"storage", COS_PERM_CATEGORY_STORAGE},
        {"bluetooth", COS_PERM_CATEGORY_BLUETOOTH},
        {"audio", COS_PERM_CATEGORY_AUDIO},
        {"health", COS_PERM_CATEGORY_HEALTH},
        {"contacts", COS_PERM_CATEGORY_CONTACTS},
        {"calendar", COS_PERM_CATEGORY_CALENDAR},
    };

    int num_cases = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < num_cases; i++)
    {
        cos_perm_category_t result = cos_permission_name_to_category(cases[i].name);
        if (result != cases[i].expected)
        {
            all_passed = false;
            break;
        }
    }

    /* Also test unknown name returns COUNT */
    cos_perm_category_t unknown = cos_permission_name_to_category("nonexistent_perm");
    if (unknown != COS_PERM_CATEGORY_COUNT)
    {
        all_passed = false;
    }

    char details[128];
    snprintf(details,
             sizeof(details),
             "%s",
             all_passed ? "All known names mapped correctly, unknown returns COUNT" : "Name mapping failed");
    _record_test("Name to Category Conversion", all_passed, details);
    return all_passed;
}

/**
 * @brief Test: Category key retrieval
 */
static bool _test_category_key(void)
{
    bool all_passed = true;

    struct
    {
        cos_perm_category_t cat;
        const char *expected_key;
    } cases[] = {
        {COS_PERM_CATEGORY_LOCATION, "location"},
        {COS_PERM_CATEGORY_SENSOR, "sensor"},
        {COS_PERM_CATEGORY_NOTIFICATION, "notification"},
        {COS_PERM_CATEGORY_STORAGE, "storage"},
        {COS_PERM_CATEGORY_BLUETOOTH, "bluetooth"},
        {COS_PERM_CATEGORY_AUDIO, "audio"},
        {COS_PERM_CATEGORY_HEALTH, "health"},
        {COS_PERM_CATEGORY_CONTACTS, "contacts"},
        {COS_PERM_CATEGORY_CALENDAR, "calendar"},
    };

    int num_cases = sizeof(cases) / sizeof(cases[0]);
    for (int i = 0; i < num_cases; i++)
    {
        const char *key = cos_permission_category_key(cases[i].cat);
        if (!key || strcmp(key, cases[i].expected_key) != 0)
        {
            all_passed = false;
            break;
        }
    }

    /* Invalid category should return NULL */
    const char *invalid_key = cos_permission_category_key(COS_PERM_CATEGORY_COUNT);
    if (invalid_key != NULL)
    {
        all_passed = false;
    }

    char details[128];
    snprintf(details,
             sizeof(details),
             "%s",
             all_passed ? "All category keys correct, invalid returns NULL" : "Category key mismatch");
    _record_test("Category Key Retrieval", all_passed, details);
    return all_passed;
}

/**
 * @brief Test: Null/invalid parameter handling
 */
static bool _test_null_safety(void)
{
    bool all_passed = true;

    /* Get with NULL app_id should return DENIED */
    cos_perm_state_t null_app = cos_permission_get(NULL, COS_PERM_CATEGORY_LOCATION);
    if (null_app != COS_PERM_STATE_DENIED)
    {
        all_passed = false;
    }

    /* Get with invalid category should return DENIED */
    cos_perm_state_t invalid_cat = cos_permission_get(TEST_APP_ID_1, COS_PERM_CATEGORY_COUNT);
    if (invalid_cat != COS_PERM_STATE_DENIED)
    {
        all_passed = false;
    }

    /* Set with NULL app_id should return false */
    bool null_set = cos_permission_set(NULL, COS_PERM_CATEGORY_LOCATION, COS_PERM_STATE_ALLOW_ALWAYS);
    if (null_set != false)
    {
        all_passed = false;
    }

    /* Set with invalid category should return false */
    bool invalid_cat_set = cos_permission_set(TEST_APP_ID_1, COS_PERM_CATEGORY_COUNT, COS_PERM_STATE_ALLOW_ALWAYS);
    if (invalid_cat_set != false)
    {
        all_passed = false;
    }

    /* Set with invalid state should return false */
    bool invalid_state_set = cos_permission_set(TEST_APP_ID_1, COS_PERM_CATEGORY_LOCATION, (cos_perm_state_t)99);
    if (invalid_state_set != false)
    {
        all_passed = false;
    }

    /* Revoke with NULL should not crash */
    cos_permission_revoke_all(NULL);

    /* Name conversion with NULL should return COUNT */
    cos_perm_category_t null_name = cos_permission_name_to_category(NULL);
    if (null_name != COS_PERM_CATEGORY_COUNT)
    {
        all_passed = false;
    }

    char details[128];
    snprintf(details,
             sizeof(details),
             "%s",
             all_passed ? "All invalid parameters handled safely" : "Null safety violation detected");
    _record_test("Null Safety / Edge Cases", all_passed, details);
    return all_passed;
}

/**
 * @brief Test: Overwrite existing permission state
 */
static bool _test_state_overwrite(void)
{
    const char *app_id = "com.test.overwrite";

    /* Set initial state */
    cos_permission_set(app_id, COS_PERM_CATEGORY_HEALTH, COS_PERM_STATE_ALLOW_ALWAYS);

    /* Overwrite with different state */
    cos_permission_set(app_id, COS_PERM_CATEGORY_HEALTH, COS_PERM_STATE_DENIED);

    cos_perm_state_t final = cos_permission_get(app_id, COS_PERM_CATEGORY_HEALTH);
    bool passed = (final == COS_PERM_STATE_DENIED);
    _record_test("State Overwrite", passed, passed ? "Permission state can be overwritten" : "Overwrite failed");
    return passed;
}

/**
 * @brief Test: All 9 categories are valid
 */
static bool _test_all_categories_valid(void)
{
    bool all_passed = true;

    for (int i = 0; i < COS_PERM_CATEGORY_COUNT; i++)
    {
        const char *key = cos_permission_category_key((cos_perm_category_t)i);
        const char *name = cos_permission_category_name((cos_perm_category_t)i);
        const char *desc = cos_permission_category_desc((cos_perm_category_t)i);

        if (!key || !name || !desc)
        {
            all_passed = false;
            break;
        }
    }

    char details[128];
    snprintf(details,
             sizeof(details),
             "Checked %d categories%s",
             COS_PERM_CATEGORY_COUNT,
             all_passed ? ", all valid" : ", some invalid");
    _record_test("All Categories Valid", all_passed, details);
    return all_passed;
}

/**
 * @brief Test: Revoke non-existent app does not crash
 */
static bool _test_revoke_nonexistent(void)
{
    /* Should not crash */
    cos_permission_revoke_all("com.test.does.not.exist");

    _record_test("Revoke Non-existent App", true, "Revoking non-existent app handled gracefully");
    return true;
}

/* ---- Test Runners ---- */

static void _run_basic_tests(void)
{
    _update_result("Running Basic Permission Tests...");

    _test_default_state_denied();
    _test_set_and_get();
    _test_all_states();
}

static void _run_isolation_tests(void)
{
    _update_result("Running Isolation Tests...");

    _test_app_isolation();
    _test_category_independence();
}

static void _run_advanced_tests(void)
{
    _update_result("Running Advanced Tests...");

    _test_revoke_all();
    _test_name_to_category();
    _test_category_key();
    _test_null_safety();
    _test_state_overwrite();
    _test_all_categories_valid();
    _test_revoke_nonexistent();
}

static void _cleanup_test_data(void)
{
    /* Clean up test artifacts so tests are repeatable */
    cos_permission_revoke_all(TEST_APP_ID_1);
    cos_permission_revoke_all(TEST_APP_ID_2);
    cos_permission_revoke_all("com.test.unique.default");
    cos_permission_revoke_all("com.test.states");
    cos_permission_revoke_all("com.test.categories");
    cos_permission_revoke_all("com.test.revoke");
    cos_permission_revoke_all("com.test.overwrite");
}

/* ---- UI Callbacks ---- */

static void _test_category_cb(lv_event_t *e)
{
    int category = (int)(long)lv_event_get_user_data(e);

    memset(&_ctx.stats, 0, sizeof(_ctx.stats));

    lv_obj_clean(_ctx.list);

    switch (category)
    {
        case 0:
            _run_basic_tests();
            break;
        case 1:
            _run_isolation_tests();
            break;
        case 2:
            _run_advanced_tests();
            break;
        case 3:
            _run_basic_tests();
            _run_isolation_tests();
            _run_advanced_tests();
            break;
        default:
            break;
    }

    /* Cleanup test data */
    _cleanup_test_data();

    char summary[256];
    snprintf(summary,
             sizeof(summary),
             "Total: %u | Pass: %u | Fail: %u",
             _ctx.stats.total_tests,
             _ctx.stats.passed_tests,
             _ctx.stats.failed_tests);
    _update_result(summary);
}

/* ---- Activity Entry Point ---- */

static cos_activity_lifecycle_t s_perm_test_activity_lifecycle = {
    .on_enter = NULL,
    .on_destroy = NULL,
    .on_pause = NULL,
    .on_resume = NULL,
};

void cos_test_permission_start(void)
{
    cos_activity_t *activity = cos_activity_create(&s_perm_test_activity_lifecycle);
    if (!activity)
    {
        return;
    }

    lv_obj_t *view = cos_activity_get_view(activity);
    if (!view)
    {
        return;
    }

    cos_activity_set_title(activity, "Permission Tests");
    cos_activity_set_type(activity, COS_ACTIVITY_TYPE_APP);

    _ctx.container = lv_obj_create(view);
    lv_obj_set_size(_ctx.container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(_ctx.container, 8, 0);
    lv_obj_set_flex_flow(_ctx.container, LV_FLEX_FLOW_COLUMN);

    /* Category selection list */
    lv_obj_t *cat_list = lv_list_create(_ctx.container);
    lv_obj_set_size(cat_list, lv_pct(100), lv_pct(35));
    lv_obj_set_flex_grow(cat_list, 1);
    cos_crown_encoder_set_target_obj(cat_list);

    const char *categories[] = {"Basic Tests", "Isolation Tests", "Advanced Tests", "Run All Tests"};

    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *btn = lv_list_add_button(cat_list, NULL, categories[i]);
        lv_obj_add_event_cb(btn, _test_category_cb, LV_EVENT_CLICKED, (void *)(long)i);
    }

    /* Results list */
    _ctx.list = lv_list_create(_ctx.container);
    lv_obj_set_size(_ctx.list, lv_pct(100), lv_pct(50));
    lv_obj_set_flex_grow(_ctx.list, 1);
    lv_obj_set_style_pad_all(_ctx.list, 4, 0);

    /* Summary label */
    _ctx.result_label = lv_label_create(_ctx.container);
    lv_label_set_text(_ctx.result_label, "Select a test category to begin");
    lv_obj_set_style_text_align(_ctx.result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(_ctx.result_label, lv_pct(100), LV_SIZE_CONTENT);

    cos_activity_enter(activity);
}

void cos_test_permission_register_tests(void)
{
    cos_test_register("Permission: default state DENIED", _test_default_state_denied);
    cos_test_register("Permission: set and get", _test_set_and_get);
    cos_test_register("Permission: all grant states", _test_all_states);
    cos_test_register("Permission: app isolation", _test_app_isolation);
    cos_test_register("Permission: category independence", _test_category_independence);
    cos_test_register("Permission: revoke all", _test_revoke_all);
    cos_test_register("Permission: name to category", _test_name_to_category);
    cos_test_register("Permission: category key", _test_category_key);
    cos_test_register("Permission: null safety", _test_null_safety);
    cos_test_register("Permission: state overwrite", _test_state_overwrite);
    cos_test_register("Permission: all categories valid", _test_all_categories_valid);
    cos_test_register("Permission: revoke nonexistent", _test_revoke_nonexistent);
}

#endif /* COS_ENABLE_TEST_APP */
