/**
 * @file cos_bt_esp32.c
 * @brief ESP32-S3 Bluetooth radio backend: NimBLE GAP advertising
 *
 * Strong implementation of cos_net_bt_backend_set_enabled() (declared in
 * cos_port.h), overriding the weak stub in src/port/cos_port.c. The Bluetooth
 * service (src/services/network/cos_net_bt.c) calls it whenever the radio is
 * toggled, so the device advertises itself over BLE and becomes discoverable
 * by phones as the configured name (default "Canto Mk.6").
 *
 * - enable  -> NimBLE stack init (once) + undirected connectable advertising
 *              (general discoverable, name in ADV data)
 * - disable -> advertising stopped
 *
 * NimBLE runs in its own FreeRTOS task; no LVGL calls are made from its
 * callbacks, so the UI thread is never blocked.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

/* ── 编译期可选:Bt 未使能时跳过整个 NimBLE 后端 ──────────────
 * 默认 sdkconfig(CONFIG_BT_ENABLED=n)下 esp_bt.h / NimBLE 头不可用,
 * 本文件必须能整体跳过;menuconfig 开启 BT 后自动恢复真实现。
 * 关闭分支:同签名空实现,main.c 的无条件调用与 cos_net_bt.c 的
 * 强/弱后端关系均不受影响。 */
#if !defined(CONFIG_BT_ENABLED) || !CONFIG_BT_ENABLED

#include "cos_core.h"

void cos_bt_esp32_early_init(void)
{
}

cos_result_t cos_net_bt_backend_set_enabled(bool enabled, const char *name)
{
    (void)enabled;
    (void)name;
    return COS_OK;
}

cos_result_t cos_net_bt_backend_power_down(void)
{
    return COS_OK;
}

#else /* CONFIG_BT_ENABLED=y:真实现 */

#include "esp_bt.h" /* esp_bt_controller_deinit:清理半初始化 controller */
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include "cos_core.h"

/* Macros and Definitions -------------------------------------*/
#define COS_BT_ESP_TAG "NetBtEsp"
#define COS_BT_NAME_MAX 32
#define COS_BT_INIT_STACK 4096

/* Variables --------------------------------------------------*/
static bool s_initialized = false;
static bool s_synced = false;  /* NimBLE host synced with controller */
static bool s_enabled = false; /* radio requested on */
static char s_name[COS_BT_NAME_MAX + 1] = {0};

/* Function Implementations -----------------------------------*/
static void _start_advertising(void);
static void _on_sync(void);
static void _on_reset(int reason);
static void _init_task(void *param);

/* NimBLE host FreeRTOS task (protocol stack runs here) */
static void _host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* 核心初始化:diagnose + nimble_port_init + GAP 回调注册 + host 任务。
 *
 * BLE controller 需要大块 internal|DMA 连续内存(em 表 / TX-RX buffer)。
 * 若在系统运行很久后(内部 RAM 已碎片化,largest 只有几 KB)才初始化,
 * nimble_port_init() 会因 ESP_ERR_NO_MEM 失败——所以必须在 app_main
 * 早期 internal RAM 尚连续时调用(cos_bt_esp32_early_init),开关只控制广播。
 *
 * 失败时做清理(esp_bt_controller_deinit):残留的 ROM/固件状态会与
 * WiFi coex 冲突(打开 WiFi 时卡死 → TG1WDT 复位)。
 * 返回 ESP_OK 表示栈已就绪(s_initialized=true)。
 */
static esp_err_t _bt_do_init(void)
{
    if (s_initialized)
        return ESP_OK;

    /* 诊断:分别打印 INTERNAL 与 INTERNAL|DMA 的碎片情况 */
    size_t f_i    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t l_i    = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t f_id   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    size_t l_id   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_LOGI(COS_BT_ESP_TAG,
             "controller init: INT free=%u largest=%u | INT|DMA free=%u largest=%u",
             (unsigned)f_i, (unsigned)l_i, (unsigned)f_id, (unsigned)l_id);

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        size_t fa_i  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t la_i  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        size_t fa_id = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        size_t la_id = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        ESP_LOGE(COS_BT_ESP_TAG, "nimble_port_init failed: %s "
                 "(INT free=%u largest=%u | INT|DMA free=%u largest=%u)",
                 esp_err_to_name(ret), (unsigned)fa_i, (unsigned)la_i,
                 (unsigned)fa_id, (unsigned)la_id);
        esp_err_t derr = esp_bt_controller_deinit();
        if (derr != ESP_OK)
            ESP_LOGW(COS_BT_ESP_TAG, "controller deinit after failed init: %s",
                     esp_err_to_name(derr));
        return ret;
    }

    ble_svc_gap_device_name_set(s_name);
    ble_svc_gap_init();
    ble_hs_cfg.sync_cb = _on_sync;
    ble_hs_cfg.reset_cb = _on_reset;

    nimble_port_freertos_init(_host_task);
    s_initialized = true;
    return ESP_OK;
}

