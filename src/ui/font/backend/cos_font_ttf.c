/**
 * @file cos_font_ttf.c
 * @brief TTF file
 */

#include "cos_config.h"
#if COS_FONT_TYPE == COS_FONT_TTF
#include "cos_font.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "cos_theme.h"
#include "cos_service_storage.h"

/* Macros and Definitions -------------------------------------*/
LV_FONT_DECLARE(COS_FONT_ICON);
#if COS_FONT_TTF_TYPE == COS_FONT_TTF_DATA
COS_FONT_DATA_DECLARE(COS_FONT_TTF_DATA_NAME);
COS_FONT_DATA_SIZE_DECLARE(COS_FONT_TTF_DATA_SIZE);
#endif /* COS_FONT_TTF_TYPE */
/* Variables --------------------------------------------------*/
static lv_font_t *font_large;
static lv_font_t *font_medium;
static lv_font_t *font_small;
static bool _font_inited = false;
#if COS_FONT_TTF_TYPE == COS_FONT_TTF_FILE
/* 字体来源:优先外部 SD 卡(COS_FONT_TTF_FILE_PATH = /sdcard/font/font.ttf),
 * 缺失时回退系统内置资源目录. 路径带 LVGL FS 盘符(COS_LVGL_FS_LETTER 'Z'),
 * 经 lv_fs_resolve_path 路由到 cos_storage_file_open_read() 读取. */
static char _font_path[256];

static void _resolve_font_path(void)
{
    snprintf(_font_path, sizeof(_font_path),
             "%c:" COS_FONT_TTF_FILE_PATH, (int)COS_LVGL_FS_LETTER);
    if (cos_storage_is_file(COS_FONT_TTF_FILE_PATH))
    {
        COS_LOG_I("Font source: external %s", COS_FONT_TTF_FILE_PATH);
        return;
    }
    COS_LOG_W("External font not found (%s), fallback to system resource",
              COS_FONT_TTF_FILE_PATH);
    snprintf(_font_path, sizeof(_font_path),
             "%c:" COS_SYS_RES_FONT_DIR "font.ttf", (int)COS_LVGL_FS_LETTER);
}
#endif
/* Function Implementations -----------------------------------*/

