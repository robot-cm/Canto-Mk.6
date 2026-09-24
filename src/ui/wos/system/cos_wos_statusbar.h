/**
 * @file eos_wos_statusbar.h
 * @brief WOS top info bar — always-on thin info strip at the top.
 *
 * Lives on the statusbar overlay layer, above the app page and notifications.
 * Single centered small label, rotates every 60s between:
 *   - "HH:MM <batt>%"   (time + battery)
 *   - "M/D Wed <batt>%" (date + weekday + battery)
 * Transparent and small — never covers page content.
 */
#ifndef EOS_WOS_STATUSBAR_H
#define EOS_WOS_STATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Initialize the top info bar (creates it on the statusbar layer).
 * @note Call after eos_overlay_layer_init().
 */
void wos_statusbar_init(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_WOS_STATUSBAR_H */
