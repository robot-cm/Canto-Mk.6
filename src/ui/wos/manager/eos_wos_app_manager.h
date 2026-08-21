/**
 * @file eos_wos_app_manager.h
 * @brief WOS App Manager — the ONLY entry point for page switching.
 *
 * Guarantees (see design doc §2.3, §4, §8):
 *  - At most ONE app page visible at any time
 *  - open() force-closes the current app first
 *  - close() deletes every LVGL child of the root, deletes the ledger
 *    timers, then runs the cleanup audit (child count must be 0)
 *  - No global LVGL objects are created here — only the app root container
 */
#ifndef EOS_WOS_APP_MANAGER_H
#define EOS_WOS_APP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "core/eos_wos_app.h"
#include <stdbool.h>

#define WOS_MAX_REGISTERED_APPS 16

typedef enum
{
    WOS_APP_STATE_IDLE = 0,
    WOS_APP_STATE_LAUNCHING,
    WOS_APP_STATE_ACTIVE,
    WOS_APP_STATE_CLOSING,
} wos_app_state_t;

/**
 * @brief Initialize the App Manager (must run after eos_overlay_layer_init).
 */
void wos_app_manager_init(void);

/**
 * @brief Register an app descriptor so it can be opened by id.
 */
bool wos_app_manager_register(const eos_wos_app_desc_t *desc);

/**
 * @brief Open an app by id. Closes the current app first, plays the
 *        open animation, and returns true on success.
 */
bool wos_app_manager_open(const char *id);

/**
 * @brief Close the current app (shrink+fade), then fully clean up.
 */
void wos_app_manager_close(void);

/**
 * @brief Get the currently active app (NULL if none).
 */
eos_wos_app_t *wos_app_manager_get_active(void);

/**
 * @brief Get the current manager state.
 */
wos_app_state_t wos_app_manager_get_state(void);

/**
 * @brief Get the id of the active app ("" when none).
 */
const char *wos_app_manager_active_id(void);

/**
 * @brief Number of registered apps.
 */
int wos_app_manager_registered_count(void);

/**
 * @brief Get the i-th registered app descriptor (for shell listing).
 */
const eos_wos_app_desc_t *wos_app_manager_registered(int i);

#ifdef __cplusplus
}
#endif

#endif /* EOS_WOS_APP_MANAGER_H */
