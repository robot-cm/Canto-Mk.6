/**
 * @file cos_test_runner.h
 * @brief Global unit test runner - aggregates all unit test registrations
 */

#ifndef COS_TEST_RUNNER_H
#define COS_TEST_RUNNER_H

#include "cos_config.h"
#if COS_ENABLE_TEST_APP

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/
void cos_test_runner_register_all(void);
void cos_test_runner_start(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_ENABLE_TEST_APP */
#endif /* COS_TEST_RUNNER_H */
