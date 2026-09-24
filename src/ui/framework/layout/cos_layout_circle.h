/**
 * @file cos_layout_circle.h
 * @brief Polar / arc layout strategy for round displays.
 */
#ifndef COS_LAYOUT_CIRCLE_H
#define COS_LAYOUT_CIRCLE_H

#include "cos_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Create the circle (polar) LayoutManager. */
cos_layout_manager_t *cos_layout_circle_create(void);

/**
 * @brief Place one widget centered at polar (angle_deg, radius_dp).
 * Used both by the default ring layout and by the ArcList widget.
 */
void cos_layout_circle_place(const cos_display_profile_t *p,
                             cos_widget_t *w,
                             float angle_deg, float radius_dp);

#ifdef __cplusplus
}
#endif
#endif /* COS_LAYOUT_CIRCLE_H */
