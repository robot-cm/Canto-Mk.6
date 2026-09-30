/**
 * @file eos_eqsolver.h
 * @brief Equation Solver - native C app for Canto Mk.6 (com.cantomk6.eqsolver)
 *
 * Solves systems of linear / non-linear equations entered by the user on the
 * round 240x240 touch screen. Fully self-contained: it does not touch Core,
 * any other app, or the startup flow. It is registered through the existing
 * native-app registry (eos_app_list.{h,c}) exactly like Album/Texthub.
 *
 * Removability: comment out EOS_APP_EQSOLVER_ENABLED below (and the matching
 * #ifdef guards in eos_app_list.c) to compile the whole app out.
 */

#ifndef EOS_EQSOLVER_H
#define EOS_EQSOLVER_H

#include "eos_activity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Master switch: comment out to remove the app from the firmware entirely. */
#define EOS_APP_EQSOLVER_ENABLED

/* App icon (must be deployed to this sdcard path, same scheme as other apps). */
#define EOS_IMG_EQSOLVER "/sdcard/theme/icons/eqsolver.bin"

/* Native app entry point, invoked by the App List registry (no args, like
 * Album/Texthub/Settings: the function creates and enters its own activity). */
void eos_eqsolver_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_EQSOLVER_H */
