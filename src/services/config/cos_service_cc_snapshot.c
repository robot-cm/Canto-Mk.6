/**
 * @file cos_service_cc_snapshot.c
 * @brief Control-Center settings snapshot on the SD card (see header)
 */

#include "cos_service_cc_snapshot.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COS_LOG_TAG "CCSnapshot"
#include "cos_log.h"
#include "cos_mem.h"
#include "cos_service_config.h"
#include "cos_service_display.h"
#include "cos_service_storage.h"
#include "cos_net_wifi.h"
#include "cos_net_bt.h"
#include "cos_port.h"

#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
#include "cos_service_power_save.h"
#include "cos_service_beast_mode.h"
#endif

/* Macros and Definitions -------------------------------------*/
#define _CC_SNAP_BUF_SIZE 256
#define _CC_SNAP_BRIGHTNESS_MIN 0
#define _CC_SNAP_BRIGHTNESS_MAX 100

/* Key names (also used verbatim in the file) */
#define _CC_KEY_BRIGHTNESS "brightness"
#define _CC_KEY_BT         "bt"
#define _CC_KEY_WIFI       "wifi"
#define _CC_KEY_POWER      "power"

/* Power mode strings written to the file */
#define _CC_POWER_STR_SMART "smart"
#define _CC_POWER_STR_SAVE  "powersave"
#define _CC_POWER_STR_BEAST "beastmode"
/* Accept the enum-ish spellings too, so a hand-edited file is forgiving */
#define _CC_POWER_ALT_SAVE  "power-save"
#define _CC_POWER_ALT_BEAST "beast"

/* Variables --------------------------------------------------*/
static bool _cc_sd_probed = false; /* probe done at least once */
static bool _cc_sd_ok     = false; /* last probe result */
/* Last text we wrote, so repeated UI ticks do not hammer the card. Empty =
 * unknown (forces a read from the file on the first store of this boot). */
static char _cc_cache[_CC_SNAP_BUF_SIZE];
static bool _cc_cache_valid = false;

/* Function Implementations -----------------------------------*/

/* ---------------------------------------------------------------- */
/* Probe: is the mounted volume a real SD card?                      */
/* ---------------------------------------------------------------- */

/* Weak hook implemented by the board layer (port/esp32s3/main/main.c).
 * Returning false is also the correct answer on the simulator. */
bool __attribute__((weak)) board_sd_is_real(void)
{
    return false;
}

/* Non-destructive writability probe: only touches the history/cc directory,
 * so a read-only or malformed card cannot be disturbed. */
static bool _cc_probe_by_write(void)
{
    const char *probe = COS_CC_SNAPSHOT_DIR "/.probe";
    cos_storage_mkdir_recursive(COS_CC_SNAPSHOT_DIR);
    if (cos_storage_write_file_immediate(probe, "1", 1) != COS_OK)
    {
        COS_LOG_W("SD probe: %s not writable - falling back to cfg.json", probe);
        return false;
    }
    cos_storage_file_remove(probe);
    return true;
}

bool cos_cc_snapshot_storage_available(void)
{
    if (!_cc_sd_probed)
    {
        _cc_sd_probed = true;
        if (board_sd_is_real())
        {
            _cc_sd_ok = cos_storage_mkdir_recursive(COS_CC_SNAPSHOT_DIR) == COS_OK ||
                        cos_storage_is_dir(COS_CC_SNAPSHOT_DIR);
        }
        else
        {
            _cc_sd_ok = _cc_probe_by_write();
        }
        COS_LOG_I("Snapshot storage: %s (%s)", _cc_sd_ok ? "SD card" : "cfg.json fallback",
                  _cc_sd_ok ? COS_CC_SNAPSHOT_FILE : "no real SD card");
    }
    return _cc_sd_ok;
}

void cos_cc_snapshot_invalidate(void)
{
    _cc_sd_probed  = false;
    _cc_sd_ok      = false;
    _cc_cache_valid = false;
    _cc_cache[0]   = '\0';
}

void cos_cc_snapshot_init(void)
{
    cos_cc_snapshot_invalidate();
    COS_LOG_D("CC snapshot service ready (%s)", COS_CC_SNAPSHOT_FILE);
}

