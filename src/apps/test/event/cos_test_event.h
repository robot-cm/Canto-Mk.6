/**
 * @file cos_test_event.h
 * @brief Event system test module header
 */

#ifndef COS_TEST_EVENT_H
#define COS_TEST_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/
void cos_test_event_start(void);
void cos_test_event_register_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_TEST_EVENT_H */
