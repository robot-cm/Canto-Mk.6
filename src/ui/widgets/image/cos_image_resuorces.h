/**
 * @file cos_image_resuorces.h
 * @brief Image resources header
 */

#ifndef COS_IMAGE_RESUORCES_H
#define COS_IMAGE_RESUORCES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_storage_paths.h"

/* System Built-in Image Resource Definitions -----------------*/
#define COS_IMG_APP COS_SYS_RES_IMG_DIR "app.bin"
#define COS_IMG_SETTINGS COS_SYS_RES_IMG_DIR "settings.bin"
#define COS_IMG_APP_HEADER_BG COS_SYS_RES_IMG_DIR "app_header.bin"
#define COS_IMG_FLASH_LIGHT COS_SYS_RES_IMG_DIR "flash_light.bin"
#define COS_IMG_WATCHFACE COS_SYS_RES_IMG_DIR "watchface.bin"
#define COS_IMG_LOGO COS_SYS_RES_IMG_DIR "logo.bin"
#define COS_IMG_ALBUM "/sdcard/theme/icons/album.bin"
#define COS_IMG_TEXTHUB "/sdcard/theme/icons/texthub.bin"
#define COS_IMG_DICTIONARY "/sdcard/theme/icons/dictionary.bin"
/* USB MSC App 图标:仓库内无 usb_msc.bin,实际由编译进 Flash 的
 * cos_icon_usb_msc(lv_image_dsc_t)直接作为图标源,此路径仅作占位/回退。 */
#define COS_IMG_USB_MSC "/sdcard/theme/icons/usb_msc.bin"
/* Spotify App 图标:仓库内无 spotify.bin,实际由编译进 Flash 的
 * cos_icon_spotify(lv_image_dsc_t)直接作为图标源,此路径仅作占位/回退。 */
#define COS_IMG_SPOTIFY "/sdcard/theme/icons/spotify.bin"
#define COS_IMG_PROFMONITOR "/sdcard/theme/icons/profmonitor.bin"

#ifdef __cplusplus
}
#endif

#endif /* COS_IMAGE_RESUORCES_H */
