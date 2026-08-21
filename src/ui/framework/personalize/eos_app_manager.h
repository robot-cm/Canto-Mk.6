/**
 * @file eos_app_manager.h
 * @brief App registry + user ordering (req #5: AppManager).
 */
#ifndef EOS_APP_MANAGER_H
#define EOS_APP_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_APP_MAX       64
#define EOS_APP_ID_LEN    32
#define EOS_APP_NAME_LEN  32

typedef struct {
    char id[EOS_APP_ID_LEN];
    char name[EOS_APP_NAME_LEN];
    int  icon_res;     /**< resource id (0 = none) */
} eos_app_info_t;

typedef struct {
    eos_app_info_t apps[EOS_APP_MAX];
    int  order[EOS_APP_MAX];   /**< display order: order[pos] = app slot */
    int  count;
} eos_app_manager_t;

void eos_app_manager_init(eos_app_manager_t *m);

/** @brief Register an app; returns slot index or -1 if full. */
int  eos_app_manager_register(eos_app_manager_t *m, const char *id,
                              const char *name, int icon_res);

/** @brief Move app from display position `from` to `to` (drag reorder). */
bool eos_app_manager_move(eos_app_manager_t *m, int from, int to);

int  eos_app_manager_count(const eos_app_manager_t *m);
const eos_app_info_t *eos_app_manager_at(const eos_app_manager_t *m, int display_pos);
/** @brief Display position of an app id, or -1 if not found. */
int  eos_app_manager_find(const eos_app_manager_t *m, const char *id);

#ifdef __cplusplus
}
#endif
#endif /* EOS_APP_MANAGER_H */
