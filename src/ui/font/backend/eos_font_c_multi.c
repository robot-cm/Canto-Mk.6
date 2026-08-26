/**
 * @file eos_font_c_multi.c
 * @brief Multi-font resource
 *
 * 内置 C 字体(编译进 Flash, XIP 映射, 不占 PSRAM/DRAM)。
 * LV_FONT_DECLARE 静态引用各字号字体及其跨字体 fallback 链。
 */

#include "eos_config.h"
#if EOS_FONT_TYPE == EOS_FONT_C_MULTI
#include "eos_font.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

/* Macros and Definitions -------------------------------------*/
/* 内置 C 字体模式(编译进 Flash, XIP 映射, 不占 PSRAM/DRAM) */
LV_FONT_DECLARE(EOS_FONT_LARGE_NAME);
LV_FONT_DECLARE(EOS_FONT_MEDIUM_NAME);
LV_FONT_DECLARE(EOS_FONT_SMALL_NAME);
LV_FONT_DECLARE(EOS_FONT_EXTRA_SMALL_NAME);
LV_FONT_DECLARE(EOS_FONT_ICON);

/* Variables --------------------------------------------------*/
static lv_font_t *font_large;
static lv_font_t *font_medium;
static lv_font_t *font_small;
static lv_font_t *font_extra_small;
static bool _font_inited = false;
/* Function Implementations -----------------------------------*/

lv_font_t *eos_font_init(void)
{
    if (_font_inited)
    {
        EOS_LOG_W("Font system already initialized, returning cached font");
        return font_medium;
    }

    EOS_LOG_I("Font system init (built-in fonts)");
    font_large = &EOS_FONT_LARGE_NAME;
    font_medium = &EOS_FONT_MEDIUM_NAME;
    font_small = &EOS_FONT_SMALL_NAME;
    font_extra_small = &EOS_FONT_EXTRA_SMALL_NAME;

    _font_inited = true;
    return font_medium;
}

void eos_font_deinit(void)
{
    if (!_font_inited)
        return;
    font_large = NULL;
    font_medium = NULL;
    font_small = NULL;
    font_extra_small = NULL;
    _font_inited = false;
}

lv_font_t *eos_font_reload(const char *path)
{
    (void)path;
    eos_font_deinit();
    return eos_font_init();
}

lv_font_t *_select_font(eos_font_size_t size)
{
    if (size >= EOS_FONT_SIZE_LARGE)
        return font_large;
    else if (size == EOS_FONT_SIZE_EXTRA_SMALL)
        return font_extra_small;
    else if (size > EOS_FONT_SIZE_SMALL)
        return font_medium;
    else
        return font_small;
}

void eos_label_set_font_size(lv_obj_t *label, eos_font_size_t size)
{
    EOS_CHECK_PTR_RETURN(label);
    lv_obj_set_style_text_font(label, _select_font(size), 0);
}

#endif /* EOS_FONT_TYPE */
