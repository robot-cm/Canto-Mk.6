/**
 * @file cos_wos_theme.h
 * @brief WOS design tokens + glass style helpers (see design doc §1)
 */
#ifndef COS_WOS_THEME_H
#define COS_WOS_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ---- Color tokens ---- */
#define WOS_COLOR_BG             lv_color_hex(0x000000) /* OLED deep black */
#define WOS_COLOR_CARD           lv_color_hex(0x1A1E26) /* glass card base */
#define WOS_COLOR_TEXT_PRIMARY   lv_color_hex(0xF5F0F0)
#define WOS_COLOR_TEXT_SECONDARY lv_color_hex(0x8A8F98)
#define WOS_COLOR_ACCENT         lv_color_hex(0xB0949E) /* Monet purple */
#define WOS_COLOR_ACCENT_ALT     lv_color_hex(0x7AD0FF) /* Aurora blue */

/* ---- Radius / font / spacing tokens ---- */
#define WOS_RADIUS_CARD   16
#define WOS_RADIUS_PILL   999
#define WOS_FONT_XXL      28
#define WOS_FONT_XL       20
#define WOS_FONT_MD       14
#define WOS_FONT_SM       11
#define WOS_FONT_XS       10
#define WOS_PAD_CARD      12
#define WOS_GAP_ROW       8
#define WOS_GAP_COL       8

/* ---- Status bar height ---- */
#define WOS_STATUSBAR_H   24

/**
 * @brief Apply the "glass card" style: translucent bg + 1px highlight border.
 * @param obj    Target object
 * @param radius Corner radius
 * @param tint   Background opacity (0..255, e.g. 224 ≈ 88%)
 */
void wos_style_glass_card(lv_obj_t *obj, int radius, lv_opa_t tint);

/**
 * @brief Apply a pill (fully rounded) glass background.
 */
void wos_style_glass_pill(lv_obj_t *obj, lv_opa_t tint);

/**
 * @brief Convenience text style presets.
 */
void wos_style_text_primary(lv_obj_t *label, int size);
void wos_style_text_secondary(lv_obj_t *label, int size);

#ifdef __cplusplus
}
#endif

#endif /* COS_WOS_THEME_H */
