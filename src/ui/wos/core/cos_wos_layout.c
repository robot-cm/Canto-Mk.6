/**
 * @file eos_wos_layout.c
 * @brief WOS layout helpers implementation
 */
#include "eos_wos_layout.h"

#include "eos_wos_theme.h"
#include "eos_log.h"
#include "eos_display_profiles.h"

#define EOS_LOG_TAG "WosLayout"

static void _wos_setup_flex(lv_obj_t *obj, lv_flex_flow_t flow)
{
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(obj, WOS_GAP_ROW, 0);
    lv_obj_set_style_pad_column(obj, WOS_GAP_COL, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *wos_row(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    _wos_setup_flex(obj, LV_FLEX_FLOW_ROW);
    return obj;
}

lv_obj_t *wos_column(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    _wos_setup_flex(obj, LV_FLEX_FLOW_COLUMN);
    return obj;
}

lv_obj_t *wos_card(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    wos_style_glass_card(obj, WOS_RADIUS_CARD, 224); /* 88% tint */
    return obj;
}

lv_obj_t *wos_grid(lv_obj_t *parent, int cols, int gap)
{
    lv_obj_t *obj = lv_obj_create(parent);
    static lv_coord_t cols_dsc[4];
    static lv_coord_t row_dsc[4];
    if (cols > 3)
        cols = 3;
    int i;
    for (i = 0; i < cols; i++)
        cols_dsc[i] = LV_GRID_FR(1);
    cols_dsc[cols] = LV_GRID_TEMPLATE_LAST;
    row_dsc[0] = LV_GRID_CONTENT;
    row_dsc[1] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(obj, cols_dsc, row_dsc);
    lv_obj_set_style_pad_row(obj, gap, 0);
    lv_obj_set_style_pad_column(obj, gap, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

void wos_flex_grow(lv_obj_t *obj)
{
    lv_obj_set_flex_grow(obj, 1);
}

void wos_make_safe_area(lv_obj_t *root)
{
    if (!root)
        return;
    eos_display_profile_t p = eos_display_profiles_get(EOS_PROFILE_240C);
    lv_obj_set_width(root, lv_pct(100));
    lv_obj_set_height(root, lv_pct(100));
    /* For the round display keep content inside the inscribed circle:
     * pad the root by (screen - safe) on each side. */
    if (p.shape == EOS_DISPLAY_SHAPE_CIRCLE && p.safe_radius > 0.0f)
    {
        lv_coord_t margin = (lv_coord_t)(240 - p.safe_radius * 2.0f) / 2;
        if (margin < 0)
            margin = 0;
        lv_obj_set_style_pad_top(root, margin + WOS_STATUSBAR_H, 0);
        lv_obj_set_style_pad_bottom(root, margin, 0);
        lv_obj_set_style_pad_left(root, margin, 0);
        lv_obj_set_style_pad_right(root, margin, 0);
    }
    else
    {
        lv_obj_set_style_pad_top(root, WOS_STATUSBAR_H, 0);
    }
}
