/**
 * @file cos_ui_framework.h
 * @brief Umbrella header for the Canto Mk.6 adaptive UI Framework.
 *
 * One include pulls in the whole stack: Display Adaptation, Layout Engine,
 * Physics, Animation, ArcList, Personalization and Liquid Glass. Every App
 * (watch / weather / music / alarm / settings / games) builds on this single
 * adaptive system instead of hard-coding coordinates or screen shapes.
 */
#ifndef COS_UI_FRAMEWORK_H
#define COS_UI_FRAMEWORK_H

#include "cos_display_profile.h"
#include "cos_display_profiles.h"
#include "cos_layout.h"
#include "cos_layout_circle.h"
#include "cos_layout_square.h"
#include "cos_physics.h"
#include "cos_ui_anim.h"
#include "cos_arclist.h"
#include "cos_app_manager.h"
#include "cos_layout_storage.h"
#include "cos_theme_manager.h"
#include "cos_liquid_glass.h"

/* P8 rendering layer: framework -> real LVGL objects. */
#include "cos_arclist_view.h"
#include "cos_layout_view.h"
#include "cos_framework_home.h"

#endif /* COS_UI_FRAMEWORK_H */
