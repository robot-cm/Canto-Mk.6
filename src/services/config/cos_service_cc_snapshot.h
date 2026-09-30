/**
 * @file eos_service_cc_snapshot.h
 * @brief Control-Center settings snapshot on the SD card (``/sdcard/history/cc/``)
 *
 * Four Control-Center settings — display brightness, Bluetooth switch, Wi-Fi
 * switch and the power mode (beast / power-save / smart) — are mirrored into a
 * tiny key=value text file on the SD card:
 *
 *     /sdcard/history/cc/settings.txt
 *
 * Why a second store next to ``/.sys/config/cfg.json``:
 *   - cfg.json lives on the mounted volume, which silently falls back to the
 *     internal SPIFFS partition when no card is present. A snapshot on the
 *     *real* SD card keeps the user's choice when the card is moved between
 *     devices, survives a reflash of the internal partition, and is trivial to
 *     inspect with a card reader.
 *   - It is flushed right before deep sleep / power-off / L2 standby, so the
 *     last UI interaction is never lost even if the deferred config writer had
 *     not reached the card yet.
 *
 * Fallback: when no real SD card is mounted the snapshot layer is completely
 * transparent — writes are dropped (RAM state stays authoritative) and reads
 * answer "no snapshot", so every consumer keeps using the existing cfg.json
 * path. Nothing here ever blocks boot or panics: storage failures are logged
 * and ignored.
 *
 * The file is deliberately human readable/editable::
 *
 *     brightness=42
 *     bt=1
 *     wifi=0
 *     power=beast
 */

#ifndef EOS_SERVICE_CC_SNAPSHOT_H
#define EOS_SERVICE_CC_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include "eos_core.h"

/* Public macros ----------------------------------------------*/
/** @brief Snapshot directory on the SD card */
#define EOS_CC_SNAPSHOT_DIR  "/sdcard/history/cc"
/** @brief Snapshot file inside EOS_CC_SNAPSHOT_DIR */
#define EOS_CC_SNAPSHOT_FILE EOS_CC_SNAPSHOT_DIR "/settings.txt"

/* Public typedefs --------------------------------------------*/

/**
 * @brief Power mode as shown by the Control Center
 *
 * ``EOS_CC_POWER_SMART`` is the system default: neither the power-save nor the
 * beast-mode service is active (see eos_service_beast_mode.h).
 */
typedef enum
{
    EOS_CC_POWER_SMART = 0, /**< 智能模式(默认:DFS 80-160MHz) */
    EOS_CC_POWER_SAVE,      /**< 省电模式 */
    EOS_CC_POWER_BEAST      /**< 性能模式 */
} eos_cc_power_mode_t;

/**
 * @brief Decoded snapshot contents
 *
 * Every field carries an ``has_*`` flag so "missing line" stays distinct from
 * "value is 0/false" — a partially written file must never reset a setting to
 * its default.
 */
typedef struct
{
    bool    has_brightness;
    uint8_t brightness;      /**< 0..100 (percent) */

    bool    has_bt;
    bool    bt;              /**< Bluetooth switch */

    bool    has_wifi;
    bool    wifi;            /**< Wi-Fi switch */

    bool    has_power;
    eos_cc_power_mode_t power; /**< eos_cc_power_mode_t */
} eos_cc_snapshot_t;

/* Public function prototypes --------------------------------*/

/** @brief Register the log tag / reset state. Safe to call more than once. */
void eos_cc_snapshot_init(void);

/**
 * @brief Probe whether the mounted volume is a real SD card
 *
 * The board layer exposes ``board_sd_is_real()``; when that hook is absent
 * (simulator, or a build without USB-MSC support) the probe falls back to a
 * writability test on EOS_CC_SNAPSHOT_DIR.
 *
 * @return true when a real SD card is writable and snapshots are enabled
 */
bool eos_cc_snapshot_storage_available(void);

/**
 * @brief Drop any cached probe/snapshot state (call when the card is swapped)
 */
void eos_cc_snapshot_invalidate(void);

/**
 * @brief Serialize one decoded snapshot to key=value text
 *
 * Exposed for tests.
 *
 * @param snap Decoded snapshot (only ``has_*`` fields are written)
 * @param buf  Destination buffer
 * @param cap  Destination capacity
 * @return Number of bytes that would be written, or -1 on invalid arguments
 */
int eos_cc_snapshot_format(const eos_cc_snapshot_t *snap, char *buf, size_t cap);

/**
 * @brief Parse key=value text produced by eos_cc_snapshot_format()
 *
 * Unknown keys and malformed lines are ignored, so the file stays forward and
 * backward compatible.
 *
 * @param text NUL-terminated text
 * @param snap Destination (fields reset to "missing" first)
 * @return true when the text was non-empty
 */
bool eos_cc_snapshot_parse(const char *text, eos_cc_snapshot_t *snap);

/**
 * @brief Read the snapshot from the SD card
 *
 * @param snap       Destination (fields reset to "missing" first)
 * @param force_sync Bypass the in-RAM cache (use after deep-sleep wake / when
 *                   sleeping, where the card may have been swapped)
 * @return true when a snapshot file was found and parsed
 */
bool eos_cc_snapshot_load(eos_cc_snapshot_t *snap, bool force_sync);

/**
 * @brief Write one setting into the snapshot file
 *
 * Reads the current file, updates the single key and writes it back through
 * ``eos_storage_write_file_immediate()``. No-op (returns EOS_OK) when no real
 * SD card is present, so callers can call it unconditionally.
 *
 * @param key        One of "brightness" / "bt" / "wifi" / "power"
 * @param value_text Textual value (e.g. "42", "1", "beast")
 * @return EOS_OK when written or intentionally skipped
 */
eos_result_t eos_cc_snapshot_store_kv(const char *key, const char *value_text);

/** @brief Convenience wrappers for the four supported keys */
eos_result_t eos_cc_snapshot_store_brightness(uint8_t percent);
eos_result_t eos_cc_snapshot_store_bt(bool enabled);
eos_result_t eos_cc_snapshot_store_wifi(bool enabled);
eos_result_t eos_cc_snapshot_store_power(eos_cc_power_mode_t mode);

/**
 * @brief Snapshot all four settings from the live services / config
 *
 * Intended for the sleep paths (deep sleep, power-off, L2 standby) and for
 * explicit "persist now" requests.
 *
 * @return EOS_OK unless the underlying write failed
 */
eos_result_t eos_cc_snapshot_capture_now(void);

/**
 * @brief Restore the four settings from the snapshot
 *
 * Applies brightness immediately, flips Bluetooth / Wi-Fi through their
 * services and enters the recorded power mode. Values that the snapshot does
 * not contain are left untouched, so existing cfg.json keeps working.
 *
 * @return EOS_OK when applied; EOS_ERR when no snapshot was available
 */
eos_result_t eos_cc_snapshot_restore_now(void);

/** @brief Append a diagnostic line about snapshot state to @p out */
void eos_cc_snapshot_dump(char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SERVICE_CC_SNAPSHOT_H */
