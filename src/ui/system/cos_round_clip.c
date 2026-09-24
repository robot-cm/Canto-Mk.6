/**
 * @file cos_round_clip.c
 * @brief Shared round-display clipping helper (see cos_round_clip.h).
 */

#include "ui/system/cos_round_clip.h"
#include "cos_config.h"    /* COS_DISPLAY_RADIUS */

void cos_round_clip(lv_obj_t *view)
{
    if (!view)
        return;

    /* Round the corners and clip drawing to the circle so the square
     * framebuffer corners are hidden behind a black bezel. */
    lv_obj_set_style_radius(view, COS_DISPLAY_RADIUS, 0);
    lv_obj_set_style_clip_corner(view, true, 0);
    lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);

    /* Paint the off-circle area black so the square framebuffer corners
     * read as a circular-watch bezel. */
    lv_obj_t *scr = lv_obj_get_screen(view);
    if (scr)
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    /* The activity view must NOT scroll itself: the carousel / pager
     * handles page paging. */
    lv_obj_clear_flag(view, LV_OBJ_FLAG_SCROLLABLE);
}
