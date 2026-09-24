/**
 * @file cos_wos_notification.h
 * @brief WOS notification layer — one glass card slides in from the top,
 *        auto-dismisses after a few seconds.
 */
#ifndef COS_WOS_NOTIFICATION_H
#define COS_WOS_NOTIFICATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Initialize the notification layer.
 * @note Call after cos_overlay_layer_init().
 */
void wos_notification_init(void);

/**
 * @brief Show a notification card.
 * @param title One-line title (e.g. app name)
 * @param body  One-line body text
 * @param hold_ms Time the card stays before sliding out (e.g. 3000)
 */
void wos_notification_show(const char *title, const char *body, uint32_t hold_ms);

/**
 * @brief Dismiss the current notification immediately.
 */
void wos_notification_dismiss(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_WOS_NOTIFICATION_H */
