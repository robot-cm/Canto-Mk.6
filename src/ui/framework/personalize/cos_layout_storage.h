/**
 * @file cos_layout_storage.h
 * @brief Persist user layout + app order (req #5: LayoutStorage).
 *
 * Stores the active desktop style name and the app display order so user
 * customisation survives reboot. Uses the existing storage service
 * (cos_storage_write/read_file); no extra dependency.
 */
#ifndef COS_LAYOUT_STORAGE_H
#define COS_LAYOUT_STORAGE_H

#include "cos_app_manager.h"
#include "cos_storage_paths.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stored under the already-created config directory. */
#define COS_LAYOUT_FILE_PATH  COS_CONFIG_DIR "ui_layout.json"
#define COS_LAYOUT_STYLE_LEN 24

typedef struct {
    char style[COS_LAYOUT_STYLE_LEN];
} cos_layout_storage_t;

void cos_layout_storage_init(cos_layout_storage_t *ls, const char *default_style);

/** @brief Persist style + app-manager display order. Returns true on success. */
bool cos_layout_storage_save(const cos_layout_storage_t *ls,
                             const cos_app_manager_t *m);

/** @brief Load style + reorder the app manager. Returns true on success. */
bool cos_layout_storage_load(cos_layout_storage_t *ls,
                             cos_app_manager_t *m);

#ifdef __cplusplus
}
#endif
#endif /* COS_LAYOUT_STORAGE_H */
