/**
 * @file cos_std_widgets.c
 * @brief System standard widgets
 */

#include "cos_std_widgets.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "cos_lang.h"
#include "cos_theme.h"
#include "cos_basic_widgets.h"
#include "cos_icon.h"
#include "cos_font.h"
/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

lv_obj_t *cos_std_info_create(lv_obj_t *scr,
                              lv_color_t icon_bg_color,
                              const char *icon,
                              const char *title_txt,
                              const char *txt)
{
    lv_obj_t *list = cos_list_create(scr);
    lv_obj_set_style_pad_row(list, 20, 0);

    cos_round_icon_create(list, icon_bg_color, icon);

    lv_obj_t *title_label = lv_label_create(list);
    lv_label_set_text(title_label, title_txt);
    cos_label_set_font_size(title_label, COS_FONT_SIZE_LARGE);

    lv_obj_t *label = lv_label_create(list);
    if (cos_lang_get_current_id() == LANG_EN)
        lv_obj_set_width(label, lv_pct(90));
    else if (cos_lang_get_current_id() == LANG_ZH)
        lv_obj_set_width(label, lv_pct(95));
    else
        lv_obj_set_width(label, lv_pct(80));
    lv_label_set_text(label, txt);
    return list;
}

void cos_std_title_comment_create(lv_obj_t *parent, const char *title, const char *comment)
{
    lv_obj_t *lt, *lc;
    lt = cos_list_add_title(parent, title);
    cos_label_set_font_size(lt, COS_FONT_SIZE_LARGE);
    lc = cos_list_add_comment(parent, comment);
    lv_obj_set_style_margin_ver(lc, 0, 0);
    lv_obj_align_to(lc, lt, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}
