/**
 * @file cos_service_config.c
 * @brief System configuration service
 */

#include "cos_service_config.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "cos_storage_paths.h"
#include "cos_image.h"
#include "cos_msg_list.h"
#include "cos_lang.h"
#define COS_LOG_TAG "ConfigService"
#include "cos_log.h"
#include "cos_basic_widgets.h"
#include "cos_event.h"
#include "cos_service_display.h"
#include "cos_service_audio.h"
#include "cos_test.h"
#include "cos_version.h"
#include "cos_port.h"
#include "cos_swipe_panel.h"
#include "cos_app.h"
#include "cos_watchface.h"
#include "cos_theme.h"
#include "cos_pkg_mgr.h"
#include "cos_service_sensor.h"
#include "cos_service_storage.h"
#include "cos_mem.h"
#include "cos_service_haptic.h"
/* [DIAG] 内存诊断:崩溃点前打印 PSRAM / 内部 DMA 状态 */
#include "esp_heap_caps.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

static cos_result_t _cos_config_save_and_broadcast(cJSON *root)
{
    if (!root)
        return COS_ERR_JSON_ERROR;

    cos_result_t ret = cos_storage_json_save(COS_CONFIG_FILE_PATH, root);
    cJSON_Delete(root);

    if (ret == COS_OK)
    {
        cos_event_post(COS_EVENT_SYSTEM_CONFIG_UPDATE, NULL, NULL);
    }

    return ret;
}

cos_result_t cos_config_set_bool(const char *key, bool value)
{
    COS_CHECK_PTR_RETURN_VAL(key, COS_ERR_VAR_NULL);

    COS_LOG_I("Try set \"%s\" = \"%s\"", key, value ? "true" : "false");

    cJSON *root = cos_storage_json_load(COS_CONFIG_FILE_PATH);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
            return COS_ERR_MEM;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
        cJSON_SetBoolValue(item, value);
    else
        cJSON_AddBoolToObject(root, key, value);

    cos_result_t ret = _cos_config_save_and_broadcast(root);

    if (ret == COS_OK)
    {
        COS_LOG_I("Successfully set config item: %s=%s", key, value ? "true" : "false");
    }

    return ret;
}

cos_result_t cos_config_set_string(const char *key, const char *value)
{
    COS_CHECK_PTR_RETURN_VAL(key && value, COS_ERR_VAR_NULL);

    COS_LOG_I("Try set \"%s\" = \"%s\"", key, value);

    cJSON *root = cos_storage_json_load(COS_CONFIG_FILE_PATH);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
            return COS_ERR_MEM;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
        cJSON_SetValuestring(item, value);
    else
        cJSON_AddStringToObject(root, key, value);

    cos_result_t ret = _cos_config_save_and_broadcast(root);

    if (ret == COS_OK)
    {
        COS_LOG_I("Successfully set config item: %s=%s", key, value);
    }

    return ret;
}

cos_result_t cos_config_set_string_silent(const char *key, const char *value)
{
    COS_CHECK_PTR_RETURN_VAL(key && value, COS_ERR_VAR_NULL);

    /* NOTE: deliberately does NOT log `value` (secrets: proxy password, etc.) */
    cJSON *root = cos_storage_json_load(COS_CONFIG_FILE_PATH);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
            return COS_ERR_MEM;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
        cJSON_SetValuestring(item, value);
    else
        cJSON_AddStringToObject(root, key, value);

    return _cos_config_save_and_broadcast(root);
}

cos_result_t cos_config_set_number(const char *key, double value)
{
    COS_CHECK_PTR_RETURN_VAL(key, COS_ERR_VAR_NULL);

    COS_LOG_I("Try set \"%s\" = \"%g\"", key, value);

    cJSON *root = cos_storage_json_load(COS_CONFIG_FILE_PATH);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
            return COS_ERR_MEM;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
        cJSON_SetNumberValue(item, value);
    else
        cJSON_AddNumberToObject(root, key, value);

    cos_result_t ret = _cos_config_save_and_broadcast(root);

    if (ret == COS_OK)
    {
        COS_LOG_I("Successfully set config item: %s=%g", key, value);
    }

    return ret;
}

bool cos_config_get_bool(const char *key, bool default_value)
{
    return cos_storage_json_get_bool(COS_CONFIG_FILE_PATH, key, default_value);
}

char *cos_config_get_string(const char *key, const char *default_value)
{
    return cos_storage_json_get_string(COS_CONFIG_FILE_PATH, key, default_value);
}

double cos_config_get_number(const char *key, double default_value)
{
    return cos_storage_json_get_number(COS_CONFIG_FILE_PATH, key, default_value);
}

cos_result_t _create_default_cfg_json(const char *path)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return COS_ERR_JSON_ERROR;
    }

    cJSON_AddStringToObject(root, COS_CONFIG_KEY_DEVICE_NAME_STR, COS_CONFIG_DEFAULT_DEVICE_NAME);
    cJSON_AddStringToObject(root, COS_CONFIG_KEY_LANGUAGE_STR, COS_CONFIG_DEFAULT_LANG_STR);
    cJSON_AddStringToObject(root, COS_CONFIG_KEY_WATCHFACE_ID_STR, COS_CONFIG_DEFAULT_WATCHFACE_ID_STR);

    cJSON *app_order_array = cJSON_CreateArray();
    if (app_order_array)
        cJSON_AddItemToObject(root, COS_CONFIG_KEY_APP_ORDER_ARRAY, app_order_array);

    // Save file using storage service JSON API
    cos_result_t ret = cos_storage_json_save(path, root);

    cJSON_Delete(root);

    return ret;
}

