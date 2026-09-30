/**
 * @file eos_overlay_layer.h
 * @brief Layered overlay system on lv_layer_top()
 *
 * All layers are full-screen transparent containers with fixed sibling ordering.
 * From bottom to top:
 *
 *   lv_layer_top()
 *   +-- _user_top_layer     (layer 0 — app-level custom overlays [future SNI use])
 *   +-- _snapshot_layer     (layer 1 — activity transition snapshots, anim blockers)
 *   +-- _app_layer          (layer 2 — current app page root, owned by WOS App Manager)
 *   +-- _header_layer       (layer 3 — app header)
 *   +-- _notification_layer (layer 4 — notification cards)
 *   +-- _statusbar_layer    (layer 5 — always-on status bar: time/battery)
 *   +-- _indicator_layer    (layer 6 — background app indicator icons)
 *   +-- _overlay_layer      (layer 7 — system overlays: control center, dialogs)
 *   +-- ... direct children (flashlight, test code — above everything)
 *
 * Guarantees via sibling ordering within the same LVGL layer:
 *   - App page is BELOW header/statusbar/notifications/indicators
 *   - Overlays cover everything naturally; no hide/show coordination needed
 */
#ifndef EOS_OVERLAY_LAYER_H
#define EOS_OVERLAY_LAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "lvgl.h"

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize the overlay layers on lv_layer_top()
 * @note Must be called once during system init, before app_header_init()
 */
void eos_overlay_layer_init(void);

/**
 * @brief Get user top layer (layer 0 — bottommost, for future app-level overlays)
 */
lv_obj_t *eos_overlay_get_user_top_layer(void);

/**
 * @brief Get snapshot layer (layer 1 — transition snapshots, anim blockers)
 */
lv_obj_t *eos_overlay_get_snapshot_layer(void);

/**
 * @brief Get app layer (layer 2 — current app page root, owned by App Manager)
 */
lv_obj_t *eos_overlay_get_app_layer(void);

/**
 * @brief Get header layer (layer 3 — app header)
 */
lv_obj_t *eos_overlay_get_header_layer(void);

/**
 * @brief Get notification layer (layer 4 — notification cards)
 */
lv_obj_t *eos_overlay_get_notification_layer(void);

/**
 * @brief Get status bar layer (layer 5 — always-on status bar)
 */
lv_obj_t *eos_overlay_get_statusbar_layer(void);

/**
 * @brief Get indicator layer (layer 6 — background app indicator icons)
 */
lv_obj_t *eos_overlay_get_indicator_layer(void);

/**
 * @brief Get overlay layer (layer 7 — topmost, for system overlays)
 */
lv_obj_t *eos_overlay_get_overlay_layer(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_OVERLAY_LAYER_H */
