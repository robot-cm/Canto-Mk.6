/**
 * @file eos_app_list.h
 * @brief App list page
 */

#ifndef EOS_APP_LIST_H
#define EOS_APP_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "eos_core.h"
#include "eos_activity.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef void (*eos_sys_app_entry_t)(void);

enum
{
    EOS_SYS_APP_SETTINGS = 0,
    EOS_SYS_APP_FLASH_LIGHT,
/* New system apps can be added here */
#if EOS_ENABLE_TEST_APP
    EOS_SYS_APP_TEST,
#endif
    EOS_SYS_APP_LAST
};

/* Native C apps (compiled into firmware, shown in the App page like plugins) */
enum
{
    EOS_NATIVE_APP_ALBUM = 0,
    EOS_NATIVE_APP_TEXTHUB,
    EOS_NATIVE_APP_DICTIONARY,
/* New native C apps can be added here */
    EOS_NATIVE_APP_LAST
};

extern const char *eos_sys_app_id_list[EOS_SYS_APP_LAST];
extern const char *eos_sys_app_icon_list[EOS_SYS_APP_LAST];
extern const char *eos_native_app_id_list[EOS_NATIVE_APP_LAST];
extern const char *eos_native_app_icon_list[EOS_NATIVE_APP_LAST];

/* Public function prototypes --------------------------------*/

/**
 * @brief Immediately launch the target app by id from any page
 * @param app_id Target app id
 * @return eos_result_t Launch result
 */
eos_result_t eos_app_launch_immediately(const char *app_id);
/**
 * @brief Get the app id of the most recently launched app
 * @return const char* App id string, or NULL if no app was launched yet.
 * @note Keeps its value even after returning to the watchface; check the
 *       current activity type to know whether an app is truly foreground.
 *       Used by the board layer to snapshot/restore UI across deep sleep.
 */
const char *eos_app_list_get_last_launch_app_id(void);
/**
 * @brief Enter app list
 * @return eos_activity_t* App list activity object
 */
void eos_app_list_enter(void);
#ifdef __cplusplus
}
#endif

#endif /* EOS_APP_LIST_H */
