/**
 * @file eos_service_power_save.h
 * @brief Power save mode service (省电模式)
 *
 * 省电模式约束:
 *   - 只能显示主界面(watchface),禁止进入其他页面/应用
 *   - 主界面底部按钮长按退出
 *   - 启用时: 低亮度 + 降低 CPU 频率
 */

#ifndef EOS_SERVICE_POWER_SAVE_H
#define EOS_SERVICE_POWER_SAVE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>
#include "eos_core.h"
#include "eos_event.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize power save service (load persisted state)
 */
eos_result_t eos_service_power_save_init(void);

/**
 * @brief Check whether power save mode is active
 */
bool eos_power_save_is_active(void);

/**
 * @brief Enter power save mode (低亮度 + 降频 + 回主界面 + 锁定)
 */
eos_result_t eos_power_save_enter(void);

/**
 * @brief Exit power save mode (恢复亮度/频率 + 解锁)
 */
eos_result_t eos_power_save_exit(void);

/**
 * @brief Get power-save state change event id (for subscribe)
 */
eos_event_code_t eos_power_save_get_event_id(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SERVICE_POWER_SAVE_H */
