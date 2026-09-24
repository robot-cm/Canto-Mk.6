/**
 * @file cos_test_package.h
 * @brief Package installation test module header
 */

#ifndef COS_TEST_PACKAGE_H
#define COS_TEST_PACKAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "cos_config.h"

#if COS_ENABLE_TEST_APP

/* Public function prototypes --------------------------------*/

/**
 * @brief Start package installation test
 *
 * Creates a new activity with input field and install button for installing
 * .eapk (application) and .ewpk (watchface) packages.
 */
void cos_test_package_start(void);

#endif /* COS_ENABLE_TEST_APP */

#ifdef __cplusplus
}
#endif

#endif /* COS_TEST_PACKAGE_H */