/* ---------------------------------------------------------------- */
/* Parse / format                                                    */
/* ---------------------------------------------------------------- */

static void _cc_snapshot_reset(cos_cc_snapshot_t *snap)
{
    if (!snap)
        return;
    memset(snap, 0, sizeof(*snap));
    snap->power = COS_CC_POWER_SMART;
}

static void _cc_parse_power(const char *v, cos_cc_snapshot_t *snap)
{
    if (!v || !v[0])
        return;
    if (strcmp(v, _CC_POWER_STR_BEAST) == 0 || strcmp(v, _CC_POWER_ALT_BEAST) == 0)
    {
        snap->power     = COS_CC_POWER_BEAST;
        snap->has_power = true;
    }
    else if (strcmp(v, _CC_POWER_STR_SAVE) == 0 || strcmp(v, _CC_POWER_ALT_SAVE) == 0)
    {
        snap->power     = COS_CC_POWER_SAVE;
        snap->has_power = true;
    }
    else if (strcmp(v, _CC_POWER_STR_SMART) == 0)
    {
        snap->power     = COS_CC_POWER_SMART;
        snap->has_power = true;
    }
    else
    {
        COS_LOG_W("Snapshot: unknown power mode '%s' (ignored)", v);
    }
}

bool cos_cc_snapshot_parse(const char *text, cos_cc_snapshot_t *snap)
{
    if (!text || !snap)
        return false;
    _cc_snapshot_reset(snap);
    if (!text[0])
        return false;

    /* Work on a mutable copy: strtok_r needs to write delimiters. */
    char work[_CC_SNAP_BUF_SIZE];
    size_t len = strlen(text);
    if (len >= sizeof(work))
        len = sizeof(work) - 1;
    memcpy(work, text, len);
    work[len] = '\0';

    char *save = NULL;
    for (char *line = strtok_r(work, "\r\n", &save); line;
         line = strtok_r(NULL, "\r\n", &save))
    {
        /* Allow leading spaces and '#' comments for hand-edited files */
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '\0' || *line == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';

        const char *key = line;
        const char *val = eq + 1;
        /* Trim trailing spaces on both sides */
        char *end = (char *)key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        while (*val == ' ' || *val == '\t')
            val++;

        if (strcmp(key, _CC_KEY_BRIGHTNESS) == 0)
        {
            int b = atoi(val);
            if (b < _CC_SNAP_BRIGHTNESS_MIN)
                b = _CC_SNAP_BRIGHTNESS_MIN;
            if (b > _CC_SNAP_BRIGHTNESS_MAX)
                b = _CC_SNAP_BRIGHTNESS_MAX;
            snap->brightness     = (uint8_t)b;
            snap->has_brightness = true;
        }
        else if (strcmp(key, _CC_KEY_BT) == 0)
        {
            snap->bt     = (atoi(val) != 0);
            snap->has_bt = true;
        }
        else if (strcmp(key, _CC_KEY_WIFI) == 0)
        {
            snap->wifi     = (atoi(val) != 0);
            snap->has_wifi = true;
        }
        else if (strcmp(key, _CC_KEY_POWER) == 0)
        {
            _cc_parse_power(val, snap);
        }
        else
        {
            COS_LOG_D("Snapshot: ignoring unknown key '%s'", key);
        }
    }
    return true;
}

int cos_cc_snapshot_format(const cos_cc_snapshot_t *snap, char *buf, size_t cap)
{
    if (!snap || !buf || cap == 0)
        return -1;

    int n = snprintf(buf, cap,
                     "# CantoMk6 control-center snapshot\n"
                     "# key=value; delete a line to leave that setting untouched\n");
    if (n < 0)
        return -1;

    if (snap->has_brightness)
    {
        int w = snprintf(buf + n, (size_t)(cap > (size_t)n ? cap - (size_t)n : 0),
                         _CC_KEY_BRIGHTNESS "=%u\n", (unsigned)snap->brightness);
        if (w > 0)
            n += w;
    }
    if (snap->has_bt)
    {
        int w = snprintf(buf + n, (size_t)(cap > (size_t)n ? cap - (size_t)n : 0),
                         _CC_KEY_BT "=%d\n", snap->bt ? 1 : 0);
        if (w > 0)
            n += w;
    }
    if (snap->has_wifi)
    {
        int w = snprintf(buf + n, (size_t)(cap > (size_t)n ? cap - (size_t)n : 0),
                         _CC_KEY_WIFI "=%d\n", snap->wifi ? 1 : 0);
        if (w > 0)
            n += w;
    }
    if (snap->has_power)
    {
        const char *ps = snap->power == COS_CC_POWER_BEAST ? _CC_POWER_STR_BEAST :
                         snap->power == COS_CC_POWER_SAVE  ? _CC_POWER_STR_SAVE :
                                                             _CC_POWER_STR_SMART;
        int w = snprintf(buf + n, (size_t)(cap > (size_t)n ? cap - (size_t)n : 0),
                         _CC_KEY_POWER "=%s\n", ps);
        if (w > 0)
            n += w;
    }
    return n;
}

