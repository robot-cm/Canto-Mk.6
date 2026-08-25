/**
 * @file eos_plugin_manager.c
 * @brief Plugin Manager implementation (SD scan / register).
 */

#include "eos_plugin_manager.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "eos_log.h"
#include "eos_storage_paths.h"
#include "services/storage/eos_service_storage.h"
#include "framework/app/eos_app.h"
#include "framework/watchface/eos_watchface.h"
#include "framework/package/eos_pkg_mgr.h"

#define EOS_LOG_TAG "PluginMgr"

/* Helpers ----------------------------------------------------*/

static bool _ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s);
    size_t lp = strlen(suffix);
    if (ls < lp)
        return false;
    return strcmp(s + ls - lp, suffix) == 0;
}

/* Public API -------------------------------------------------*/

eos_result_t eos_plugin_manager_scan(bool force, eos_plugin_scan_result_t *out_result)
{
    eos_plugin_scan_result_t r;
    memset(&r, 0, sizeof(r));

    eos_dir_t dir = eos_storage_dir_open(EOS_SD_APPS_DIR);
    if (!dir)
    {
        EOS_LOG_W("[PluginMgr] cannot open SD apps dir: %s (skipping scan)",
                  EOS_SD_APPS_DIR);
        if (out_result)
            *out_result = r;
        return EOS_OK; /* missing SD is allowed (Shell must work without it) */
    }

    char name[EOS_FS_PATH_MAX];
    while (eos_storage_dir_read(dir, name, sizeof(name)) == EOS_OK)
    {
        bool is_wf = _ends_with(name, ".ewpk");
        bool is_app = _ends_with(name, ".eapk");
        if (!is_wf && !is_app)
            continue;

        char full[EOS_FS_PATH_MAX + EOS_FS_NAME_MAX];
        snprintf(full, sizeof(full), "%s%s", EOS_SD_APPS_DIR, name);
        if (!eos_storage_is_file(full))
            continue;

        r.scanned++;

        /* Read the header to learn the package id (used for dedup) and to
         * confirm this is a genuine package. */
        eos_pkg_header_t hdr;
        const char *id = NULL;
        if (eos_pkg_read_header(full, &hdr) == EOS_OK)
            id = hdr.pkg_id;

        bool already = false;
        if (is_wf)
        {
            if (id && eos_watchface_list_contains(id))
                already = true;
        }
        else
        {
            if (id && eos_app_list_contains(id))
                already = true;
        }

        if (already && !force)
        {
            r.skipped++;
            continue;
        }

        eos_result_t ir = is_wf ? eos_watchface_install(full)
                                : eos_app_install(full);
        if (ir == EOS_OK)
            r.installed++;
        else
        {
            r.failed++;
            EOS_LOG_W("[PluginMgr] install failed for %s (result=%d)", name, (int)ir);
        }
    }

    eos_storage_dir_close(dir);

    if (out_result)
        *out_result = r;

    EOS_LOG_I("[PluginMgr] scan done: scanned=%u installed=%u skipped=%u failed=%u",
              r.scanned, r.installed, r.skipped, r.failed);
    return EOS_OK;
}

eos_result_t eos_plugin_manager_init(void)
{
    return eos_plugin_manager_scan(false, NULL);
}

uint32_t eos_plugin_manager_count_installed(void)
{
    return eos_app_get_installed();
}
