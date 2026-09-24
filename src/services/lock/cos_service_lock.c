/**
 * @file cos_service_lock.c
 * @brief Lock screen service implementation
 */

#include "cos_service_lock.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#define COS_LOG_TAG "LockService"
#include "cos_log.h"
#include "cos_event.h"
#include "cos_config.h"
#include "cos_service_config.h"
#include "cos_mem.h"
#include "cos_service_pm.h"
#include "cos_lock_page.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/
static bool _lock_active = false;

/* Function Implementations -----------------------------------*/

static void _on_display_on_cb(cos_event_t *e)
{
    (void)e;

    /* Don't re-show if already active */
    if (_lock_active)
    {
        return;
    }

    /* Check if password is enabled and a hash exists */
    bool enabled = cos_config_get_bool(COS_CONFIG_KEY_PASSWORD_ENABLED_BOOL, false);
    if (!enabled)
    {
        return;
    }

    char *hash = cos_config_get_string(COS_CONFIG_KEY_PASSWORD_HASH_STR, "");
    bool has_hash = (hash && strlen(hash) > 0);
    cos_free(hash);

    if (!has_hash)
    {
        return;
    }

    /* Show lock screen security barrier on lv_layer_top() */
    _lock_active = true;
    cos_lock_page_show();
    COS_LOG_I("Lock screen shown");
}

bool cos_lock_screen_is_active(void)
{
    return _lock_active || cos_lock_page_is_visible();
}

void cos_lock_screen_dismiss(void)
{
    if (!_lock_active && !cos_lock_page_is_visible())
    {
        return;
    }

    /* Don't dismiss while sleeping — async verify callback may fire during sleep.
     * Defer dismissal until next DISPLAY_ON so the lock page survives sleep/wake. */
    if (cos_pm_get_state() == COS_PM_SLEEP)
    {
        COS_LOG_W("Lock dismiss deferred: device is asleep");
        return;
    }

    _lock_active = false;

    /* Reset input devices to release any pressed objects before destroying overlay */
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL)
    {
        lv_indev_reset(indev, NULL);
    }

    cos_lock_page_hide();
    COS_LOG_I("Lock screen dismissed");
}

void cos_service_lock_init(void)
{
    COS_LOG_I("Lock service init — security barrier mode (not chrome overlay)");
    cos_event_subscribe_ex(COS_EVENT_SYSTEM_DISPLAY_ON, _on_display_on_cb, NULL, NULL);
}
