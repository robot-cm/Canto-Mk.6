/**
 * @file cos_storage_paths.h
 * @brief System storage paths definition
 *
 * This file defines all system directory paths in a centralized location.
 * All storage related path constants should be defined here to maintain
 * a clear and unified view of the system's directory structure.
 */

#ifndef COS_STORAGE_PATHS_H
#define COS_STORAGE_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "cos_config.h"

/* Public macros - System Root and Base Directories --------*/

/**
 * Root directory is defined in cos_config.h as COS_SYS_ROOT_DIR
 * Default: "/" on Native platform, or file system path on simulator
 */

/** @brief System base directory */
#define COS_SYS_DIR "/.sys/"

/* Configuration Storage -----------------------------------*/
/** @brief Configuration files directory */
#define COS_CONFIG_DIR COS_SYS_DIR "config/"

/** @brief Main configuration file */
#define COS_CONFIG_FILE_PATH COS_CONFIG_DIR "cfg.json"

/* Resources Storage ---------------------------------------*/
/** @brief System resources directory */
#define COS_SYS_RES_DIR COS_SYS_DIR "res/"

/** @brief System images resources directory */
#define COS_SYS_RES_IMG_DIR COS_SYS_RES_DIR "img/"

/** @brief System fonts resources directory */
#define COS_SYS_RES_FONT_DIR COS_SYS_RES_DIR "font/"

/* Application Management Storage --------------------------*/
/** @brief Application management base directory */
#define COS_APP_DIR COS_SYS_DIR "app/"

/** @brief Installed applications directory */
#define COS_APP_INSTALLED_DIR COS_APP_DIR "apps/"

/** @brief Application data storage directory */
#define COS_APP_DATA_DIR COS_APP_DIR "app_data/"

/* Watchface Management Storage ----------------------------*/
/** @brief Watchface management base directory */
#define COS_WATCHFACE_DIR COS_SYS_DIR "wf/"

/** @brief Installed watchfaces directory */
#define COS_WATCHFACE_INSTALLED_DIR COS_WATCHFACE_DIR "faces/"

/** @brief Watchface data storage directory */
#define COS_WATCHFACE_DATA_DIR COS_WATCHFACE_DIR "wf_data/"

/* User-facing media (native Gallery / Files apps) ----------------*/
/** @brief User gallery (image) directory scanned by the Gallery app */
#define COS_GALLERY_DIR "/gallery/"

/** @brief Root directory for the Files browser */
#define COS_FILES_ROOT_DIR "/"

/* Removable storage (SD card) ----------------------------------*/
/** @brief SD card mount root. On Native this is a directory under the build
 *         FS; on MCU it is the mounted microSD root. Plugins are staged here
 *         as .eapk / .ewpk and registered by the Plugin Manager. */
#define COS_SD_ROOT_DIR "/sdcard/"

/** @brief Directory on the SD card scanned by the Plugin Manager for packages */
#define COS_SD_APPS_DIR "/sdcard/apps/"

#ifdef __cplusplus
}
#endif

#endif /* COS_STORAGE_PATHS_H */
