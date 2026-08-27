/**
 * @file eos_font.h
 * @brief Font system
 */

#ifndef EOS_FONT_H
#define EOS_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "eos_config.h"
#include "eos_log.h"

/* Public macros ----------------------------------------------*/
#define EOS_FONT_ICON RemixIcon
#define EOS_FONT_DATA_DECLARE(font_name) LV_ATTRIBUTE_EXTERN_DATA extern const unsigned char font_name[]
#define EOS_FONT_DATA_SIZE_DECLARE(font_size) LV_ATTRIBUTE_EXTERN_DATA extern const int font_size
/* Icon font (RemixIcon) definition lives in resources/font/RemixIcon.c */
LV_FONT_DECLARE(RemixIcon);
/* Public typedefs --------------------------------------------*/

typedef enum
{
    EOS_FONT_SIZE_LARGE = EOS_FONT_CFG_LARGE_SIZE,
    EOS_FONT_SIZE_MEDIUM = EOS_FONT_CFG_MEDIUM_SIZE,
    EOS_FONT_SIZE_LARGE_MINUS = EOS_FONT_CFG_LARGE_MINUS_SIZE,   /**< 22px 档:22..25px 请求 */
    EOS_FONT_SIZE_SMALL = EOS_FONT_CFG_SMALL_SIZE,
    EOS_FONT_SIZE_TALL = EOS_FONT_CFG_TALL_SIZE,                 /**< 18px 档:17..19px 请求 */
    EOS_FONT_SIZE_EXTRA_SMALL = 16,   /**< 16px 档:16px 请求 */
    EOS_FONT_SIZE_MICRO = EOS_FONT_CFG_MICRO_SIZE,   /**< 13px 档:12..15px 请求 */
    EOS_FONT_SIZE_TINY = EOS_FONT_CFG_TINY_SIZE,     /**< 10px 档:<=11px 请求 */
} eos_font_size_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Font system initialization
 * @return lv_font_t* Returns default font pointer on success, otherwise returns NULL
 */
lv_font_t *eos_font_init(void);
/**
 * @brief Font system deinitialization - closes font files, frees caches
 */
void eos_font_deinit(void);
/**
 * @brief Reload fonts from file (deinit + init + theme update)
 * @return lv_font_t* Returns default font pointer on success, otherwise returns NULL
 */
lv_font_t *eos_font_reload(const char *path);
/**
 * @brief Set font size for label object
 * @param label Target label
 * @param size Font size, supports any size in pixels
 * @note `EOS_FONT_BASE_SIZE` is the font source size, values larger than this will be distorted.
 */
void eos_label_set_font_size(lv_obj_t *label, eos_font_size_t size);
#ifdef __cplusplus
}
#endif

#endif /* EOS_FONT_H */
