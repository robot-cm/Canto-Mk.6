/**
 * @file eos_control_center.h
 * @brief Pull-up control center
 */

#ifndef EOS_CONTROL_CENTER_H
#define EOS_CONTROL_CENTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "eos_chrome_manager.h"
#include "eos_swipe_panel.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
typedef struct
{
    eos_swipe_panel_t *swipe_panel;
    lv_obj_t *container;
    lv_obj_t *flash_light_btn;
    lv_obj_t *brightness_btn;
    lv_obj_t *power_save_btn;
    lv_obj_t *beast_mode_btn;
    lv_obj_t *wifi_btn;
    lv_obj_t *bl_btn;
    lv_obj_t *settings_btn;
} eos_control_center_t;
/* Public function prototypes --------------------------------*/

/**
 * @brief Pull control center into screen
 */
void eos_control_panel_slide_change(void);
/**
 * @brief Show control center
 */
void eos_control_center_show(void);
/**
 * @brief Hide control center
 */
void eos_control_center_hide(void);
/**
 * @brief Query whether the control center overlay is currently open
 * @return true if the swipe panel is in the OPEN state
 */
bool eos_control_center_is_open(void);
/**
 * @brief Get control center instance
 * @return eos_control_center_t*
 */
eos_control_center_t *eos_control_center_get_instance(void);
/**
 * @brief Initialize control center, create a control center instance
 */
void eos_control_center_init(void);
const eos_chrome_overlay_t *eos_control_center_get_overlay_descriptor(void);
#ifdef __cplusplus
}
#endif

#endif /* EOS_CONTROL_CENTER_H */