/* 提前初始化入口:由 app_main 在 cos_init() 之前(internal RAM 连续时)调用。
 * 失败不致命:s_initialized 保持 false,用户打开蓝牙开关时 _init_task 会再试。 */
void cos_bt_esp32_early_init(void)
{
    if (s_initialized)
        return;

    esp_err_t ret = _bt_do_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(COS_BT_ESP_TAG, "early init deferred (%s); will retry on switch toggle",
                 esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(COS_BT_ESP_TAG,
             "early init OK: stack ready, advertising waits for the radio switch");
}

/* Low-priority init task: nimble_port_init() blocks until the controller is
 * ready, so it must not run on the LVGL/UI thread (cos_port.h constraint).
 * Kept as fallback when the early init was skipped or failed. */
static void _init_task(void *param)
{
    (void)param;
    if (_bt_do_init() != ESP_OK)
    {
        s_enabled = false;
        s_initialized = false;
    }
    vTaskDelete(NULL);
}

/* GAP event callback (NimBLE host task context) */
static int _gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI(COS_BT_ESP_TAG, "connected");
        }
        else
        {
            _start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(COS_BT_ESP_TAG, "disconnected, re-advertising");
        _start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        _start_advertising();
        break;
    default:
        break;
    }
    return 0;
}

/* Set ADV fields (name) and start undirected connectable advertising */
static void _start_advertising(void)
{
    if (!s_synced || !s_enabled)
        return;

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_name;
    fields.name_len = (uint8_t)strlen(s_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(COS_BT_ESP_TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; /* undirected connectable */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  /* general discoverable */
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(200);

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, _gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(COS_BT_ESP_TAG, "adv_start failed: %d", rc);
    }
    else
    {
        ESP_LOGI(COS_BT_ESP_TAG, "advertising as \"%s\"", s_name);
    }
}

/* Called by NimBLE when host syncs with the controller */
static void _on_sync(void)
{
    uint8_t addr_type;
    s_synced = (ble_hs_id_infer_auto(0, &addr_type) == 0);
    if (!s_synced)
    {
        ESP_LOGW(COS_BT_ESP_TAG, "could not infer own address");
        return;
    }
    if (s_enabled)
    {
        _start_advertising();
    }
}

static void _on_reset(int reason)
{
    s_synced = false;
    ESP_LOGW(COS_BT_ESP_TAG, "nimble reset, reason=%d", reason);
}

cos_result_t cos_net_bt_backend_set_enabled(bool enabled, const char *name)
{
    if (name && name[0])
    {
        strncpy(s_name, name, COS_BT_NAME_MAX);
        s_name[COS_BT_NAME_MAX] = '\0';
        /* early init 时 GAP 服务可能已注册:adv fields 用 s_name,但 GAP
         * Service 的 device-name 属性需单独同步,否则手机读到的名字是旧的 */
        if (s_initialized)
            ble_svc_gap_device_name_set(s_name);
    }

    if (!enabled)
    {
        s_enabled = false;
        if (s_synced)
        {
            ble_gap_adv_stop();
        }
        ESP_LOGI(COS_BT_ESP_TAG, "radio off");
        return COS_OK;
    }

    s_enabled = true;

    if (!s_initialized)
    {
        /* Kick off protocol stack init on a low-priority task. Advertising
         * starts automatically in _on_sync() once the host is ready. */
        BaseType_t ok = xTaskCreate(_init_task, "cos_bt_init", COS_BT_INIT_STACK,
                                    NULL, 1, NULL);
        if (ok != pdPASS)
        {
            s_enabled = false;
            return COS_ERR_NET_BT;
        }
    }
    else if (s_synced)
    {
        /* Already up: restart advertising with the (possibly new) name. */
        ble_gap_adv_stop();
        _start_advertising();
    }
    /* If the stack is still syncing, _on_sync() will start advertising. */

    return COS_OK;
}

cos_result_t cos_net_bt_backend_power_down(void)
{
    s_enabled = false;
    if (!s_initialized)
        return COS_OK;

    if (s_synced)
        ble_gap_adv_stop();

    /* ESP-IDF's NimBLE lifecycle requires stop before deinit. Deinit then
     * disables and deinitializes the controller, releasing the PM locks that
     * would otherwise prevent automatic Light-sleep. */
    int stop_ret = nimble_port_stop();
    if (stop_ret != 0)
    {
        ESP_LOGE(COS_BT_ESP_TAG, "nimble_port_stop failed: %d", stop_ret);
        return COS_ERR_NET_BT;
    }
    esp_err_t deinit_ret = nimble_port_deinit();
    if (deinit_ret != ESP_OK)
    {
        ESP_LOGE(COS_BT_ESP_TAG, "nimble_port_deinit failed: %s",
                 esp_err_to_name(deinit_ret));
        return COS_ERR_NET_BT;
    }

    s_initialized = false;
    s_synced = false;
    ESP_LOGI(COS_BT_ESP_TAG, "BLE controller deinitialized for power save");
    return COS_OK;
}

#endif /* CONFIG_BT_ENABLED */
