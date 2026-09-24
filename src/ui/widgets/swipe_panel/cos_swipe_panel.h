/**
 * @file cos_swipe_panel.h
 * @brief Swipe panel header file
 */

#ifndef COS_SWIPE_PANEL_H
#define COS_SWIPE_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_core.h"
#include "lvgl.h"
#include "cos_slide_widget.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
/**
 * @brief SwipePanel drag direction
 */
typedef enum
{
    COS_SWIPE_DIR_UP = 0, // Slide up to reveal swipe_obj
    COS_SWIPE_DIR_DOWN = 1, // Slide down to reveal swipe_obj
    COS_SWIPE_DIR_LEFT = 2, // Slide left to reveal swipe_obj
    COS_SWIPE_DIR_RIGHT = 3, // Slide right to reveal swipe_obj
    COS_SWIPE_DIR_COUNT // Total number of directions
} cos_swipe_dir_t;
/**
 * @brief SwipePanel structure definition
 */
typedef struct
{
    cos_slide_widget_t *sw;
    lv_obj_t *swipe_obj;
    lv_obj_t *handle_bar;
    cos_swipe_dir_t dir;
} cos_swipe_panel_t;
/* Public function prototypes --------------------------------*/

/**
 * @brief Slide the swipe panel into screen
 * @param sp Target swipe panel
 */
void cos_swipe_panel_slide_down(cos_swipe_panel_t *sp);
/**
 * @brief Delete SwipePanel
 * @param swipe_panel Swipe panel
 */
void cos_swipe_panel_delete(cos_swipe_panel_t *swipe_panel);

/**
 * @brief Create SwipePanel
 * @param parent Parent object of the swipe panel
 * @return Pointer to the created SwipePanel
 * @note Must be deleted with cos_swipe_panel_delete when not in use, otherwise memory leak may occur
 */
cos_swipe_panel_t *cos_swipe_panel_create(lv_obj_t *parent);

/**
 * @brief Set drag direction
 * @param swipe_panel Swipe panel
 * @param dir Drag direction, e.g., COS_SWIPE_DIR_DOWN means dragging down to reveal swipe_obj
 */
void cos_swipe_panel_set_dir(cos_swipe_panel_t *swipe_panel, const cos_swipe_dir_t dir);

/**
 * @brief Allow the whole panel (not just its edge touch strip) to be dragged
 *        when open — so an opened overlay can be dismissed by dragging
 *        anywhere on it (e.g. swipe-down anywhere to close the cards page).
 */
void cos_swipe_panel_set_full_drag(cos_swipe_panel_t *swipe_panel, bool enable);

/**
 * @brief Hide HandleBar (small white bar)
 * @param swipe_panel Swipe panel
 */
void cos_swipe_panel_hide_handle_bar(cos_swipe_panel_t *swipe_panel);

/**
 * @brief Show HandleBar (small white bar)
 * @param swipe_panel Swipe panel
 */
void cos_swipe_panel_show_handle_bar(cos_swipe_panel_t *swipe_panel);

/**
 * @brief Externally trigger pull-back animation, automatically pull back outside screen
 * @param swipe_panel Swipe panel
 */
void cos_swipe_panel_pull_back(cos_swipe_panel_t *swipe_panel);

/**
 * @brief Trigger pull animation, pull swipe panel to specified pixel position (absolute coordinates)
 *
 * When swipe panel is horizontal, target controls x-axis coordinate
 *
 * When swipe panel is vertical, target controls y-axis coordinate
 * @param swipe_panel Swipe panel
 * @param target Target position in pixels
 * @param anim Whether to enable animation
 */
void cos_swipe_panel_move(cos_swipe_panel_t *swipe_panel, int32_t target, bool anim);
/**
 * @brief Show swipe panel
 */
void cos_swipe_panel_show(cos_swipe_panel_t *sp);
/**
 * @brief Hide swipe panel
 */
void cos_swipe_panel_hide(cos_swipe_panel_t *sp);
#ifdef __cplusplus
}
#endif

#endif /* COS_SWIPE_PANEL_H */
