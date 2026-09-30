/**
 * @file eos_liquid_glass.h
 * @brief Simplified "Liquid Glass" visual style (req #6).
 *
 * Avoids real-time Gaussian blur / heavy transparency / particles. Uses
 * semi-transparent backgrounds, a highlight edge, a soft shadow (subtle glow)
 * and rounded corners — the watchOS/visionOS look at ESP32 cost.
 */
#ifndef EOS_LIQUID_GLASS_H
#define EOS_LIQUID_GLASS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Apply a glass card look (rounded, translucent, glowing edge). */
void eos_liquid_glass_card(void *obj);

/** @brief Apply a softer glass panel background. */
void eos_liquid_glass_panel(void *obj);

/** @brief Subtle glass: transparent rounded corners + thin highlight edge only. */
void eos_liquid_glass_card_subtle(void *obj);

/** @brief Clean solid app-icon tile (no glass) — used by the Launcher. */
void eos_app_icon_solid(void *obj);

#ifdef __cplusplus
}
#endif
#endif /* EOS_LIQUID_GLASS_H */
