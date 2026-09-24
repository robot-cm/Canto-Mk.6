/**
 * @file cos_test_runner.c
 * @brief Global unit test runner - aggregates all unit test registrations
 */

#include "cos_test_runner.h"
#if COS_ENABLE_TEST_APP

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "cos_test_framework.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/
void cos_test_audio_register_tests(void);
void cos_test_stress_register_tests(void);
void cos_test_sensor_register_tests(void);
void cos_test_event_register_tests(void);
void cos_test_permission_register_tests(void);
void cos_test_snapshot_register_tests(void);

void cos_test_runner_register_all(void)
{
    cos_test_audio_register_tests();
    cos_test_stress_register_tests();
    cos_test_sensor_register_tests();
    cos_test_event_register_tests();
    cos_test_permission_register_tests();
    cos_test_snapshot_register_tests();
}

void cos_test_runner_start(void)
{
    cos_test_runner_register_all();
    cos_test_fw_page_start("Unit Tests");
}

#endif /* COS_ENABLE_TEST_APP */
