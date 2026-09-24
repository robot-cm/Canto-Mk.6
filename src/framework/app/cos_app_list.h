/**
 * @file cos_app_list.h
 * @brief App list page
 */

#ifndef COS_APP_LIST_H
#define COS_APP_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_core.h"
#include "cos_activity.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef void (*cos_sys_app_entry_t)(void);

enum
{
    COS_SYS_APP_SETTINGS = 0,
    COS_SYS_APP_FLASH_LIGHT,
/* New system apps can be added here */
#if COS_ENABLE_TEST_APP
    COS_SYS_APP_TEST,
#endif
    COS_SYS_APP_LAST
};

/* Native C apps (compiled into firmware, shown in the App page like plugins) */
enum
{
    COS_NATIVE_APP_ALBUM = 0,
    COS_NATIVE_APP_TEXTHUB,
    COS_NATIVE_APP_DICTIONARY,
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    COS_NATIVE_APP_USB_MSC,
#endif
#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE
    COS_NATIVE_APP_SPOTIFY,
#endif
#if defined(CONFIG_PROFMONITOR_APP_ENABLE) && CONFIG_PROFMONITOR_APP_ENABLE
    COS_NATIVE_APP_PROFMONITOR,
#endif
    COS_NATIVE_APP_EQSOLVER,
/* New native C apps can be added here */
    COS_NATIVE_APP_LAST
};

extern const char *cos_sys_app_id_list[COS_SYS_APP_LAST];
extern const char *cos_sys_app_icon_list[COS_SYS_APP_LAST];
extern const char *cos_native_app_id_list[COS_NATIVE_APP_LAST];
extern const char *cos_native_app_icon_list[COS_NATIVE_APP_LAST];

/* Public function prototypes --------------------------------*/

/**
 * @brief Immediately launch the target app by id from any page
 * @param app_id Target app id
 * @return cos_result_t Launch result
 */
cos_result_t cos_app_launch_immediately(const char *app_id);
/**
 * @brief Get the app id of the most recently launched app
 * @return const char* App id string, or NULL if no app was launched yet.
 * @note Keeps its value even after returning to the watchface; check the
 *       current activity type to know whether an app is truly foreground.
 *       Used by the board layer to snapshot/restore UI across deep sleep.
 */
const char *cos_app_list_get_last_launch_app_id(void);
/**
 * @brief Enter app list
 * @return cos_activity_t* App list activity object
 */
void cos_app_list_enter(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_APP_LIST_H */
