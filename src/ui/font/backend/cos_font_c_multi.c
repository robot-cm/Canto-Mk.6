/**
 * @file cos_font_c_multi.c
 * @brief Multi-font resource
 *
 * 内置 C 字体(编译进 Flash, XIP 映射, 不占 PSRAM/DRAM)。
 * LV_FONT_DECLARE 静态引用各字号字体及其跨字体 fallback 链。
 */

#include "cos_config.h"
#if COS_FONT_TYPE == COS_FONT_C_MULTI
#include "cos_font.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

/* Macros and Definitions -------------------------------------*/
/* 内置 C 字体模式(编译进 Flash, XIP 映射, 不占 PSRAM/DRAM) */
LV_FONT_DECLARE(COS_FONT_LARGE_NAME);
LV_FONT_DECLARE(COS_FONT_MEDIUM_NAME);
LV_FONT_DECLARE(COS_FONT_SMALL_NAME);
LV_FONT_DECLARE(COS_FONT_TALL_NAME);
LV_FONT_DECLARE(COS_FONT_EXTRA_SMALL_NAME);
LV_FONT_DECLARE(COS_FONT_MICRO_NAME);
LV_FONT_DECLARE(COS_FONT_TINY_NAME);
LV_FONT_DECLARE(COS_FONT_ICON);

/* Variables --------------------------------------------------*/
static lv_font_t *font_large;
static lv_font_t *font_medium;
static lv_font_t *font_small;
static lv_font_t *font_tall;
static lv_font_t *font_extra_small;
static lv_font_t *font_micro;
static lv_font_t *font_tiny;
static bool _font_inited = false;
/* Function Implementations -----------------------------------*/

lv_font_t *cos_font_init(void)
{
    if (_font_inited)
    {
        COS_LOG_W("Font system already initialized, returning cached font");
        return font_medium;
    }

    COS_LOG_I("Font system init (built-in fonts)");
    font_large = &COS_FONT_LARGE_NAME;
    font_medium = &COS_FONT_MEDIUM_NAME;
    font_small = &COS_FONT_SMALL_NAME;
    font_tall = &COS_FONT_TALL_NAME;
    font_extra_small = &COS_FONT_EXTRA_SMALL_NAME;
    font_micro = &COS_FONT_MICRO_NAME;
    font_tiny = &COS_FONT_TINY_NAME;

    _font_inited = true;
    return font_medium;
}

void cos_font_deinit(void)
{
    if (!_font_inited)
        return;
    font_large = NULL;
    font_medium = NULL;
    font_small = NULL;
    font_tall = NULL;
    font_extra_small = NULL;
    font_micro = NULL;
    font_tiny = NULL;
    _font_inited = false;
}

lv_font_t *cos_font_reload(const char *path)
{
    (void)path;
    cos_font_deinit();
    return cos_font_init();
}

lv_font_t *_select_font(cos_font_size_t size)
{
    /* 区间映射(7 档):
     * >=30 → jbm_30 | 26..29 → jbm_26 | 20..25 → jbm_20
     * 18..19 → jbm_18 | 16..17 → jbm_16 | 12..15 → jbm_13 | <=11 → jbm_10
     * (原 22..25px 的 LARGE_MINUS 档为死代码,已移除,其请求现落入 SMALL 档) */
    if (size >= COS_FONT_SIZE_LARGE)
        return font_large;
    else if (size >= COS_FONT_SIZE_MEDIUM)
        return font_medium;
    else if (size >= COS_FONT_SIZE_SMALL)
        return font_small;
    else if (size >= COS_FONT_SIZE_TALL)
        return font_tall;
    else if (size >= COS_FONT_SIZE_EXTRA_SMALL)
        return font_extra_small;
    else if (size >= COS_FONT_SIZE_MICRO)
        return font_micro;
    else
        return font_tiny;
}

void cos_label_set_font_size(lv_obj_t *label, cos_font_size_t size)
{
    COS_CHECK_PTR_RETURN(label);
    lv_obj_set_style_text_font(label, _select_font(size), 0);
}

#endif /* COS_FONT_TYPE */
