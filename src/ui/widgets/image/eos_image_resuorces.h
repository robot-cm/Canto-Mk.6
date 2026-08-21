/**
 * @file eos_image_resuorces.h
 * @brief Image resources header
 */

#ifndef EOS_IMAGE_RESUORCES_H
#define EOS_IMAGE_RESUORCES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "eos_storage_paths.h"

/* System Built-in Image Resource Definitions -----------------*/
#define EOS_IMG_APP EOS_SYS_RES_IMG_DIR "app.bin"
#define EOS_IMG_SETTINGS EOS_SYS_RES_IMG_DIR "settings.bin"
#define EOS_IMG_APP_HEADER_BG EOS_SYS_RES_IMG_DIR "app_header.bin"
#define EOS_IMG_FLASH_LIGHT EOS_SYS_RES_IMG_DIR "flash_light.bin"
#define EOS_IMG_WATCHFACE EOS_SYS_RES_IMG_DIR "watchface.bin"
#define EOS_IMG_LOGO EOS_SYS_RES_IMG_DIR "logo.bin"

#ifdef __cplusplus
}
#endif

#endif /* EOS_IMAGE_RESUORCES_H */
