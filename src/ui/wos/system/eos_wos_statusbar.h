/**
 * @file eos_wos_statusbar.h
 * @brief WOS status bar — always-on thin glass strip at the top.
 *
 * Lives on the statusbar overlay layer, above the app page and notifications.
 * Left: app title (or watch name). Right: clock + battery.
 */
#ifndef EOS_WOS_STATUSBAR_H
#define EOS_WOS_STATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Initialize the status bar (creates it on the statusbar layer).
 * @note Call after eos_overlay_layer_init().
 */
void wos_statusbar_init(void);

/**
 * @brief Set the left title text.
 */
void wos_statusbar_set_title(const char *title);

/**
 * @brief Force an immediate clock refresh (also runs on a 30s timer).
 */
void wos_statusbar_update_clock(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_WOS_STATUSBAR_H */
