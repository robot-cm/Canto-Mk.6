/**
 * @file cos_wos_app.h
 * @brief WOS app instance abstraction (see design doc §2.4)
 *
 * Every app owns a fully independent world: its own root container, its own
 * UI objects, its own event bindings and its own timers. The timer/animation
 * ledger guarantees nothing survives a close.
 */
#ifndef COS_WOS_APP_H
#define COS_WOS_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

#define WOS_APP_MAX_TIMERS 8
#define WOS_APP_MAX_ANIMS  8

typedef struct cos_wos_app cos_wos_app_t;

/**
 * @brief App descriptor — registered with the App Manager.
 */
typedef struct
{
    const char *id;   /**< unique id, e.g. "demo", "clock" */
    const char *name; /**< display name */
    /** Build the app UI inside app->root. Called once per open. */
    void (*build)(cos_wos_app_t *app);
    /** Release app-owned resources (user_data, JS program...). */
    void (*destroy)(cos_wos_app_t *app);
} cos_wos_app_desc_t;

struct cos_wos_app
{
    const cos_wos_app_desc_t *desc;
    lv_obj_t *root;   /**< app-owned container (flex) — the only page object */
    void *user_data;  /**< app private state */

    /* Resource ledger: every timer/anim created by this app is registered
     * here and force-deleted on close (no leak, no orphan callbacks). */
    lv_timer_t *timers[WOS_APP_MAX_TIMERS];
    uint8_t timer_cnt;
    lv_anim_t *anims[WOS_APP_MAX_ANIMS];
    uint8_t anim_cnt;
};

/**
 * @brief Register a timer on the app ledger.
 * @param app  App instance
 * @param cb   Timer callback
 * @param period_ms Period
 * @param user_data Passed to the callback (recommended: app)
 */
lv_timer_t *wos_app_timer(cos_wos_app_t *app, lv_timer_cb_t cb, uint32_t period_ms, void *user_data);

/**
 * @brief Get the display name of an app descriptor.
 */
const char *wos_app_desc_name(const cos_wos_app_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* COS_WOS_APP_H */
