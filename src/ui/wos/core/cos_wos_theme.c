/**
 * @file cos_wos_theme.c
 * @brief WOS design tokens implementation
 */
#include "cos_wos_theme.h"

#include "cos_log.h"
#include "cos_font.h" /* cos_label_set_font_size (accepts pixel size) */

#define COS_LOG_TAG "WosTheme"

void wos_style_glass_card(lv_obj_t *obj, int radius, lv_opa_t tint)
{
    if (!obj)
        return;
    lv_obj_set_style_bg_color(obj, WOS_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(obj, tint, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_10, 0);
    lv_obj_set_style_border_color(obj, lv_color_white(), 0);
    lv_obj_set_style_pad_all(obj, WOS_PAD_CARD, 0);
    /* glass cards are not scrollable by default */
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void wos_style_glass_pill(lv_obj_t *obj, lv_opa_t tint)
{
    if (!obj)
        return;
    lv_obj_set_style_bg_color(obj, WOS_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(obj, tint, 0);
    lv_obj_set_style_radius(obj, WOS_RADIUS_PILL, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_10, 0);
    lv_obj_set_style_border_color(obj, lv_color_white(), 0);
    lv_obj_set_style_pad_hor(obj, 10, 0);
}

void wos_style_text_primary(lv_obj_t *label, int size)
{
    if (!label)
        return;
    lv_obj_set_style_text_color(label, WOS_COLOR_TEXT_PRIMARY, 0);
    cos_label_set_font_size(label, (cos_font_size_t)size);
}

void wos_style_text_secondary(lv_obj_t *label, int size)
{
    if (!label)
        return;
    lv_obj_set_style_text_color(label, WOS_COLOR_TEXT_SECONDARY, 0);
    cos_label_set_font_size(label, (cos_font_size_t)size);
}
