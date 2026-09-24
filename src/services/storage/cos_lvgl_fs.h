/**
 * @file cos_lvgl_fs.h
 * @brief File system interface for LVGL, implemented using Canto Mk.6 storage service
 */

#ifndef COS_LVGL_FS_H
#define COS_LVGL_FS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Register LVGL file system driver
 * This function initializes and registers the file system driver with LVGL
 * All file operations will go through the Canto Mk.6 storage service
 */
void cos_lvgl_fs_register(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_LVGL_FS_H */