/* ---------------------------------------------------------------- */
/* File access                                                       */
/* ---------------------------------------------------------------- */

/* Read the snapshot text into @p out (size @p cap). Returns false when the
 * file is absent or unreadable. */
static bool _cc_read_text(char *out, size_t cap)
{
    out[0] = '\0';
    if (!cos_storage_is_file(COS_CC_SNAPSHOT_FILE))
        return false;
    char *content = cos_storage_read_file(COS_CC_SNAPSHOT_FILE);
    if (!content)
        return false;
    snprintf(out, cap, "%s", content);
    cos_free(content);
    return true;
}

bool cos_cc_snapshot_load(cos_cc_snapshot_t *snap, bool force_sync)
{
    if (!snap)
        return false;
    _cc_snapshot_reset(snap);

    if (!cos_cc_snapshot_storage_available())
        return false;

    char text[_CC_SNAP_BUF_SIZE];
    if (!force_sync && _cc_cache_valid)
    {
        snprintf(text, sizeof(text), "%s", _cc_cache);
    }
    else if (!_cc_read_text(text, sizeof(text)))
    {
        _cc_cache_valid = false;
        _cc_cache[0]    = '\0';
        return false;
    }
    else
    {
        snprintf(_cc_cache, sizeof(_cc_cache), "%s", text);
        _cc_cache_valid = true;
    }

    if (!cos_cc_snapshot_parse(text, snap))
        return false;
    return true;
}

cos_result_t cos_cc_snapshot_store_kv(const char *key, const char *value_text)
{
    if (!key || !value_text)
        return COS_ERR_VAR_NULL;

    if (!cos_cc_snapshot_storage_available())
        return COS_OK; /* transparent fallback: cfg.json stays authoritative */

    /* Read the current text (cache first, disk otherwise) so the other three
     * keys survive the rewrite. */
    char old_text[_CC_SNAP_BUF_SIZE];
    bool have_old = false;
    if (_cc_cache_valid)
    {
        snprintf(old_text, sizeof(old_text), "%s", _cc_cache);
        have_old = true;
    }
    else
    {
        have_old = _cc_read_text(old_text, sizeof(old_text));
    }

    /* Decode → mutate the single field → re-encode. Going through the struct
     * (instead of splicing lines) keeps the file normalised and guarantees an
     * outdated/duplicated key cannot survive. */
    cos_cc_snapshot_t snap;
    if (have_old)
        cos_cc_snapshot_parse(old_text, &snap);
    else
        _cc_snapshot_reset(&snap);

    if (strcmp(key, _CC_KEY_BRIGHTNESS) == 0)
    {
        int b = atoi(value_text);
        if (b < _CC_SNAP_BRIGHTNESS_MIN)
            b = _CC_SNAP_BRIGHTNESS_MIN;
        if (b > _CC_SNAP_BRIGHTNESS_MAX)
            b = _CC_SNAP_BRIGHTNESS_MAX;
        snap.brightness     = (uint8_t)b;
        snap.has_brightness = true;
    }
    else if (strcmp(key, _CC_KEY_BT) == 0)
    {
        snap.bt     = (atoi(value_text) != 0);
        snap.has_bt = true;
    }
    else if (strcmp(key, _CC_KEY_WIFI) == 0)
    {
        snap.wifi     = (atoi(value_text) != 0);
        snap.has_wifi = true;
    }
    else if (strcmp(key, _CC_KEY_POWER) == 0)
    {
        _cc_parse_power(value_text, &snap);
        if (!snap.has_power)
            return COS_ERR_VAR_NULL;
    }
    else
    {
        COS_LOG_W("Snapshot: unsupported key '%s'", key);
        return COS_ERR_VAR_NULL;
    }

    char text[_CC_SNAP_BUF_SIZE];
    int n = cos_cc_snapshot_format(&snap, text, sizeof(text));
    if (n <= 0)
        return COS_ERR_JSON_ERROR;

    cos_result_t ret = cos_storage_write_file_immediate(COS_CC_SNAPSHOT_FILE, text, (size_t)n);
    if (ret == COS_OK)
    {
        snprintf(_cc_cache, sizeof(_cc_cache), "%s", text);
        _cc_cache_valid = true;
    }
    else
    {
        _cc_cache_valid = false; /* stay honest: next reader goes to disk */
        COS_LOG_W("Snapshot write failed (%d) - SD removed?", (int)ret);
    }
    return ret;
}