void cos_service_config_init()
{
    COS_LOG_D("Init cos_config");
    cos_storage_mkdir_if_not_exist(COS_SYS_DIR);
    cos_storage_mkdir_if_not_exist(COS_CONFIG_DIR);

    cos_storage_mkdir_if_not_exist(COS_APP_DIR);
    cos_storage_mkdir_if_not_exist(COS_APP_INSTALLED_DIR);
    cos_storage_mkdir_if_not_exist(COS_APP_DATA_DIR);

    cos_storage_mkdir_if_not_exist(COS_WATCHFACE_DIR);
    cos_storage_mkdir_if_not_exist(COS_WATCHFACE_INSTALLED_DIR);
    cos_storage_mkdir_if_not_exist(COS_WATCHFACE_DATA_DIR);

    cos_storage_mkdir_if_not_exist(COS_SYS_RES_DIR);
    cos_storage_mkdir_if_not_exist(COS_SYS_RES_IMG_DIR);
    cos_storage_mkdir_if_not_exist(COS_SYS_RES_FONT_DIR);

    // Check and create default config file using storage service JSON API
    if (!cos_storage_is_file(COS_CONFIG_FILE_PATH))
    {
        if (_create_default_cfg_json(COS_CONFIG_FILE_PATH) != COS_OK)
        {
            COS_LOG_E("Create default config json failed");
            COS_LOG_E("[DIAG] PSRAM free=%u largest=%u | INT|DMA free=%u largest=%u",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
            /* SD/SPIFFS 是不可信/可故障存储:写失败绝不能 panic(Core 稳定性)。
             * 降级为内存默认配置继续启动,后续各 get_* 返回默认值。 */
            COS_LOG_W("Config file unavailable - continuing with in-memory defaults");
        }
    }
    else
    {
        // Check if config file is valid
        cJSON *root = cos_storage_json_load(COS_CONFIG_FILE_PATH);
        if (!root)
        {
            if (_create_default_cfg_json(COS_CONFIG_FILE_PATH) != COS_OK)
            {
                COS_LOG_E("Create default config json failed");
                COS_LOG_W("Config file invalid & recreate failed - using in-memory defaults");
            }
        }
        else
        {
            cJSON_Delete(root);
        }
    }

    /************************** Load system settings **************************/
    if (cos_config_get_bool(COS_CONFIG_KEY_BLUETOOTH_BOOL, false))
    {
        cos_bluetooth_enable();
        COS_LOG_I("Bluetooth enable");
    }
    else
    {
        cos_bluetooth_disable();
        COS_LOG_I("Bluetooth disable");
    }

    uint8_t brightness = (uint8_t)cos_config_get_number(COS_CONFIG_KEY_DISPLAY_BRIGHTNESS_NUMBER, 50);
    if (brightness < COS_DISPLAY_BRIGHTNESS_MIN || brightness > COS_DISPLAY_BRIGHTNESS_MAX)
        brightness = 50;
    cos_display_set_brightness(brightness, COS_DISPLAY_DURATION_OFF, false);
    COS_LOG_I("Display brightness set: %d", brightness);

    bool mute = cos_config_get_bool(COS_CONFIG_KEY_MUTE_BOOL, false);
    if (mute)
    {
        cos_service_audio_set_mute(true);
        COS_LOG_I("Silent mode ON");
    }
    else
    {
        uint8_t volume = (uint8_t)cos_config_get_number(COS_CONFIG_KEY_SPEAKER_VOLUME_NUMBER, 20);
        cos_service_audio_set_volume(volume);
        COS_LOG_I("Volume: %d", volume);
    }

    uint8_t strength =
        (uint8_t)cos_config_get_number(COS_CONFIG_KEY_VIBRATOR_STRENGTH_NUMBER, COS_HAPTIC_STRENGTH_NORMAL);
    cos_haptic_set_strength(strength);
    COS_LOG_I("Vibrator strength: %d", strength);
}

cos_result_t cos_config_add_item(const char *key, const char *value)
{
    if (!key || !value)
    {
        COS_LOG_E("Invalid parameters: key or value is NULL");
        return COS_ERR_VAR_NULL;
    }

    cJSON *root = cos_storage_json_load(COS_CONFIG_FILE_PATH);
    if (!root)
        return COS_ERR_FILE_ERROR;

    if (cJSON_HasObjectItem(root, key))
    {
        COS_LOG_W("Key '%s' already exists in config", key);
        cJSON_Delete(root);
        return COS_ERR_JSON_ERROR;
    }

    cJSON_AddStringToObject(root, key, value);

    return _cos_config_save_and_broadcast(root);
}

cJSON *cos_config_get_json(const char *key)
{
    return cos_storage_json_get_json(COS_CONFIG_FILE_PATH, key);
}

cos_result_t cos_config_set_json(const char *key, cJSON *json_value)
{
    COS_CHECK_PTR_RETURN_VAL(key && json_value, COS_ERR_VAR_NULL);

    COS_LOG_I("Try set JSON \"%s\"", key);

    cJSON *root = cos_storage_json_load(COS_CONFIG_FILE_PATH);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
            return COS_ERR_MEM;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
        cJSON_ReplaceItemInObject(root, key, json_value);
    else
        cJSON_AddItemToObject(root, key, json_value);

    cos_result_t ret = _cos_config_save_and_broadcast(root);

    if (ret == COS_OK)
    {
        COS_LOG_I("Successfully set JSON config item: %s", key);
    }

    return ret;
}
