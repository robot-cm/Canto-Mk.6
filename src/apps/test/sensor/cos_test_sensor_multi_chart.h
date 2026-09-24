/**
 * @file cos_test_sensor_multi_chart.h
 * @brief Multi-sensor sampling status visualization test header
 */

#ifndef COS_TEST_SENSOR_MULTI_CHART_H
#define COS_TEST_SENSOR_MULTI_CHART_H

#include "cos_config.h"

#if COS_ENABLE_TEST_APP

#include "cos_activity.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start multi-sensor chart test activity
 */
void cos_test_sensor_multi_chart_start(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_ENABLE_TEST_APP */

#endif /* COS_TEST_SENSOR_MULTI_CHART_H */
