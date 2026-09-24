/**
 * @file cos_layout.h
 * @brief Layout Engine — shape-agnostic, anchor/constraint based placement.
 *
 * Apps declare widgets with an anchor + margin (in dp) and a size (in dp).
 * They NEVER store fixed pixel coordinates. The active LayoutManager resolves
 * everything to px at layout time, choosing polar (circle) or grid (square/
 * rectangle) strategies automatically from the DisplayProfile.
 */
#ifndef COS_LAYOUT_H
#define COS_LAYOUT_H

#include "cos_display_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Where a widget attaches within the safe area. */
typedef enum {
    COS_ANCHOR_TOP_LEFT = 0,
    COS_ANCHOR_TOP_CENTER,
    COS_ANCHOR_TOP_RIGHT,
    COS_ANCHOR_CENTER_LEFT,
    COS_ANCHOR_CENTER,
    COS_ANCHOR_CENTER_RIGHT,
    COS_ANCHOR_BOTTOM_LEFT,
    COS_ANCHOR_BOTTOM_CENTER,
    COS_ANCHOR_BOTTOM_RIGHT,
} cos_anchor_t;

/** @brief Common widget kinds shared by every screen shape. */
typedef enum {
    COS_WIDGET_BUTTON,
    COS_WIDGET_ICON,
    COS_WIDGET_LABEL,
    COS_WIDGET_CARD,
} cos_widget_kind_t;

/**
 * @brief Layout-agnostic widget description.
 *
 * All input fields are logical (dp / anchor). The px fields (x,y,w,h,scale,
 * opacity,visible) are filled in by the LayoutManager at layout time.
 */
typedef struct {
    cos_widget_kind_t kind;
    const char *id;
    float w_dp;            /**< logical width  */
    float h_dp;            /**< logical height */
    cos_anchor_t anchor;   /**< attachment point in safe area */
    float margin_dp;       /**< gap from the anchor edge (dp) */
    int   grid_col;        /**< SQUARE/RECT grid column (0-based) */
    int   grid_row;        /**< SQUARE/RECT grid row (0-based) */
    int   grid_span;       /**< columns spanned (default 1) */
    /* ---- resolved at layout time (px) ---- */
    float x;               /**< top-left x */
    float y;               /**< top-left y */
    float w;
    float h;
    float scale;           /**< visual scale factor (1.0 default) */
    float opacity;         /**< 0..1 */
    bool  visible;
} cos_widget_t;

/** @brief LayoutManager base (vtable). */
typedef struct cos_layout_manager cos_layout_manager_t;
struct cos_layout_manager {
    cos_display_shape_t shape;
    void (*layout)(cos_layout_manager_t *self,
                   const cos_display_profile_t *p,
                   cos_widget_t *widgets, int count);
    void (*destroy)(cos_layout_manager_t *self);
};

/** @brief Pick the correct manager for the profile's shape. */
cos_layout_manager_t *cos_layout_manager_create(const cos_display_profile_t *p);

/** @brief Resolve the anchor reference point (px) for a profile. */
void cos_layout_anchor_point(const cos_display_profile_t *p, cos_anchor_t a,
                             float *ax, float *ay);

/**
 * @brief Convert an anchor reference point into a widget top-left, applying
 * the margin (push the widget inward from its anchor edge) and size.
 */
void cos_layout_apply_margin(float *x, float *y, cos_anchor_t a,
                             float w, float h, float margin);

/** @brief Convenience: lay out one anchored widget into px (square/rect path). */
void cos_layout_place_anchored(const cos_display_profile_t *p, cos_widget_t *w);

#ifdef __cplusplus
}
#endif
#endif /* COS_LAYOUT_H */
