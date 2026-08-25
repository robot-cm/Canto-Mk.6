/**
 * @file eos_net_wifi_esp32.c
 * @brief Real Wi-Fi backend for ESP32 (ESP-IDF esp_wifi_*)
 *
 * Implements the platform half of eos_net_wifi.c:
 *   - lazy init: NVS + default event loop + netif + esp_wifi_init + STA mode
 *   - real active scan (esp_wifi_scan_start / get_ap_records), RSSI-sorted
 *   - real connect: esp_wifi_connect + wait for GOT_IP event (blocking)
 *
 * NOTE: scan/connect are synchronous (called from the UI scan callback).
 * A full-channel active scan blocks ~1-2s; acceptable for the settings page.
 * A future async migration should move this to service_task + completion
 * callback, per AGENTS.md §19 (no long blocking I/O on the UI task).
 */

#include "eos_net_wifi_esp32.h"

/* ESP-IDF */
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"

/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG "NetWifiESP32"

/* Connect sync bits */
#define WIFI_CONN_GOT_IP  BIT0
#define WIFI_CONN_FAIL    BIT1

/* ------------------------------------------------------------------ */
/*  static state                                                       */
/* ------------------------------------------------------------------ */
static bool            s_wifi_inited = false;
static esp_netif_t    *s_sta_netif   = NULL;
static EventGroupHandle_t s_conn_evt = NULL;
static char            s_got_ip[16]  = "";

/* ------------------------------------------------------------------ */
/*  event handlers                                                     */
/* ------------------------------------------------------------------ */
static void _wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_conn_evt) {
            xEventGroupSetBits(s_conn_evt, WIFI_CONN_FAIL);
        }
        ESP_LOGW(TAG, "STA disconnected");
    }
}

static void _ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_got_ip, sizeof(s_got_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_got_ip);
        if (s_conn_evt) {
            xEventGroupSetBits(s_conn_evt, WIFI_CONN_GOT_IP);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  init                                                               */
/* ------------------------------------------------------------------ */
esp_err_t eos_net_wifi_esp32_init(void)
{
    if (s_wifi_inited) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "init: step1 nvs_flash_init (task=%s prio=%d)",
             pcTaskGetName(NULL), (int)uxTaskPriorityGet(NULL));
    /* 1. NVS (Wi-Fi calibration / PMK cache) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. default event loop (ignore "already created") */
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. netif + station interface
     * 注意:netif 创建后若后续任何一步失败,必须销毁它,否则其 key("sta")
     * 仍注册在 esp_netif 中,下次重试 create_default_wifi_sta 会触发
     * esp_netif_new_api 断言崩溃。所有失败路径统一 goto err_after_wifi 回滚。 */
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        ESP_LOGE(TAG, "create default wifi sta netif failed");
        return ESP_FAIL;
    }

    /* 4. wifi stack
     * 内存瘦身:XIAO internal RAM 仅 512KB。WIFI_INIT_CONFIG_DEFAULT 默认
     * 静态 RX/TX 16+16、AMPDU RX 窗口 16、cache TX 32,需 ~150-200KB
     * 连续 internal RAM(静态 buffer 无法放 PSRAM),系统运行一段时间后
     * 碎片化即 esp_wifi_init 返回 ESP_ERR_NO_MEM。
     * 这里把静态 buffer 压到最小、关闭 AMPDU/AMSDU,动态 buffer 经
     * CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 自动落到 8MB PSRAM。 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num   = 4;
    cfg.static_tx_buf_num   = 0;
    cfg.tx_buf_type         = 1; /* dynamic TX(PSRAM) */
    cfg.dynamic_rx_buf_num  = 16;
    cfg.dynamic_tx_buf_num  = 8;
    cfg.cache_tx_buf_num    = 0;
    cfg.rx_mgmt_buf_num     = 5;
    cfg.mgmt_sbuf_num       = 16;
    cfg.ampdu_rx_enable     = 0;
    cfg.ampdu_tx_enable     = 0;
    cfg.amsdu_tx_enable     = 0;
    cfg.rx_ba_win           = 4;
    cfg.nvs_enable          = 1;
    ESP_LOGI(TAG, "init: step4 esp_wifi_init ...");
    ret = esp_wifi_init(&cfg);
    ESP_LOGI(TAG, "init: step5 esp_wifi_init -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s (internal RAM 不足,已回滚,下次可重试)",
                 esp_err_to_name(ret));
        goto err_after_wifi;
    }
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     _wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register wifi event handler failed: %s", esp_err_to_name(ret));
        goto err_after_wifi;
    }
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     _ip_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register ip event handler failed: %s", esp_err_to_name(ret));
        goto err_after_wifi;
    }

    /* 5. STA mode + start radio */
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        goto err_after_wifi;
    }
    ESP_LOGI(TAG, "init: step6 esp_wifi_start ...");
    ret = esp_wifi_start();
    ESP_LOGI(TAG, "init: step7 esp_wifi_start -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        goto err_after_wifi;
    }

    s_conn_evt = xEventGroupCreate();
    if (!s_conn_evt) {
        ESP_LOGE(TAG, "event group create failed");
        goto err_after_wifi;
    }

    s_wifi_inited = true;
    ESP_LOGI(TAG, "ESP-IDF Wi-Fi stack ready (STA, slim buffers)");
    return ESP_OK;

