/**
 * @file eos_launcher.c
 * @brief Launcher dispatcher.
 *
 * The v2 (Honeycomb) launcher has been removed and the v1 framework Home is
 * kept hidden as a fallback. The app-list page itself is served by the
 * built-in bubble_grid (EOS_USE_CUSTOM_LAUNCHER is undefined in the simulator
 * config), so this dispatcher is normally never reached at runtime.
 */
#include "ui/launcher/eos_launcher.h"

#include "ui/launcher/eos_launcher_v1.h"

void eos_launcher_enter(void)
{
    /* v2 deleted; v1 retained but hidden. The real app list is bubble_grid. */
    eos_launcher_v1_enter();
}
