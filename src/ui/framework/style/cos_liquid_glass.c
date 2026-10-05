/**
 * @file cos_liquid_glass.c
 * @brief Simplified Liquid Glass style helpers.
 */
#include "cos_liquid_glass.h"
#include <lvgl.h>

void cos_liquid_glass_card(void *obj)
{
    lv_obj_t *o = (lv_obj_t *)obj;
    if (!o) return;
    lv_obj_set_style_bg_opa(o, 150, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x2a3a5a), 0);
    lv_obj_set_style_radius(o, 18, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, 80, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    /* 16px 模糊阴影是逐像素开销大户(每帧多 pass 模糊 + 逐像素 mask),
     * 卡片本身已有 1px 高光边框,去掉阴影视觉损失很小;改用略强的边框
     * 补偿立体感。 */
    lv_obj_set_style_border_opa(o, 120, 0);
}

void cos_liquid_glass_panel(void *obj)
{
    lv_obj_t *o = (lv_obj_t *)obj;
    if (!o) return;
    lv_obj_set_style_bg_opa(o, 110, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x101826), 0);
    lv_obj_set_style_radius(o, 24, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, 60, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
}

void cos_liquid_glass_card_subtle(void *obj)
{
    lv_obj_t *o = (lv_obj_t *)obj;
    if (!o) return;
    /* Subtle glass: only a soft highlight edge + slightly translucent corners.
     * No dark-blue wash and NO glow shadow, so it reads as "glassy corners"
     * without being obvious and keeps the card's own accent background. */
    lv_obj_set_style_bg_opa(o, 205, 0);   /* a touch of see-through at the corners */
    lv_obj_set_style_radius(o, 16, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, 45, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
}

void cos_app_icon_solid(void *obj)
{
    lv_obj_t *o = (lv_obj_t *)obj;
    if (!o) return;
    /* Clean solid disc / rounded tile for Launcher app icons — replaces the
     * liquid-glass look so icons read crisply on any watchface (no translucent
     * wash that made split-background watchfaces look half-black / half-white). */
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x22304A), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
}
