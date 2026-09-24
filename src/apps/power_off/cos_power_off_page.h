/**
 * @file eos_power_off_page.h
 * @brief 关机页(主界面向上滑动手势打开)
 */
#ifndef EOS_POWER_OFF_PAGE_H
#define EOS_POWER_OFF_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>

/* Public function prototypes --------------------------------*/

/**
 * @brief 打开关机页(半透明遮罩 + 圆形关机按钮 + 提示文字)
 * @note 点关机按钮 → eos_pm_power_off()(真机进入硬件深睡,不返回);
 *       点遮罩空白 → 关闭页面。
 */
void eos_power_off_page_open(void);

/**
 * @brief 关闭关机页
 */
void eos_power_off_page_close(void);

/**
 * @brief 关机页是否已打开
 */
bool eos_power_off_page_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_POWER_OFF_PAGE_H */
