/**
 * @file eos_plugin_manager.h
 * @brief Plugin Manager — scans the SD card for plugin packages and registers them.
 *
 * The Plugin Manager is the bridge between the "SD card as install source"
 * model (see design doc stage E) and the existing install primitives
 * (eos_app_install / eos_watchface_install). On boot (or on a manual
 * `plugin scan` shell command) it walks EOS_SD_APPS_DIR, reads each
 * .eapk / .ewpk header, and registers packages that are not already
 * installed. The operation is idempotent: already-installed plugins are
 * skipped (unless force is requested).
 *
 * This module is Core-level: it only depends on stable storage / app /
 * watchface / package-manager APIs and never touches the UI or LVGL.
 */

#ifndef EOS_PLUGIN_MANAGER_H
#define EOS_PLUGIN_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "eos_core.h"

/* Public typedefs --------------------------------------------*/

/**
 * @brief Result of a Plugin Manager scan.
 */
typedef struct
{
    uint32_t scanned;   /**< number of .eapk/.ewpk files found on the SD */
    uint32_t installed; /**< number of packages freshly installed/registered */
    uint32_t skipped;   /**< number already installed (idempotent skip) */
    uint32_t failed;    /**< number that failed to install */
} eos_plugin_scan_result_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize the Plugin Manager and perform the first scan.
 * @return eos_result_t EOS_OK on success (a missing SD card is not an error).
 * @note Safe to call once during boot, after eos_app_init() / eos_watchface_init().
 */
eos_result_t eos_plugin_manager_init(void);

/**
 * @brief Scan the SD apps directory and register any new plugins.
 * @param force When true, re-install packages that are already present
 *              (used to pick up updated packages). When false, already
 *              installed packages are skipped (idempotent).
 * @param out_result Optional pointer filled with the scan tally. May be NULL.
 * @return eos_result_t EOS_OK on success.
 */
eos_result_t eos_plugin_manager_scan(bool force, eos_plugin_scan_result_t *out_result);

/**
 * @brief Convenience: number of currently installed application plugins.
 */
uint32_t eos_plugin_manager_count_installed(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_PLUGIN_MANAGER_H */
