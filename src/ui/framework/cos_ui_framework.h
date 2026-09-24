/**
 * @file eos_ui_framework.h
 * @brief Umbrella header for the Canto Mk.6 adaptive UI Framework.
 *
 * One include pulls in the whole stack: Display Adaptation, Layout Engine,
 * Physics, Animation, ArcList, Personalization and Liquid Glass. Every App
 * (watch / weather / music / alarm / settings / games) builds on this single
 * adaptive system instead of hard-coding coordinates or screen shapes.
 */
#ifndef EOS_UI_FRAMEWORK_H
#define EOS_UI_FRAMEWORK_H

#include "eos_display_profile.h"
#include "eos_display_profiles.h"
#include "eos_layout.h"
#include "eos_layout_circle.h"
#include "eos_layout_square.h"
#include "eos_physics.h"
#include "eos_ui_anim.h"
#include "eos_arclist.h"
#include "eos_app_manager.h"
#include "eos_layout_storage.h"
#include "eos_theme_manager.h"
#include "eos_liquid_glass.h"

/* P8 rendering layer: framework -> real LVGL objects. */
#include "eos_arclist_view.h"
#include "eos_layout_view.h"
#include "eos_framework_home.h"

#endif /* EOS_UI_FRAMEWORK_H */
