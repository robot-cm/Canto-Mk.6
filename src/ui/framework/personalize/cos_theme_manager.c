/**
 * @file cos_theme_manager.c
 * @brief Theme manager implementation.
 */
#include "cos_theme_manager.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

void cos_theme_manager_init(cos_theme_t *t)
{
    memset(t, 0, sizeof(*t));
    strncpy(t->name, "LiquidGlass", 23);
    t->name[23] = '\0';
    t->bg_alpha = 170;
    t->accent   = 0x1f6feb;
    t->radius   = 18;
    t->glow     = 40;
    t->glass    = true;
}

void cos_theme_manager_set(cos_theme_t *t, const char *name,
                           uint8_t bg_alpha, uint32_t accent,
                           uint16_t radius, uint8_t glow, bool glass)
{
    strncpy(t->name, name ? name : "Custom", 23);
    t->name[23] = '\0';
    t->bg_alpha = bg_alpha;
    t->accent   = accent;
    t->radius   = radius;
    t->glow     = glow;
    t->glass    = glass;
}

void cos_theme_manager_apply(const cos_theme_t *t, lv_obj_t *obj, bool is_card)
{
    if (!obj) return;
    if (is_card) {
        lv_obj_set_style_bg_opa(obj, t->bg_alpha, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(t->accent), 0);
        lv_obj_set_style_radius(obj, (int32_t)t->radius, 0);
        /* highlight edge */
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_opa(obj, 70, 0);
        lv_obj_set_style_border_color(obj, lv_color_white(), 0);
        /* shadow / subtle glow */
        lv_obj_set_style_shadow_width(obj, t->glow > 0 ? 14 : 0, 0);
        lv_obj_set_style_shadow_opa(obj, t->glow, 0);
        lv_obj_set_style_shadow_color(obj, lv_color_hex(t->accent), 0);
        lv_obj_set_style_shadow_spread(obj, 0, 0);
    } else {
        lv_obj_set_style_bg_opa(obj, (lv_opa_t)((uint16_t)t->bg_alpha * 6 / 10), 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(t->accent), 0);
    }
}

int cos_theme_manager_serialize(const cos_theme_t *t, char *buf, int buflen)
{
    return snprintf(buf, (size_t)buflen,
                    "name=%s;bg_alpha=%u;accent=%u;radius=%u;glow=%u;glass=%d",
                    t->name, (unsigned)t->bg_alpha, (unsigned)t->accent,
                    (unsigned)t->radius, (unsigned)t->glow, t->glass ? 1 : 0);
}

bool cos_theme_manager_deserialize(cos_theme_t *t, const char *buf)
{
    if (!buf) return false;
    char name[24];
    name[0] = '\0';
    unsigned bg = 0, accent = 0, radius = 0, glow = 0, glass = 0;
    if (sscanf(buf, "name=%23[^;];bg_alpha=%u;accent=%u;radius=%u;glow=%u;glass=%u",
               name, &bg, &accent, &radius, &glow, &glass) >= 6) {
        strncpy(t->name, name, 23);
        t->name[23] = '\0';
        t->bg_alpha = (uint8_t)bg;
        t->accent   = (uint32_t)accent;
        t->radius   = (uint16_t)radius;
        t->glow     = (uint8_t)glow;
        t->glass    = (glass != 0);
        return true;
    }
    return false;
}
