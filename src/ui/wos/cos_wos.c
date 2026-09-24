/**
 * @file cos_wos.c
 * @brief WOS framework initialization
 */
#include "cos_wos.h"

#include "cos_log.h"

#define COS_LOG_TAG "Wos"

/** Demo app descriptor (apps/cos_wos_app_demo.c) */
const cos_wos_app_desc_t *wos_demo_app_get_desc(void);

void cos_wos_framework_init(void)
{
    wos_app_manager_init();
    wos_statusbar_init();
    wos_notification_init();

    /* register built-in demo app */
    wos_app_manager_register(wos_demo_app_get_desc());

    COS_LOG_I("wos: framework initialized");
}
