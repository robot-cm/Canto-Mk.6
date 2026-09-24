/**
 * @file cos_test_sensor_chart.h
 * @brief Sensor chart visualization test module header
 */

#ifndef COS_TEST_SENSOR_CHART_H
#define COS_TEST_SENSOR_CHART_H

#include "cos_config.h"

#if COS_ENABLE_TEST_APP

#include "cos_activity.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start sensor chart test activity
 */
void cos_test_sensor_chart_start(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_ENABLE_TEST_APP */

#endif /* COS_TEST_SENSOR_CHART_H */
