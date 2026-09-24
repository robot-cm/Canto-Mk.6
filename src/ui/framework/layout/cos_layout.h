/**
 * @file eos_layout.h
 * @brief Layout Engine — shape-agnostic, anchor/constraint based placement.
 *
 * Apps declare widgets with an anchor + margin (in dp) and a size (in dp).
 * They NEVER store fixed pixel coordinates. The active LayoutManager resolves
 * everything to px at layout time, choosing polar (circle) or grid (square/
 * rectangle) strategies automatically from the DisplayProfile.
 */
#ifndef EOS_LAYOUT_H
#define EOS_LAYOUT_H

#include "eos_display_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Where a widget attaches within the safe area. */
typedef enum {
    EOS_ANCHOR_TOP_LEFT = 0,
    EOS_ANCHOR_TOP_CENTER,
    EOS_ANCHOR_TOP_RIGHT,
    EOS_ANCHOR_CENTER_LEFT,
    EOS_ANCHOR_CENTER,
    EOS_ANCHOR_CENTER_RIGHT,
    EOS_ANCHOR_BOTTOM_LEFT,
    EOS_ANCHOR_BOTTOM_CENTER,
    EOS_ANCHOR_BOTTOM_RIGHT,
} eos_anchor_t;

/** @brief Common widget kinds shared by every screen shape. */
typedef enum {
    EOS_WIDGET_BUTTON,
    EOS_WIDGET_ICON,
    EOS_WIDGET_LABEL,
    EOS_WIDGET_CARD,
} eos_widget_kind_t;

/**
 * @brief Layout-agnostic widget description.
 *
 * All input fields are logical (dp / anchor). The px fields (x,y,w,h,scale,
 * opacity,visible) are filled in by the LayoutManager at layout time.
 */
typedef struct {
    eos_widget_kind_t kind;
    const char *id;
    float w_dp;            /**< logical width  */
    float h_dp;            /**< logical height */
    eos_anchor_t anchor;   /**< attachment point in safe area */
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
} eos_widget_t;

/** @brief LayoutManager base (vtable). */
typedef struct eos_layout_manager eos_layout_manager_t;
struct eos_layout_manager {
    eos_display_shape_t shape;
    void (*layout)(eos_layout_manager_t *self,
                   const eos_display_profile_t *p,
                   eos_widget_t *widgets, int count);
    void (*destroy)(eos_layout_manager_t *self);
};

/** @brief Pick the correct manager for the profile's shape. */
eos_layout_manager_t *eos_layout_manager_create(const eos_display_profile_t *p);

/** @brief Resolve the anchor reference point (px) for a profile. */
void eos_layout_anchor_point(const eos_display_profile_t *p, eos_anchor_t a,
                             float *ax, float *ay);

/**
 * @brief Convert an anchor reference point into a widget top-left, applying
 * the margin (push the widget inward from its anchor edge) and size.
 */
void eos_layout_apply_margin(float *x, float *y, eos_anchor_t a,
                             float w, float h, float margin);

/** @brief Convenience: lay out one anchored widget into px (square/rect path). */
void eos_layout_place_anchored(const eos_display_profile_t *p, eos_widget_t *w);

#ifdef __cplusplus
}
#endif
#endif /* EOS_LAYOUT_H */
