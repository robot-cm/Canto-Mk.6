/**
 * @file eos_launcher_v1.h
 * @brief v1 (legacy) Launcher — hidden behind the launcher_mode toggle.
 *
 * This is the previous framework Home (ArcList on round, LayoutManager grid /
 * bubble on square). It is no longer the default; `eos_launcher_enter()` only
 * routes here when launcher_mode == "v1". Kept, not deleted.
 */
#ifndef EOS_LAUNCHER_V1_H
#define EOS_LAUNCHER_V1_H

#include "eos_activity.h"

#ifdef __cplusplus
extern "C" {
#endif

void eos_launcher_v1_enter(void);

#ifdef __cplusplus
}
#endif
#endif /* EOS_LAUNCHER_V1_H */
