/**
 * @file eos_arclist.c
 * @brief Huawei-Watch style arc app list geometry + physics.
 */
#include "eos_arclist.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void eos_arclist_init(eos_arclist_t *al, const eos_display_profile_t *p, int count)
{
    al->profile = p;
    al->count = (count > EOS_ARCLIST_MAX) ? EOS_ARCLIST_MAX : count;
    al->scroll = 0.0f;
    al->arc_radius_factor = 0.90f;
    al->step_deg = 22.0f;
    al->focus_scale = 1.3f;
    al->min_scale = 0.45f;
    al->visible_deg = 75.0f;
    al->item_radius = 22.0f * p->dp_scale;   /* keep legacy containment default */

    float max_scroll = (al->count > 1)
                     ? (float)(al->count - 1) * al->step_deg
                     : 0.0f;
    eos_scroller_init(&al->scroller, 0.0f, max_scroll);
    eos_arclist_layout(al);
}

void eos_arclist_layout(eos_arclist_t *al)
{
    const eos_display_profile_t *p = al->profile;
    float R = p->safe_radius * al->arc_radius_factor;
    float scale_slope = (al->focus_scale - al->min_scale) / al->visible_deg;
    float opa_slope   = (1.0f - 0.15f) / al->visible_deg;

    for (int i = 0; i < al->count; i++) {
        float a_deg = (float)i * al->step_deg - al->scroll;
        float a = a_deg * (float)M_PI / 180.0f;

        /* Coverflow-on-arc: focus at center, neighbours arc away. */
        float x = p->center_x + R * sinf(a);
        float y = p->center_y - R * (1.0f - cosf(a));

        float ad = fabsf(a_deg);
        float scale = al->focus_scale - scale_slope * ad;
        if (scale < al->min_scale) scale = al->min_scale;
        float op = 1.0f - opa_slope * ad;
        if (op < 0.15f) op = 0.15f;
        bool vis = (ad <= al->visible_deg);

        eos_arclist_item_t *it = &al->items[i];
        it->index = i;
        it->x = x;
        it->y = y;
        it->scale = scale;
        it->opacity = op;
        it->visible = vis;

        /* Guarantee no off-screen / clipped content: pull back inside safe area. */
        float r = al->item_radius * scale;
        eos_display_profile_clamp_inside(p, &it->x, &it->y, r);
    }
}

void eos_arclist_drag(eos_arclist_t *al, float dy_px, float dt)
{
    float deg_per_px = al->step_deg / (al->profile->safe_radius * al->arc_radius_factor);
    float delta_deg = -dy_px * deg_per_px;   /* drag up -> advance to next app */
    eos_scroller_drag(&al->scroller, delta_deg, dt);
    al->scroll = al->scroller.pos;
    eos_arclist_layout(al);
}

void eos_arclist_release(eos_arclist_t *al)
{
    eos_scroller_release(&al->scroller);
}

void eos_arclist_step(eos_arclist_t *al, float dt)
{
    eos_scroller_step(&al->scroller, dt);
    al->scroll = al->scroller.pos;
    eos_arclist_layout(al);
}

void eos_arclist_set_item_radius(eos_arclist_t *al, float radius_px)
{
    if (!al) return;
    al->item_radius = (radius_px > 0.0f) ? radius_px : 22.0f;
    /* Re-run the layout with the new clamp radius so the resolved geometry
     * (and the safe-area clamp) reflects the real card size immediately —
     * otherwise a freshly created view sits at rest with the default-radius
     * clamp and a later assertion using the true card radius fails. */
    if (al->profile)
        eos_arclist_layout(al);
}

int eos_arclist_focus_index(const eos_arclist_t *al)
{
    int best = 0;
    float bestd = 1e9f;
    for (int i = 0; i < al->count; i++) {
        float a = (float)i * al->step_deg - al->scroll;
        float d = fabsf(a);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}
