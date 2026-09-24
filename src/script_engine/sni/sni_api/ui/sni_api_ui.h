/**
 * @file sni_api_ui.h
 * @brief JerryScript bridge for the CantoMk6 adaptive UI framework.
 *
 * Exposes the P8 rendering layer (DisplayProfile, safe-area, Liquid Glass,
 * ArcList/LayoutManager Home) to JavaScript as `cos.ui.*`, so a UI written
 * entirely in JS becomes screen-shape adaptive automatically — exactly the
 * same code path the native Launcher uses.
 */

#ifndef SNI_API_UI_H
#define SNI_API_UI_H

#include "jerryscript.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Build the static `ui` API object (called once from sni_init). */
void sni_api_ui_init(void);

/** @brief Mount `ui` under the `cos` namespace (called from sni_mount). */
void sni_api_ui_mount(jerry_value_t realm);

#ifdef __cplusplus
}
#endif
#endif /* SNI_API_UI_H */
