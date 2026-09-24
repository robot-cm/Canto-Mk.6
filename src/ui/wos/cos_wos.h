/**
 * @file cos_wos.h
 * @brief Canto Mk.6 Watch OS UI framework — master header
 *
 * One include pulls the whole framework. See WatchOS_UI框架设计.md for the
 * architecture (layers / App Manager / lifecycle / layout / animation rules).
 */
#ifndef COS_WOS_H
#define COS_WOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "core/cos_wos_theme.h"
#include "core/cos_wos_layout.h"
#include "core/cos_wos_app.h"
#include "manager/cos_wos_transition.h"
#include "manager/cos_wos_app_manager.h"
#include "system/cos_wos_statusbar.h"
#include "system/cos_wos_notification.h"

/**
 * @brief Initialize the WOS framework (status bar, app manager, system layers)
 * @note Call once after cos_overlay_layer_init() during system boot.
 */
void cos_wos_framework_init(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_WOS_H */
