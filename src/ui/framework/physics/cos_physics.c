/**
 * @file cos_physics.c
 * @brief Inertial scroller implementation (friction + spring + bounce).
 */
#include "cos_physics.h"
#include <math.h>

void cos_scroller_init(cos_scroller_t *s, float min, float max)
{
    s->pos = min;
    s->vel = 0.0f;
    s->min = min;
    s->max = max;
    s->decay = 4.0f;
    s->spring_k = 140.0f;
    s->spring_c = 20.0f;
    s->dragging = false;
}

void cos_scroller_set_params(cos_scroller_t *s, float decay,
                             float spring_k, float spring_c)
{
    s->decay = decay;
    s->spring_k = spring_k;
    s->spring_c = spring_c;
}

void cos_scroller_drag(cos_scroller_t *s, float delta, float dt)
{
    s->dragging = true;
    s->pos += delta;
    s->vel = (dt > 1e-4f) ? (delta / dt) : 0.0f;
}

void cos_scroller_release(cos_scroller_t *s)
{
    s->dragging = false;
    if (s->vel > 4000.0f)  s->vel = 4000.0f;
    if (s->vel < -4000.0f) s->vel = -4000.0f;
}

void cos_scroller_step(cos_scroller_t *s, float dt)
{
    if (s->dragging || dt <= 0.0f) return;

    if (s->pos < s->min || s->pos > s->max) {
        /* Beyond a bound: spring + damping pull it back (bounce). */
        float bound = (s->pos < s->min) ? s->min : s->max;
        float accel = -s->spring_k * (s->pos - bound) - s->spring_c * s->vel;
        s->vel += accel * dt;
        s->pos += s->vel * dt;
        /* Hard safety against runaway overshoot (never explode). */
        if (s->pos < s->min - 60.0f) { s->pos = s->min - 60.0f; s->vel *= -0.3f; }
        if (s->pos > s->max + 60.0f) { s->pos = s->max + 60.0f; s->vel *= -0.3f; }
    } else {
        /* Inside bounds: friction decay. */
        s->vel *= expf(-s->decay * dt);
        s->pos += s->vel * dt;
        if (s->pos < s->min) { s->pos = s->min; if (s->vel < 0.0f) s->vel = 0.0f; }
        if (s->pos > s->max) { s->pos = s->max; if (s->vel > 0.0f) s->vel = 0.0f; }
        if (fabsf(s->vel) < 1.0f) s->vel = 0.0f;
    }
}

bool cos_scroller_is_resting(const cos_scroller_t *s)
{
    if (s->dragging) return false;
    return (fabsf(s->vel) < 0.5f) && (s->pos >= s->min) && (s->pos <= s->max);
}
