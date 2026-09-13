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
#define EOS_IMG_ALBUM "/sdcard/theme/icons/album.bin"
#define EOS_IMG_TEXTHUB "/sdcard/theme/icons/texthub.bin"
#define EOS_IMG_DICTIONARY "/sdcard/theme/icons/dictionary.bin"
/* USB MSC App 图标:仓库内无 usb_msc.bin,实际由编译进 Flash 的
 * eos_icon_usb_msc(lv_image_dsc_t)直接作为图标源,此路径仅作占位/回退。 */
#define EOS_IMG_USB_MSC "/sdcard/theme/icons/usb_msc.bin"
/* Spotify App 图标:仓库内无 spotify.bin,实际由编译进 Flash 的
 * eos_icon_spotify(lv_image_dsc_t)直接作为图标源,此路径仅作占位/回退。 */
#define EOS_IMG_SPOTIFY "/sdcard/theme/icons/spotify.bin"

#ifdef __cplusplus
}
#endif

#endif /* EOS_IMAGE_RESUORCES_H */
