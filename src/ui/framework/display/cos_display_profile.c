/**
 * @file cos_display_profile.c
 * @brief Implementation of the Display Adaptation Layer.
 */
#include "cos_display_profile.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

cos_display_profile_t cos_display_profile_create(cos_display_shape_t shape, int w, int h)
{
    cos_display_profile_t p;
    p.shape = shape;
    p.width = w;
    p.height = h;
    p.center_x = (float)w * 0.5f;
    p.center_y = (float)h * 0.5f;

    float half_min = (float)(w < h ? w : h) * 0.5f;
    p.radius = half_min;
    p.aspect_ratio = (h != 0) ? ((float)w / (float)h) : 1.0f;
    p.dp_scale = (float)w / COS_UI_REF_WIDTH;

    if (shape == COS_DISPLAY_SHAPE_CIRCLE) {
        /* Usable inscribed circle leaves an 8% margin (no rectangular clipping). */
        p.safe_radius = half_min * 0.92f;
        p.safe_x = p.center_x - p.safe_radius;
        p.safe_y = p.center_y - p.safe_radius;
        p.safe_w = p.safe_radius * 2.0f;
        p.safe_h = p.safe_radius * 2.0f;
    } else {
        /* SQUARE / RECTANGLE: inset padding = 6% of the smaller dimension. */
        float pad = half_min * 0.12f;
        p.safe_radius = 0.0f;
        p.safe_x = pad;
        p.safe_y = pad;
        p.safe_w = (float)w - 2.0f * pad;
        p.safe_h = (float)h - 2.0f * pad;
    }
    return p;
}

float cos_dp(const cos_display_profile_t *p, float dp)
{
    return dp * p->dp_scale;
}

void cos_display_profile_polar(const cos_display_profile_t *p,
                               float angle_deg, float radius_px,
                               float *x, float *y)
{
    float a = angle_deg * (float)M_PI / 180.0f;
    *x = p->center_x + radius_px * cosf(a);
    *y = p->center_y + radius_px * sinf(a);
}

bool cos_display_profile_is_inside(const cos_display_profile_t *p,
                                   float x, float y, float r)
{
    if (p->shape == COS_DISPLAY_SHAPE_CIRCLE) {
        float dx = x - p->center_x;
        float dy = y - p->center_y;
        return (sqrtf(dx * dx + dy * dy) + r) <= p->safe_radius;
    }
    return (x - r) >= p->safe_x && (x + r) <= (p->safe_x + p->safe_w) &&
           (y - r) >= p->safe_y && (y + r) <= (p->safe_y + p->safe_h);
}

bool cos_display_profile_clamp_inside(const cos_display_profile_t *p,
                                      float *x, float *y, float r)
{
    if (cos_display_profile_is_inside(p, *x, *y, r)) {
        return false;
    }
    if (p->shape == COS_DISPLAY_SHAPE_CIRCLE) {
        float dx = *x - p->center_x;
        float dy = *y - p->center_y;
        float d = sqrtf(dx * dx + dy * dy);
        /* Clamp to (safe_radius - r) minus a sub-pixel epsilon so the result is
         * guaranteed strictly inside the safe area, not grazing the boundary
         * (float reconstruction of dist can land a hair over max_r). */
        float max_r = p->safe_radius - r - 0.5f;
        if (max_r < 0.0f) max_r = 0.0f;
        if (d < 1e-4f) {
            *x = p->center_x;
            *y = p->center_y - max_r;   /* push straight up from center */
        } else {
            float s = max_r / d;
            *x = p->center_x + dx * s;
            *y = p->center_y + dy * s;
        }
        return true;
    } else {
        /* Reserve a 0.5px epsilon so clamped points are strictly inside. */
        float min_x = p->safe_x + r + 0.5f;
        float max_x = p->safe_x + p->safe_w - r - 0.5f;
        float min_y = p->safe_y + r + 0.5f;
        float max_y = p->safe_y + p->safe_h - r - 0.5f;
        if (min_x > max_x) min_x = max_x = p->safe_x + p->safe_w * 0.5f;
        if (min_y > max_y) min_y = max_y = p->safe_y + p->safe_h * 0.5f;
        if (*x < min_x) *x = min_x; else if (*x > max_x) *x = max_x;
        if (*y < min_y) *y = min_y; else if (*y > max_y) *y = max_y;
        return true;
    }
}
