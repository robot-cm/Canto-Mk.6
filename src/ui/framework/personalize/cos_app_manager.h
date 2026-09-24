/**
 * @file cos_app_manager.h
 * @brief App registry + user ordering (req #5: AppManager).
 */
#ifndef COS_APP_MANAGER_H
#define COS_APP_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COS_APP_MAX       64
#define COS_APP_ID_LEN    32
#define COS_APP_NAME_LEN  32

typedef struct {
    char id[COS_APP_ID_LEN];
    char name[COS_APP_NAME_LEN];
    int  icon_res;     /**< resource id (0 = none) */
} cos_app_info_t;

typedef struct {
    cos_app_info_t apps[COS_APP_MAX];
    int  order[COS_APP_MAX];   /**< display order: order[pos] = app slot */
    int  count;
} cos_app_manager_t;

void cos_app_manager_init(cos_app_manager_t *m);

/** @brief Register an app; returns slot index or -1 if full. */
int  cos_app_manager_register(cos_app_manager_t *m, const char *id,
                              const char *name, int icon_res);

/** @brief Move app from display position `from` to `to` (drag reorder). */
bool cos_app_manager_move(cos_app_manager_t *m, int from, int to);

int  cos_app_manager_count(const cos_app_manager_t *m);
const cos_app_info_t *cos_app_manager_at(const cos_app_manager_t *m, int display_pos);
/** @brief Display position of an app id, or -1 if not found. */
int  cos_app_manager_find(const cos_app_manager_t *m, const char *id);

#ifdef __cplusplus
}
#endif
#endif /* COS_APP_MANAGER_H */
