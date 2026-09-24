/**
 * @file cos_service_beast_mode.h
 * @brief Beast mode service (性能模式)
 *
 * 性能模式与省电模式(Power Save)互斥:开启一方会自动退出另一方。
 * 两者都不开启时 = 智能模式(系统默认的平衡调频)。
 */

#ifndef COS_SERVICE_BEAST_MODE_H
#define COS_SERVICE_BEAST_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>
#include "cos_core.h"
#include "cos_event.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize beast mode service (load persisted state)
 */
cos_result_t cos_service_beast_mode_init(void);

/**
 * @brief Check whether beast mode is active
 */
bool cos_beast_mode_is_active(void);

/**
 * @brief Enter beast mode (CPU performance max, mutually exclusive with power save)
 */
cos_result_t cos_beast_mode_enter(void);

/**
 * @brief Exit beast mode (back to smart mode)
 */
cos_result_t cos_beast_mode_exit(void);

/**
 * @brief Get beast mode state-change event id (for subscribe)
 */
cos_event_code_t cos_beast_mode_get_event_id(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_BEAST_MODE_H */
