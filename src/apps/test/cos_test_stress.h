/**
 * @file cos_test_stress.h
 * @brief SPM stress test - memory leak check
 */

#ifndef COS_TEST_STRESS_H
#define COS_TEST_STRESS_H

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
void cos_test_stress_register_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_ENABLE_TEST_APP */
#endif /* COS_TEST_STRESS_H */
