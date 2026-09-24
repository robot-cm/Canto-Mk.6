/**
 * @file cos_watchface_js.h
 * @brief JavaScript script watchface implementation
 */

#ifndef COS_WATCHFACE_JS_H
#define COS_WATCHFACE_JS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "cos_watchface.h"
/* Public function prototypes --------------------------------*/

/**
 * @brief Create a JS watchface instance with its own Activity
 * @param watchface_id Watchface package ID (e.g. "cn.sab1e.clock")
 * @return cos_watchface_instance_t* Instance pointer, NULL on failure
 *
 * The instance loads the script package from disk and owns its Activity.
 * Caller is responsible for destroying the instance when no longer needed.
 */
cos_watchface_instance_t *cos_watchface_js_create(const char *watchface_id);

#ifdef __cplusplus
}
#endif

#endif /* COS_WATCHFACE_JS_H */
