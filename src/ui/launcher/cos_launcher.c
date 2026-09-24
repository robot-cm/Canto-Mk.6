/**
 * @file cos_launcher.c
 * @brief Launcher dispatcher.
 *
 * The v2 (Honeycomb) launcher has been removed and the v1 framework Home is
 * kept hidden as a fallback. The app-list page itself is served by the
 * built-in bubble_grid (COS_USE_CUSTOM_LAUNCHER is undefined in the simulator
 * config), so this dispatcher is normally never reached at runtime.
 */
#include "ui/launcher/cos_launcher.h"

#include "ui/launcher/cos_launcher_v1.h"

void cos_launcher_enter(void)
{
    /* v2 deleted; v1 retained but hidden. The real app list is bubble_grid. */
    cos_launcher_v1_enter();
}
