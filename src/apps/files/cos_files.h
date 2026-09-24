/**
 * @file cos_files.h
 * @brief Minimal file browser (native app)
 *
 * Browse the filesystem from COS_FILES_ROOT_DIR ("/"), descend into
 * directories, and open text files in a scrollable viewer.
 */

#ifndef COS_FILES_H
#define COS_FILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Enter the Files browser.
 */
void cos_files_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_FILES_H */
