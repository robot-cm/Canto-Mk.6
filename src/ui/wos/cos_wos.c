/**
 * @file eos_wos.c
 * @brief WOS framework initialization
 */
#include "eos_wos.h"

#include "eos_log.h"

#define EOS_LOG_TAG "Wos"

/** Demo app descriptor (apps/eos_wos_app_demo.c) */
const eos_wos_app_desc_t *wos_demo_app_get_desc(void);

void eos_wos_framework_init(void)
{
    wos_app_manager_init();
    wos_statusbar_init();
    wos_notification_init();

    /* register built-in demo app */
    wos_app_manager_register(wos_demo_app_get_desc());

    EOS_LOG_I("wos: framework initialized");
}
