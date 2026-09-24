/**
 * @file cos_wos_transition.c
 * @brief WOS page transitions implementation
 */
#include "cos_wos_transition.h"

#include "cos_log.h"

#define COS_LOG_TAG "WosTrans"

#define OPEN_MS   200
#define CLOSE_MS  150
#define SLIDE_MS  200
#define PULL_MS   250

#define EASE_OUT  lv_anim_path_ease_out
#define EASE_IN   lv_anim_path_ease_in

static void _anim_scale_cb(void *var, int32_t v)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)var, v, 0);
}

static void _anim_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void _anim_translate_x_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void _anim_translate_y_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

void wos_transition_open(lv_obj_t *root)
{
    if (!root || !lv_obj_is_valid(root))
        return;
    lv_obj_set_style_transform_pivot_x(root, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(root, lv_pct(50), 0);
    lv_obj_set_style_transform_scale(root, 217, 0); /* 0.85 * 256 */
    lv_obj_set_style_opa(root, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_time(&a, OPEN_MS);
    lv_anim_set_path_cb(&a, EASE_OUT);
    lv_anim_set_values(&a, 217, 256);
    lv_anim_set_exec_cb(&a, _anim_scale_cb);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_time(&a, OPEN_MS);
    lv_anim_set_path_cb(&a, EASE_OUT);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a, _anim_opa_cb);
    lv_anim_start(&a);
}

void wos_transition_close(lv_obj_t *root, lv_anim_ready_cb_t on_done)
{
    if (!root || !lv_obj_is_valid(root))
    {
        if (on_done)
            on_done(NULL);
        return;
    }
    lv_obj_set_style_transform_pivot_x(root, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(root, lv_pct(50), 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_time(&a, CLOSE_MS);
    lv_anim_set_path_cb(&a, EASE_IN);
    lv_anim_set_values(&a, 256, 217);
    lv_anim_set_exec_cb(&a, _anim_scale_cb);
    if (on_done)
        lv_anim_set_ready_cb(&a, on_done);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_time(&a, CLOSE_MS);
    lv_anim_set_path_cb(&a, EASE_IN);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_exec_cb(&a, _anim_opa_cb);
    lv_anim_start(&a);
}

void wos_transition_slide(lv_obj_t *from, lv_obj_t *to, lv_dir_t dir)
{
    int sign = (dir == LV_DIR_LEFT) ? -1 : 1;
    lv_coord_t w = 240;

    if (to && lv_obj_is_valid(to))
    {
        lv_obj_set_style_translate_x(to, sign * w, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, to);
        lv_anim_set_time(&a, SLIDE_MS);
        lv_anim_set_path_cb(&a, EASE_OUT);
        lv_anim_set_values(&a, sign * w, 0);
        lv_anim_set_exec_cb(&a, _anim_translate_x_cb);
        lv_anim_start(&a);
    }
    if (from && lv_obj_is_valid(from))
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, from);
        lv_anim_set_time(&a, SLIDE_MS);
        lv_anim_set_path_cb(&a, EASE_OUT);
        lv_anim_set_values(&a, 0, -sign * w);
        lv_anim_set_exec_cb(&a, _anim_translate_x_cb);
        lv_anim_start(&a);
    }
}

void wos_transition_pull_down(lv_obj_t *panel)
{
    if (!panel || !lv_obj_is_valid(panel))
        return;
    lv_obj_set_style_translate_y(panel, -240, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_time(&a, PULL_MS);
    lv_anim_set_path_cb(&a, EASE_OUT);
    lv_anim_set_values(&a, -240, 0);
    lv_anim_set_exec_cb(&a, _anim_translate_y_cb);
    lv_anim_start(&a);
}
