/**
 * @file eos_wos.h
 * @brief Canto Mk.6 Watch OS UI framework — master header
 *
 * One include pulls the whole framework. See WatchOS_UI框架设计.md for the
 * architecture (layers / App Manager / lifecycle / layout / animation rules).
 */
#ifndef EOS_WOS_H
#define EOS_WOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "core/eos_wos_theme.h"
#include "core/eos_wos_layout.h"
#include "core/eos_wos_app.h"
#include "manager/eos_wos_transition.h"
#include "manager/eos_wos_app_manager.h"
#include "system/eos_wos_statusbar.h"
#include "system/eos_wos_notification.h"

/**
 * @brief Initialize the WOS framework (status bar, app manager, system layers)
 * @note Call once after eos_overlay_layer_init() during system boot.
 */
void eos_wos_framework_init(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_WOS_H */
