/**
 * @file eos_layout_circle.h
 * @brief Polar / arc layout strategy for round displays.
 */
#ifndef EOS_LAYOUT_CIRCLE_H
#define EOS_LAYOUT_CIRCLE_H

#include "eos_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Create the circle (polar) LayoutManager. */
eos_layout_manager_t *eos_layout_circle_create(void);

/**
 * @brief Place one widget centered at polar (angle_deg, radius_dp).
 * Used both by the default ring layout and by the ArcList widget.
 */
void eos_layout_circle_place(const eos_display_profile_t *p,
                             eos_widget_t *w,
                             float angle_deg, float radius_dp);

#ifdef __cplusplus
}
#endif
#endif /* EOS_LAYOUT_CIRCLE_H */
