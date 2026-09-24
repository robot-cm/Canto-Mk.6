/**
 * @file cos_gallery.h
 * @brief Minimal image viewer (native app)
 *
 * Scans COS_GALLERY_DIR for PNG images and lets the user cycle through them
 * with prev/next buttons. Images are fit-scaled to the round display.
 */

#ifndef COS_GALLERY_H
#define COS_GALLERY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Enter the Gallery image viewer.
 */
void cos_gallery_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_GALLERY_H */
