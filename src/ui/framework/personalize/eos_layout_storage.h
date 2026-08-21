/**
 * @file eos_layout_storage.h
 * @brief Persist user layout + app order (req #5: LayoutStorage).
 *
 * Stores the active desktop style name and the app display order so user
 * customisation survives reboot. Uses the existing storage service
 * (eos_storage_write/read_file); no extra dependency.
 */
#ifndef EOS_LAYOUT_STORAGE_H
#define EOS_LAYOUT_STORAGE_H

#include "eos_app_manager.h"
#include "eos_storage_paths.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stored under the already-created config directory. */
#define EOS_LAYOUT_FILE_PATH  EOS_CONFIG_DIR "ui_layout.json"
#define EOS_LAYOUT_STYLE_LEN 24

typedef struct {
    char style[EOS_LAYOUT_STYLE_LEN];
} eos_layout_storage_t;

void eos_layout_storage_init(eos_layout_storage_t *ls, const char *default_style);

/** @brief Persist style + app-manager display order. Returns true on success. */
bool eos_layout_storage_save(const eos_layout_storage_t *ls,
                             const eos_app_manager_t *m);

/** @brief Load style + reorder the app manager. Returns true on success. */
bool eos_layout_storage_load(eos_layout_storage_t *ls,
                             eos_app_manager_t *m);

#ifdef __cplusplus
}
#endif
#endif /* EOS_LAYOUT_STORAGE_H */
