/**
 * @file cos_wos_app.c
 * @brief WOS app instance abstraction implementation
 */
#include "cos_wos_app.h"

#include "cos_log.h"

#define COS_LOG_TAG "WosApp"

lv_timer_t *wos_app_timer(cos_wos_app_t *app, lv_timer_cb_t cb, uint32_t period_ms, void *user_data)
{
    if (!app)
        return NULL;
    if (app->timer_cnt >= WOS_APP_MAX_TIMERS)
    {
        COS_LOG_E("wos: app '%s' timer ledger full", app->desc ? app->desc->id : "?");
        return NULL;
    }
    lv_timer_t *t = lv_timer_create(cb, period_ms, user_data);
    if (t)
    {
        app->timers[app->timer_cnt++] = t;
    }
    return t;
}

const char *wos_app_desc_name(const cos_wos_app_desc_t *desc)
{
    return (desc && desc->name) ? desc->name : (desc ? desc->id : "?");
}