lv_font_t *cos_font_init(void)
{
    if (_font_inited)
    {
        COS_LOG_W("Font system already initialized, returning cached font");
        return font_medium;
    }

    COS_LOG_I("Font system init");

#if COS_FONT_TTF_TYPE == COS_FONT_TTF_FILE
    /* 开机流程中此处早于开机动画(cos_boot_anim_start),且存储服务已就绪 */
    _resolve_font_path();
#endif

#if COS_FONT_TTF_TYPE == COS_FONT_TTF_DATA

#if COS_FONT_TTF_ENABLE_EXTENDED
    font_large = lv_tiny_ttf_create_data_ex(COS_FONT_TTF_DATA_NAME,
                                            COS_FONT_TTF_DATA_SIZE,
                                            COS_FONT_SIZE_LARGE,
                                            COS_FONT_TTF_KERNING,
                                            COS_FONT_TTF_CACHE_SIZE);
    font_medium = lv_tiny_ttf_create_data_ex(COS_FONT_TTF_DATA_NAME,
                                             COS_FONT_TTF_DATA_SIZE,
                                             COS_FONT_SIZE_MEDIUM,
                                             COS_FONT_TTF_KERNING,
                                             COS_FONT_TTF_CACHE_SIZE);
    font_small = lv_tiny_ttf_create_data_ex(COS_FONT_TTF_DATA_NAME,
                                            COS_FONT_TTF_DATA_SIZE,
                                            COS_FONT_SIZE_SMALL,
                                            COS_FONT_TTF_KERNING,
                                            COS_FONT_TTF_CACHE_SIZE);
#else
    font_large = lv_tiny_ttf_create_data(COS_FONT_TTF_DATA_NAME, COS_FONT_TTF_DATA_SIZE, COS_FONT_SIZE_LARGE);
    font_medium = lv_tiny_ttf_create_data(COS_FONT_TTF_DATA_NAME, COS_FONT_TTF_DATA_SIZE, COS_FONT_SIZE_MEDIUM);
    font_small = lv_tiny_ttf_create_data(COS_FONT_TTF_DATA_NAME, COS_FONT_TTF_DATA_SIZE, COS_FONT_SIZE_SMALL);
#endif /* COS_FONT_TTF_ENABLE_EXTENDED */

#elif COS_FONT_TTF_TYPE == COS_FONT_TTF_FILE

#if COS_FONT_TTF_ENABLE_EXTENDED
    font_large =
        lv_tiny_ttf_create_file_ex(_font_path, COS_FONT_SIZE_LARGE, COS_FONT_TTF_KERNING, COS_FONT_TTF_CACHE_SIZE);
    font_medium =
        lv_tiny_ttf_create_file_ex(_font_path, COS_FONT_SIZE_MEDIUM, COS_FONT_TTF_KERNING, COS_FONT_TTF_CACHE_SIZE);
    font_small =
        lv_tiny_ttf_create_file_ex(_font_path, COS_FONT_SIZE_SMALL, COS_FONT_TTF_KERNING, COS_FONT_TTF_CACHE_SIZE);
#else
    font_large = lv_tiny_ttf_create_file(_font_path, COS_FONT_SIZE_LARGE);
    font_medium = lv_tiny_ttf_create_file(_font_path, COS_FONT_SIZE_MEDIUM);
    font_small = lv_tiny_ttf_create_file(_font_path, COS_FONT_SIZE_SMALL);
#endif /* COS_FONT_TTF_ENABLE_EXTENDED */

#endif /* COS_FONT_TTF_TYPE */

    if (!font_large || !font_medium || !font_small)
    {
        COS_LOG_E("Some fonts failed to load!");
        return NULL;
    }
    else
    {
        font_large->fallback = font_medium;
        font_medium->fallback = font_small;
        font_small->fallback = &COS_FONT_ICON;
        COS_LOG_D("All TTF fonts loaded successfully");
    }
    _font_inited = true;
    return font_medium;
}

void cos_font_deinit(void)
{
    if (!_font_inited)
        return;

    COS_LOG_I("Font system deinit");

    if (font_large)
    {
        lv_tiny_ttf_destroy(font_large);
        font_large = NULL;
    }
    if (font_medium)
    {
        lv_tiny_ttf_destroy(font_medium);
        font_medium = NULL;
    }
    if (font_small)
    {
        lv_tiny_ttf_destroy(font_small);
        font_small = NULL;
    }

    _font_inited = false;
}

lv_font_t *cos_font_reload(const char *path)
{
#if COS_FONT_TTF_TYPE == COS_FONT_TTF_FILE
    if (path && path[0])
    {
        snprintf(_font_path, sizeof(_font_path), "%s", path);
        COS_LOG_I("Font path changed to: %s", _font_path);
    }
#endif

    cos_font_deinit();

    lv_font_t *default_font = cos_font_init();
    if (!default_font)
        return NULL;

    cos_theme_set(lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), default_font);

    return default_font;
}

lv_font_t *_select_font(cos_font_size_t size)
{
    switch (size)
    {
        case COS_FONT_SIZE_LARGE:
            return font_large;
        case COS_FONT_SIZE_MEDIUM:
            return font_medium;
        case COS_FONT_SIZE_SMALL:
            return font_small;
        default:
            if (size >= COS_FONT_SIZE_LARGE)
                return font_large;
            else if (size > COS_FONT_SIZE_SMALL)
                return font_medium;
            else
                return font_small;
    }
}

void cos_label_set_font_size(lv_obj_t *label, cos_font_size_t size)
{
    COS_CHECK_PTR_RETURN(label);
    lv_obj_set_style_text_font(label, _select_font(size), 0);
}
#endif /* COS_FONT_TYPE */
