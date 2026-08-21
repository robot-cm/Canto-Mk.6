/**
 * @file eos_layout.c
 * @brief LayoutManager base, factory and anchor helpers.
 */
#include "eos_layout.h"
#include "eos_layout_circle.h"
#include "eos_layout_square.h"

void eos_layout_anchor_point(const eos_display_profile_t *p, eos_anchor_t a,
                             float *ax, float *ay)
{
    float left, top, right, bottom;
    if (p->shape == EOS_DISPLAY_SHAPE_CIRCLE) {
        left   = p->center_x - p->safe_radius;
        top    = p->center_y - p->safe_radius;
        right  = p->center_x + p->safe_radius;
        bottom = p->center_y + p->safe_radius;
    } else {
        left   = p->safe_x;
        top    = p->safe_y;
        right  = p->safe_x + p->safe_w;
        bottom = p->safe_y + p->safe_h;
    }
    float cx = (left + right) * 0.5f;
    float cy = (top + bottom) * 0.5f;

    switch (a) {
        case EOS_ANCHOR_TOP_LEFT:      *ax = left;  *ay = top;    break;
        case EOS_ANCHOR_TOP_CENTER:    *ax = cx;    *ay = top;    break;
        case EOS_ANCHOR_TOP_RIGHT:     *ax = right; *ay = top;    break;
        case EOS_ANCHOR_CENTER_LEFT:   *ax = left;  *ay = cy;     break;
        case EOS_ANCHOR_CENTER:        *ax = cx;    *ay = cy;     break;
        case EOS_ANCHOR_CENTER_RIGHT:  *ax = right; *ay = cy;     break;
        case EOS_ANCHOR_BOTTOM_LEFT:   *ax = left;  *ay = bottom; break;
        case EOS_ANCHOR_BOTTOM_CENTER: *ax = cx;    *ay = bottom; break;
        case EOS_ANCHOR_BOTTOM_RIGHT:  *ax = right; *ay = bottom; break;
        default:                       *ax = cx;    *ay = cy;     break;
    }
}

void eos_layout_apply_margin(float *x, float *y, eos_anchor_t a,
                             float w, float h, float margin)
{
    switch (a) {
        case EOS_ANCHOR_TOP_LEFT:      *x += margin;       *y += margin;        break;
        case EOS_ANCHOR_TOP_CENTER:    *x -= w * 0.5f;     *y += margin;        break;
        case EOS_ANCHOR_TOP_RIGHT:     *x -= w + margin;   *y += margin;        break;
        case EOS_ANCHOR_CENTER_LEFT:   *x += margin;       *y -= h * 0.5f;      break;
        case EOS_ANCHOR_CENTER:        *x -= w * 0.5f;     *y -= h * 0.5f;      break;
        case EOS_ANCHOR_CENTER_RIGHT:  *x -= w + margin;   *y -= h * 0.5f;      break;
        case EOS_ANCHOR_BOTTOM_LEFT:   *x += margin;       *y -= h + margin;    break;
        case EOS_ANCHOR_BOTTOM_CENTER: *x -= w * 0.5f;     *y -= h + margin;    break;
        case EOS_ANCHOR_BOTTOM_RIGHT:  *x -= w + margin;   *y -= h + margin;    break;
    }
}

void eos_layout_place_anchored(const eos_display_profile_t *p, eos_widget_t *w)
{
    float ax = 0.0f, ay = 0.0f;
    eos_layout_anchor_point(p, w->anchor, &ax, &ay);
    w->w = eos_dp(p, w->w_dp);
    w->h = eos_dp(p, w->h_dp);
    float x = ax, y = ay;
    eos_layout_apply_margin(&x, &y, w->anchor, w->w, w->h, eos_dp(p, w->margin_dp));
    /* x,y is top-left; clamp the widget center (with its bounding radius) so the
     * whole box stays inside the safe area — never clip or hide content. */
    float cx = x + w->w * 0.5f;
    float cy = y + w->h * 0.5f;
    float r = (w->w > w->h ? w->w : w->h) * 0.5f;
    eos_display_profile_clamp_inside(p, &cx, &cy, r);
    w->x = cx - w->w * 0.5f;
    w->y = cy - w->h * 0.5f;
    w->scale = 1.0f;
    w->opacity = 1.0f;
    w->visible = true;
}

eos_layout_manager_t *eos_layout_manager_create(const eos_display_profile_t *p)
{
    if (p->shape == EOS_DISPLAY_SHAPE_CIRCLE) {
        return eos_layout_circle_create();
    }
    return eos_layout_square_create();
}
