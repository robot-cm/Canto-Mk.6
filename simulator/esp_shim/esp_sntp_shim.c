/**
 * @file esp_sntp_shim.c
 * @brief Host stub for the ESP-IDF SNTP API (see third_party ESP-IDF esp_sntp.h).
 *
 * Why this exists:
 *   src/services/time/cos_service_time.c guards its whole NTP path with
 *   `#if !defined(COS_SIMULATOR) || COS_SIMULATOR == 0`, so esp_sntp_* calls
 *   are not compiled on the simulator — with one exception: the sync callback
 *   `_ntp_sync_cb()` is defined unconditionally and its last statement calls
 *   `esp_sntp_stop()`. That leaves exactly one undefined symbol at link time.
 *
 *   src/ is not allowed to change, so the symbol is satisfied here instead.
 *   The callback is never registered on the host (its only registration site
 *   `esp_sntp_set_time_sync_notification_cb()` is inside the guarded block),
 *   therefore this stub is unreachable at runtime — it is pure link glue.
 *
 * Real signature: void esp_sntp_stop(void);
 */

void esp_sntp_stop(void)
{
    /* no-op: no SNTP stack on the host */
}