cos_result_t cos_cc_snapshot_store_brightness(uint8_t percent)
{
    char v[8];
    snprintf(v, sizeof(v), "%u", (unsigned)percent);
    return cos_cc_snapshot_store_kv(_CC_KEY_BRIGHTNESS, v);
}

cos_result_t cos_cc_snapshot_store_bt(bool enabled)
{
    return cos_cc_snapshot_store_kv(_CC_KEY_BT, enabled ? "1" : "0");
}

cos_result_t cos_cc_snapshot_store_wifi(bool enabled)
{
    return cos_cc_snapshot_store_kv(_CC_KEY_WIFI, enabled ? "1" : "0");
}

cos_result_t cos_cc_snapshot_store_power(cos_cc_power_mode_t mode)
{
    const char *ps = mode == COS_CC_POWER_BEAST ? _CC_POWER_STR_BEAST :
                     mode == COS_CC_POWER_SAVE  ? _CC_POWER_STR_SAVE :
                                                  _CC_POWER_STR_SMART;
    return cos_cc_snapshot_store_kv(_CC_KEY_POWER, ps);
}

/* ---------------------------------------------------------------- */
/* Capture / restore                                                 */
/* ---------------------------------------------------------------- */

static cos_cc_power_mode_t _cc_live_power_mode(void)
{
#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
    if (cos_power_save_is_active())
        return COS_CC_POWER_SAVE;
    if (cos_beast_mode_is_active())
        return COS_CC_POWER_BEAST;
#endif
    return COS_CC_POWER_SMART;
}

cos_result_t cos_cc_snapshot_capture_now(void)
{
    if (!cos_cc_snapshot_storage_available())
    {
        COS_LOG_D("Snapshot skipped: no real SD card");
        return COS_OK;
    }

    cos_cc_snapshot_t snap;
    _cc_snapshot_reset(&snap);

    double b = cos_config_get_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, 50);
    if (b < 0)
        b = 0;
    if (b > _CC_SNAP_BRIGHTNESS_MAX)
        b = _CC_SNAP_BRIGHTNESS_MAX;
    snap.brightness     = (uint8_t)b;
    snap.has_brightness = true;

    snap.bt       = cos_config_get_bool(COS_CONFIG_KEY_BLUETOOTH_BOOL, false);
    snap.has_bt   = true;
    snap.wifi     = cos_net_wifi_is_enabled();
    snap.has_wifi = true;
    snap.power    = _cc_live_power_mode();
    snap.has_power = true;

    char text[_CC_SNAP_BUF_SIZE];
    int n = cos_cc_snapshot_format(&snap, text, sizeof(text));
    if (n <= 0)
        return COS_ERR_JSON_ERROR;

    /* Sleep paths call this with the UI already torn down: bypass the deferred
     * writer so the bytes are on the card before the rails go down. */
    cos_result_t ret = cos_storage_write_file_immediate(COS_CC_SNAPSHOT_FILE, text, (size_t)n);
    if (ret == COS_OK)
    {
        snprintf(_cc_cache, sizeof(_cc_cache), "%s", text);
        _cc_cache_valid = true;
        COS_LOG_I("Snapshot saved: brightness=%u bt=%d wifi=%d power=%d",
                  (unsigned)snap.brightness, snap.bt, snap.wifi, (int)snap.power);
    }
    else
    {
        _cc_cache_valid = false;
        COS_LOG_W("Snapshot save failed (%d)", (int)ret);
    }
    return ret;
}

