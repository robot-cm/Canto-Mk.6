/**
 * @file cos_test_framework.h
 * @brief Unit test framework - registration, execution, UI page
 */

#ifndef COS_TEST_FRAMEWORK_H
#define COS_TEST_FRAMEWORK_H

#include "cos_config.h"
#if COS_ENABLE_TEST_APP

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

/* Public macros ----------------------------------------------*/
#define COS_TEST_NAME_MAX 80
#define COS_TEST_MAX 256

#define COS_EXPECT_TRUE(cond, name, msg) cos_test_record(name, (cond), msg)

#define COS_EXPECT_FALSE(cond, name, msg) cos_test_record(name, !(cond), msg)

#define COS_EXPECT_EQ(a, b, name, msg) cos_test_record(name, (a) == (b), msg)

#define COS_EXPECT_NE(a, b, name, msg) cos_test_record(name, (a) != (b), msg)

#define COS_EXPECT_NOT_NULL(ptr, name, msg) cos_test_record(name, (ptr) != NULL, msg)

#define COS_EXPECT_NULL(ptr, name, msg) cos_test_record(name, (ptr) == NULL, msg)

/* Public typedefs --------------------------------------------*/
typedef bool (*cos_test_fn_t)(void);

/* Public function prototypes --------------------------------*/
void cos_test_register(const char *name, cos_test_fn_t fn);
void cos_test_record(const char *name, bool passed, const char *detail);
void cos_test_reset(void);
void cos_test_run_all(void);
void cos_test_run_group(const char *prefix);
uint32_t cos_test_get_total(void);
uint32_t cos_test_get_passed(void);
uint32_t cos_test_get_failed(void);
void cos_test_fw_page_start(const char *title);
bool cos_test_assert(bool cond, const char *file, int line, const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* COS_ENABLE_TEST_APP */
#endif /* COS_TEST_FRAMEWORK_H */
