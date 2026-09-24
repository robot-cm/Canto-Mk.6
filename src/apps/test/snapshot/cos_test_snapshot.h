/**
 * @file cos_test_snapshot.h
 * @brief LVGL snapshot performance test - measures tick cost of full-screen snapshot
 */

#ifndef COS_TEST_SNAPSHOT_H
#define COS_TEST_SNAPSHOT_H

#include "cos_config.h"
#if COS_ENABLE_TEST_APP

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public function prototypes --------------------------------*/
void cos_test_snapshot_register_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_ENABLE_TEST_APP */
#endif /* COS_TEST_SNAPSHOT_H */