cos_result_t cos_cc_snapshot_restore_now(void)
{
    cos_cc_snapshot_t snap;
    if (!cos_cc_snapshot_load(&snap, true))
    {
        COS_LOG_D("Snapshot restore skipped: no snapshot");
        return COS_ERR_FILE_ERROR;
    }

    cos_result_t ret = COS_OK;

    /* 1. Brightness: apply immediately (no fade - we are restoring, not
     *    animating) and mirror into cfg.json so the regular boot path and the
     *    control-center slider agree with what the user just got. */
    if (snap.has_brightness)
    {
        cos_display_set_brightness(snap.brightness, COS_DISPLAY_DURATION_OFF, false);
        if ((uint8_t)cos_config_get_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, 50) !=
            snap.brightness)
        {
            cos_config_set_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, snap.brightness);
        }
        COS_LOG_I("Snapshot restore: brightness=%u", (unsigned)snap.brightness);
    }

    /* 2. Radios. Order matters: power mode may force both off, so apply the
     *    switches first and the power mode last.
     *
     *    NOTE: Wi-Fi is intentionally NOT auto-connected here. Boot already
     *    runs the deferred connect-from-history path; connecting synchronously
     *    from the UI thread would freeze the first frame. */
    if (snap.has_wifi && snap.wifi != cos_net_wifi_is_enabled())
    {
        cos_result_t r = cos_net_wifi_set_enabled(snap.wifi);
        if (r != COS_OK)
            ret = r;
        COS_LOG_I("Snapshot restore: wifi=%d (result %d)", snap.wifi, (int)r);
    }

    if (snap.has_bt && snap.bt != cos_net_bt_is_enabled())
    {
        cos_result_t r = cos_net_bt_set_enabled(snap.bt);
        if (r != COS_OK)
            ret = r;
        COS_LOG_I("Snapshot restore: bt=%d (result %d)", snap.bt, (int)r);
    }

    /* 3. Power mode: enter is idempotent and mutually exclusive, so a stale
     *    "smart" never needs an exit. */
    if (snap.has_power)
    {
#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0
        if (snap.power == COS_CC_POWER_SAVE && !cos_power_save_is_active())
        {
            cos_power_save_enter();
            COS_LOG_I("Snapshot restore: power=powersave");
        }
        else if (snap.power == COS_CC_POWER_BEAST && !cos_beast_mode_is_active())
        {
            cos_beast_mode_enter();
            COS_LOG_I("Snapshot restore: power=beastmode");
        }
        else
        {
            COS_LOG_I("Snapshot restore: power=%d (already in effect)", (int)snap.power);
        }
#endif
    }

    return ret;
}

/* ---------------------------------------------------------------- */
/* Diagnostics                                                       */
/* ---------------------------------------------------------------- */

void cos_cc_snapshot_dump(char *out, size_t cap)
{
    if (!out || cap == 0)
        return;

    cos_cc_snapshot_t snap;
    bool found = cos_cc_snapshot_load(&snap, true);
    int n = snprintf(out, cap,
                     "cc snapshot: path=%s sd=%d found=%d cache=%d\n",
                     COS_CC_SNAPSHOT_FILE,
                     (int)cos_cc_snapshot_storage_available(),
                     (int)found, (int)_cc_cache_valid);
    if (n < 0 || (size_t)n >= cap || !found)
        return;

    if (snap.has_brightness)
        n += snprintf(out + n, cap - (size_t)n, "  brightness=%u\n",
                      (unsigned)snap.brightness);
    if (snap.has_bt)
        n += snprintf(out + n, cap - (size_t)n, "  bt=%d\n", snap.bt ? 1 : 0);
    if (snap.has_wifi)
        n += snprintf(out + n, cap - (size_t)n, "  wifi=%d\n", snap.wifi ? 1 : 0);
    if (snap.has_power && (size_t)n < cap)
        snprintf(out + n, cap - (size_t)n, "  power=%d\n", (int)snap.power);
}
