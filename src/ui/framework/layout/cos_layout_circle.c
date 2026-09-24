/**
 * @file cos_layout_circle.c
 * @brief Polar / arc layout strategy for round displays.
 */
#include "cos_layout_circle.h"
#include "cos_mem.h"

static void _circle_layout(cos_layout_manager_t *self,
                           const cos_display_profile_t *p,
                           cos_widget_t *widgets, int count)
{
    /* Default fallback: distribute widgets evenly on a ring (top-first). */
    float ring_r = p->safe_radius * 0.60f;
    for (int i = 0; i < count; i++) {
        float a = (count > 1)
                ? (360.0f * (float)i / (float)count - 90.0f)
                : -90.0f;
        cos_layout_circle_place(p, &widgets[i], a, ring_r);
    }
}

static void _circle_destroy(cos_layout_manager_t *self)
{
    cos_free(self);
}

cos_layout_manager_t *cos_layout_circle_create(void)
{
    cos_layout_manager_t *m =
        (cos_layout_manager_t *)cos_malloc(sizeof(cos_layout_manager_t));
    m->shape = COS_DISPLAY_SHAPE_CIRCLE;
    m->layout = _circle_layout;
    m->destroy = _circle_destroy;
    return m;
}

void cos_layout_circle_place(const cos_display_profile_t *p,
                             cos_widget_t *w,
                             float angle_deg, float radius_dp)
{
    w->w = cos_dp(p, w->w_dp);
    w->h = cos_dp(p, w->h_dp);
    float radius_px = cos_dp(p, radius_dp);
    float cx = 0.0f, cy = 0.0f;
    cos_display_profile_polar(p, angle_deg, radius_px, &cx, &cy);
    float r = (w->w > w->h ? w->w : w->h) * 0.5f;
    cos_display_profile_clamp_inside(p, &cx, &cy, r);
    w->x = cx - w->w * 0.5f;
    w->y = cy - w->h * 0.5f;
    w->scale = 1.0f;
    w->opacity = 1.0f;
    w->visible = true;
}
