/**
 * @file eos_files.h
 * @brief Minimal file browser (native app)
 *
 * Browse the filesystem from EOS_FILES_ROOT_DIR ("/"), descend into
 * directories, and open text files in a scrollable viewer.
 */

#ifndef EOS_FILES_H
#define EOS_FILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Enter the Files browser.
 */
void eos_files_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_FILES_H */
