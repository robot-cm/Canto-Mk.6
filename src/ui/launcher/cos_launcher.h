/**
 * @file cos_launcher.h
 * @brief Canto Mk.6 adaptive Launcher (root activity)
 *
 * The Launcher is now a framework-backed adaptive Home: it renders its app
 * list through the UI framework (ArcList on round, LayoutManager grid on
 * square/rectangle) using the real Plugin Manager registry. Shape-agnostic.
 */
#ifndef COS_LAUNCHER_H
#define COS_LAUNCHER_H

#include "cos_activity.h"
#include "ui/framework/display/cos_display_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration — keep this header light; the full cos_framework_home_t
 * definition lives in the UI framework header (included by cos_launcher.c).
 * NOTE: cos_display_profile_t is an anonymous-struct typedef, so it cannot be
 * forward-declared; the include above supplies its real definition. */
typedef struct cos_framework_home cos_framework_home_t;

/**
 * @brief Enter the adaptive Launcher as a SEPARATE page (not the root).
 *        The watchface (built-in clock) is the home/root activity; this is
 *        pushed on top and can be popped back to the watchface. Round screens
 *        use the Huawei-style ArcList; square/rectangle use the LayoutManager
 *        grid or the Apple-style bubble grid (per settings).
 */
void cos_launcher_enter(void);

/**
 * @brief Enter the v1 (legacy) Launcher implementation. Reached only when
 *        launcher_mode == "v1". Hidden by default; toggled via the shell
 *        `launcher mode v1|v2` command.
 */
void cos_launcher_v1_enter(void);

/**
 * @brief Build the adaptive Home (Liquid Glass status bar + ArcList /
 *        LayoutManager grid) from the real Plugin Manager registry into
 *        `parent`. Returns the number of apps shown and stores the created
 *        home in *out_home. on_select is wired to launch the tapped app.
 *        Shape-agnostic: pass whichever display profile the target screen uses.
 */
int cos_launcher_build_home(lv_obj_t *parent, const cos_display_profile_t *p,
                            cos_framework_home_t **out_home);

/**
 * @brief Live-rebuild the Home from the current Plugin Manager registry
 *        (called on app install/uninstall). No-op if the Home is not built.
 */
void cos_launcher_rebuild(void);

/**
 * @brief Return the live Home handle (NULL if not built). Used by tests.
 */
cos_framework_home_t *cos_launcher_get_home(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_LAUNCHER_H */
