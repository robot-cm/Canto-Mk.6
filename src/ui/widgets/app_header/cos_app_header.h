/**
 * @file cos_app_header.h
 * @brief Application top navigation header
 */

#ifndef COS_APP_HEADER_H
#define COS_APP_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_lang.h"
#include "cos_activity.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Hide app header
 */
void cos_app_header_hide(void);
/**
 * @brief Show app header
 * @param a Activity to show app header
 */
void cos_app_header_show(cos_activity_t *a);
/**
 * @brief Animatically show or hide app header
 * @param a Target activity (used to refresh title and color when showing, can be NULL when hiding)
 * @param visible Whether to show
 * @param duration_ms Animation duration (milliseconds), immediate switch when 0
 */
void cos_app_header_set_visible_animated(cos_activity_t *a, bool visible, uint32_t duration_ms);

/**
 * @brief Slide app header vertically to show or hide it
 * @param a Target activity (used to refresh title and color when showing, can be NULL when hiding)
 * @param visible Whether to show
 * @param duration_ms Animation duration (milliseconds), immediate switch when 0
 *
 * When hiding, the header slides upward out of the screen.
 * When showing, the header slides downward back into place.
 */
void cos_app_header_slide_visible_animated(cos_activity_t *a, bool visible, uint32_t duration_ms);
/**
 * @brief Initialize app header
 *
 * App header will be placed in lv_layer_top() layer
 *
 * To hide app header use `cos_app_header_hide`
 *
 * To show app header use `cos_app_header_show`
 *
 * @warning App header can only be initialized once
 */
void cos_app_header_init(void);
/**
 * @brief Check if app header is currently visible
 */
bool cos_app_header_is_visible(void);
/**
 * @brief Set whether the header back button is visible.
 * @param visible true = show back button, false = hide it
 * @note Apps exit via swipe-back, so the header back button is hidden
 *       while an app page is active (see cos_app_list _app_on_enter).
 */
void cos_app_header_set_back_btn_visible(bool visible);
/**
 * @brief Set whether the header clock label is visible.
 * @param visible true = show clock, false = hide it
 * @note Settings pages hide the header clock because the status bar
 *       already shows the time. Other header pages re-enable it.
 */
void cos_app_header_set_clock_visible(bool visible);
/**
 * @brief Attach app header to specified view
 * @param view View to attach
 */
void cos_app_header_attach_to_view(lv_obj_t *view);
/**
 * @brief Detach app header from view, restore to original parent object
 */
void cos_app_header_detach_from_view(void);
/**
 * @brief Check if app header is currently attached to a view
 */
bool cos_app_header_is_attached_to_view(void);
/**
 * @brief Play title change animation
 * @param from From which activity to switch
 * @param to Switch to which activity
 * @param need_anim Whether animation is needed
 * @param reverse_anim Whether to reverse animation
 */
void _play_title_changed_anim(cos_activity_t *from,
                              cos_activity_t *to,
                              bool need_anim,
                              bool reverse_anim,
                              lv_anim_timeline_t *at);
#ifdef __cplusplus
}
#endif

#endif /* COS_APP_HEADER_H */
