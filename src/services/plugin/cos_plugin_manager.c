/**
 * @file cos_plugin_manager.c
 * @brief Plugin Manager implementation (SD scan / register).
 */

#include "cos_plugin_manager.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "cos_log.h"
#include "cos_storage_paths.h"
#include "services/storage/cos_service_storage.h"
#include "framework/app/cos_app.h"
#include "framework/watchface/cos_watchface.h"
#include "framework/package/cos_pkg_mgr.h"

#define COS_LOG_TAG "PluginMgr"

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

cos_result_t cos_plugin_manager_scan(bool force, cos_plugin_scan_result_t *out_result)
{
    cos_plugin_scan_result_t r;
    memset(&r, 0, sizeof(r));

    cos_dir_t dir = cos_storage_dir_open(COS_SD_APPS_DIR);
    if (!dir)
    {
        COS_LOG_W("[PluginMgr] cannot open SD apps dir: %s (skipping scan)",
                  COS_SD_APPS_DIR);
        if (out_result)
            *out_result = r;
        return COS_OK; /* missing SD is allowed (Shell must work without it) */
    }

    char name[COS_FS_PATH_MAX];
    while (cos_storage_dir_read(dir, name, sizeof(name)) == COS_OK)
    {
        bool is_wf = _ends_with(name, ".ewpk");
        bool is_app = _ends_with(name, ".eapk");
        if (!is_wf && !is_app)
            continue;

        char full[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
        snprintf(full, sizeof(full), "%s%s", COS_SD_APPS_DIR, name);
        if (!cos_storage_is_file(full))
            continue;

        r.scanned++;

        /* Read the header to learn the package id (used for dedup) and to
         * confirm this is a genuine package. */
        cos_pkg_header_t hdr;
        const char *id = NULL;
        if (cos_pkg_read_header(full, &hdr) == COS_OK)
            id = hdr.pkg_id;

        bool already = false;
        if (is_wf)
        {
            if (id && cos_watchface_list_contains(id))
                already = true;
        }
        else
        {
            if (id && cos_app_list_contains(id))
            {
                /* The install dir entry may be a stale/empty directory left
                 * behind by a failed unpack or a manual wipe. Require the
                 * manifest file to actually exist before treating the app as
                 * installed, otherwise force a (re)install. */
                char manifest[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
                snprintf(manifest, sizeof(manifest),
                         COS_APP_INSTALLED_DIR "%s/" COS_APP_MANIFEST_FILE_NAME, id);
                already = cos_storage_is_file(manifest);
            }
        }

        if (already && !force)
        {
            r.skipped++;
            continue;
        }

        cos_result_t ir = is_wf ? cos_watchface_install(full)
                                : cos_app_install(full);
        if (ir == COS_OK)
            r.installed++;
        else
        {
            r.failed++;
            COS_LOG_W("[PluginMgr] install failed for %s (result=%d)", name, (int)ir);
        }
    }

    cos_storage_dir_close(dir);

    if (out_result)
        *out_result = r;

    COS_LOG_I("[PluginMgr] scan done: scanned=%u installed=%u skipped=%u failed=%u",
              r.scanned, r.installed, r.skipped, r.failed);
    return COS_OK;
}

cos_result_t cos_plugin_manager_init(void)
{
    return cos_plugin_manager_scan(false, NULL);
}

uint32_t cos_plugin_manager_count_installed(void)
{
    return cos_app_get_installed();
}