err_after_wifi:
    /* 统一回滚:注销事件处理器 → deinit Wi-Fi → 销毁 netif。
     * 全部复位后 s_wifi_inited 仍为 false,下次 init 从零开始,
     * 不会出现 netif key 重复 / 资源泄漏。 */
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, _ip_event_handler);
    esp_wifi_deinit();
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  auth mapping                                                       */
/* ------------------------------------------------------------------ */
static eos_wifi_auth_t _map_auth(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN: return EOS_WIFI_AUTH_OPEN;
    case WIFI_AUTH_WEP:  return EOS_WIFI_AUTH_WEP;
    case WIFI_AUTH_WPA_PSK:
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return EOS_WIFI_AUTH_WPA;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WAPI_PSK:
        return EOS_WIFI_AUTH_WPA3;
    default:
        return EOS_WIFI_AUTH_WPA;
    }
}

/* ------------------------------------------------------------------ */
/*  scan                                                               */
/* ------------------------------------------------------------------ */
esp_err_t eos_net_wifi_esp32_scan(eos_wifi_ap_t *out_aps,
                                  uint32_t max_aps, uint32_t *out_count)
{
    if (!out_aps || !out_count || max_aps == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = eos_net_wifi_esp32_init();
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_scan_config_t cfg = {
        .ssid       = NULL,
        .bssid      = NULL,
        .channel    = 0,                    /* all channels */
        .show_hidden = false,
        .scan_type  = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 80,         /* ms */
        .scan_time.active.max = 120,
    };
    ret = esp_wifi_scan_start(&cfg, true);  /* blocking scan */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "scan_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t ap_num = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_num);
    if (ret != ESP_OK) {
        esp_wifi_scan_stop();
        return ret;
    }
    if (ap_num == 0) {
        esp_wifi_scan_stop();
        *out_count = 0;
        return ESP_OK;
    }

    uint16_t cap = (ap_num > max_aps) ? (uint16_t)max_aps : ap_num;
    wifi_ap_record_t *recs = heap_caps_malloc(cap * sizeof(wifi_ap_record_t),
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!recs) {
        esp_wifi_scan_stop();
        return ESP_ERR_NO_MEM;
    }
    ret = esp_wifi_scan_get_ap_records(&cap, recs);
    esp_wifi_scan_stop();
    if (ret != ESP_OK) {
        heap_caps_free(recs);
        return ret;
    }

    uint32_t n = 0;
    for (uint16_t i = 0; i < cap; i++) {
        if (recs[i].ssid[0] == '\0') {
            continue;                       /* hidden AP */
        }
        eos_wifi_ap_t *ap = &out_aps[n];
        strncpy(ap->ssid, (const char *)recs[i].ssid, EOS_NET_WIFI_SSID_MAX);
        ap->ssid[EOS_NET_WIFI_SSID_MAX] = '\0';
        ap->rssi    = recs[i].rssi;
        ap->channel = recs[i].primary;
        ap->auth    = (uint8_t)_map_auth(recs[i].authmode);
        n++;
    }
    heap_caps_free(recs);

    /* Sort by RSSI, strongest first (insertion sort, n is small) */
    for (uint32_t i = 1; i < n; i++) {
        eos_wifi_ap_t key = out_aps[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && out_aps[j].rssi < key.rssi) {
            out_aps[j + 1] = out_aps[j];
            j--;
        }
        out_aps[j + 1] = key;
    }

    *out_count = n;
    ESP_LOGI(TAG, "scan done: %u AP(s) found", (unsigned)n);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  connect / disconnect                                               */
/* ------------------------------------------------------------------ */
esp_err_t eos_net_wifi_esp32_connect(const char *ssid, const char *password,
                                     char *ip, size_t ip_len, int8_t *rssi)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = eos_net_wifi_esp32_init();
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (password && password[0]) {
        strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    }
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Clear stale bits, then connect */
    xEventGroupClearBits(s_conn_evt, WIFI_CONN_GOT_IP | WIFI_CONN_FAIL);
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "connecting to \"%s\" ...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_conn_evt,
                                           WIFI_CONN_GOT_IP | WIFI_CONN_FAIL,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONN_GOT_IP) {
        if (ip && ip_len > 0) {
            strncpy(ip, s_got_ip, ip_len - 1);
            ip[ip_len - 1] = '\0';
        }
        if (rssi) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                *rssi = ap.rssi;
            }
        }
        return ESP_OK;
    }

    /* Failure or timeout: clean up */
    esp_wifi_disconnect();
    ESP_LOGW(TAG, "connect failed or timeout (bits=0x%x)", (unsigned)bits);
    return ESP_FAIL;
}

esp_err_t eos_net_wifi_esp32_disconnect(void)
{
    esp_err_t ret = eos_net_wifi_esp32_init();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_disconnect();
    if (ret == ESP_OK || ret == ESP_ERR_WIFI_NOT_CONNECT) {
        return ESP_OK;
    }
    return ret;
}
