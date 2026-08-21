/**
 * @file eos_ui_anim.c
 * @brief Easing math + LVGL page transition bindings.
 */
#include "eos_ui_anim.h"
#include "lvgl.h"
#include <math.h>

static float clamp01(float t)
{
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t;
}

float eos_ease_linear(float t)        { return clamp01(t); }

float eos_ease_in_out_cubic(float t)
{
    t = clamp01(t);
    return (t < 0.5f)
         ? (4.0f * t * t * t)
         : (1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f);
}

float eos_ease_out_cubic(float t)
{
    t = clamp01(t);
    return 1.0f - powf(1.0f - t, 3.0f);
}

float eos_ease_out_back(float t)
{
    t = clamp01(t);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

float eos_ease_out_bounce(float t)
{
    t = clamp01(t);
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    if (t < 1.0f / d1)        return n1 * t * t;
    else if (t < 2.0f / d1) { t -= 1.5f / d1;  return n1 * t * t + 0.75f; }
    else if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    else                     { t -= 2.625f / d1; return n1 * t * t + 0.984375f; }
}

float eos_ease_map(float from, float to, float t, float (*ease)(float))
{
    return from + (to - from) * ease(clamp01(t));
}

/* ---- LVGL bindings ---- */
static void _op_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void _scale_cb(void *var, int32_t v)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)var, v, 0);
}

void eos_anim_page_close(void *old_obj, unsigned int duration_ms)
{
    if (!old_obj) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, old_obj);
    lv_anim_set_duration(&a, duration_ms);
    lv_anim_set_exec_cb(&a, _op_cb);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    /* Mirror the page-open spec: close also scales 1.0 -> 0.8 (256 -> 205). */
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, old_obj);
    lv_anim_set_duration(&b, duration_ms);
    lv_anim_set_exec_cb(&b, _scale_cb);
    lv_anim_set_values(&b, 256, 205);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_in_out);
    lv_anim_start(&b);
}

void eos_anim_add_fade_scale(lv_anim_timeline_t *at, void *obj,
                             int from_opa, int to_opa,
                             int from_scale, int to_scale,
                             unsigned int delay_ms, unsigned int duration_ms)
{
    if (!at || !obj) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_duration(&a, duration_ms);
    lv_anim_set_exec_cb(&a, _op_cb);
    lv_anim_set_values(&a, from_opa, to_opa);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_timeline_add(at, delay_ms, &a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, obj);
    lv_anim_set_duration(&b, duration_ms);
    lv_anim_set_exec_cb(&b, _scale_cb);
    lv_anim_set_values(&b, from_scale, to_scale);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_in_out);
    lv_anim_timeline_add(at, delay_ms, &b);
}

void eos_anim_page_open(void *new_obj, unsigned int duration_ms)
{
    if (!new_obj) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, new_obj);
    lv_anim_set_duration(&a, duration_ms);
    lv_anim_set_exec_cb(&a, _op_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, new_obj);
    lv_anim_set_duration(&b, duration_ms);
    lv_anim_set_exec_cb(&b, _scale_cb);
    lv_anim_set_values(&b, 205, 256);   /* 0.8 -> 1.0 (256 == 100%) */
    lv_anim_set_path_cb(&b, lv_anim_path_overshoot);
    lv_anim_start(&b);
}
