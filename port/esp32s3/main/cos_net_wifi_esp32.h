/**
 * @file eos_net_wifi_esp32.h
 * @brief Real Wi-Fi backend for ESP32 (ESP-IDF esp_wifi_*)
 *
 * Platform layer under eos_net_wifi.c (Core System Service). Implements real
 * scanning / connecting via the ESP-IDF Wi-Fi stack. The simulator keeps its
 * own stub backend; this file is only built for EOS_PLATFORM_ESP32.
 */

#ifndef EOS_NET_WIFI_ESP32_H
#define EOS_NET_WIFI_ESP32_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "eos_net_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lazy-init the ESP-IDF Wi-Fi stack (NVS + event loop + netif + wifi).
 *        Idempotent; safe to call before every scan/connect.
 */
esp_err_t eos_net_wifi_esp32_init(void);

/**
 * @brief Stop the ESP-IDF Wi-Fi driver without deinitializing its allocation.
 *
 * This releases the radio and its ESP_PM_APB_FREQ_MAX lock while preserving
 * the netif/event setup, so a later eos_net_wifi_esp32_init() can restart the
 * station without the fragile reinitialization path.
 */
esp_err_t eos_net_wifi_esp32_stop(void);

/**
 * @brief Early-init hook: called by app_main BEFORE eos_init(), while internal
 *        RAM is still contiguous (esp_wifi_init needs internal DMA buffers that
 *        fail with ESP_ERR_NO_MEM once the heap is fragmented at runtime).
 *        Memory-gated internally: skips (keeps lazy init) when the largest free
 *        internal block is below a threshold, so it never starves the LVGL UI.
 *        Failure/skip is non-fatal — scan/connect still retry lazily.
 */
esp_err_t eos_net_wifi_esp32_early_init(void);

/**
 * @brief Perform a real active scan and fill out_aps (sorted by RSSI desc).
 */
esp_err_t eos_net_wifi_esp32_scan(eos_wifi_ap_t *out_aps,
                                  uint32_t max_aps, uint32_t *out_count);

/**
 * @brief Connect to an AP and wait for IP (blocking, ~15s timeout).
 *        On success ip/rssi are filled from the real association.
 */
esp_err_t eos_net_wifi_esp32_connect(const char *ssid, const char *password,
                                     char *ip, size_t ip_len, int8_t *rssi);

/** @brief Disconnect the station. */
esp_err_t eos_net_wifi_esp32_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_NET_WIFI_ESP32_H */
