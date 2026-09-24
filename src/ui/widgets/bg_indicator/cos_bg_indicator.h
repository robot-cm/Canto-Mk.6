/**
 * @file cos_bg_indicator.h
 * @brief Background App Indicator (Redmi Watch style)
 *
 * Maintains a list of apps that are running in the background and draws a small
 * (16~24 px) circular icon at the TOP-CENTER of the watch face, above every
 * watch face (it lives in the overlay indicator layer, not inside any watch
 * face layout). Tapping the icon restores the app to the foreground.
 *
 * States:
 *   - APP_RUNNING : show the icon
 *   - APP_CLOSED  : remove the icon
 *
 * The app layer calls cos_bg_indicator_register() from the activity's
 * on_pause() and cos_bg_indicator_unregister() from on_resume()/on_destroy().
 */

#ifndef COS_BG_INDICATOR_H
#define COS_BG_INDICATOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "lvgl.h"
#include "cos_activity.h"

/* Public macros ----------------------------------------------*/
#define COS_BG_INDICATOR_ICON_SIZE 20 /**< Indicator icon size in px */
#define COS_BG_INDICATOR_MAX_APPS 4   /**< Max simultaneous background apps */

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize the Background App Indicator overlay (icon container)
 * @note Call once during system init, after cos_overlay_layer_init()
 */
void cos_bg_indicator_init(void);

/**
 * @brief Register a background-running app and show its indicator icon
 * @param app_id App id (owned by caller, copied internally)
 * @param icon_src Icon image path (e.g. icon.bin PNG), or NULL for default
 * @param activity Keep-alive activity to restore on icon tap (may be NULL)
 * @return cos_result_t COS_OK success, COS_FAILED / COS_ERR_* failure
 */
cos_result_t cos_bg_indicator_register(const char *app_id,
                                       const char *icon_src,
                                       cos_activity_t *activity);

/**
 * @brief Unregister a background app and remove its indicator icon
 * @param app_id App id
 * @return cos_result_t COS_OK success, COS_FAILED if not found
 */
cos_result_t cos_bg_indicator_unregister(const char *app_id);

/**
 * @brief Show or hide the whole background-app indicator icon container.
 *        The activity layer calls this so the indicator is only visible on
 *        the watchface (user requirement: 标只能在主界面出现).
 * @param visible true to show, false to hide
 */
void cos_bg_indicator_set_visible(bool visible);

/**
 * @brief Whether any app is currently running in the background
 */
bool cos_bg_indicator_has_any(void);

/**
 * @brief Number of background-running apps
 */
uint32_t cos_bg_indicator_count(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_BG_INDICATOR_H */
