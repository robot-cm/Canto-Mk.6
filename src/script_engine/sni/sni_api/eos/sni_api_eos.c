/**
 * @file sni_api_eos.c
 * @brief Canto Mk.6 API
 */

#include "sni_api_eos.h"

/* Includes ---------------------------------------------------*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "lvgl.h"
#include "sni_type_bridge.h"
#include "sni_types.h"
#include "sni_api_export.h"
#include "eos_log.h"
#include "eos_mem.h"
#include "script_engine_core.h"
#include "eos_font.h"
#include "eos_activity.h"
#include "eos_service_time.h"
#include "eos_app.h"
#include "eos_watchface.h"
#include "eos_service_storage.h"
#include "eos_app_header.h"
#include "eos_files.h"                 /* P0.5 相册: eos.app.openFiles -> 原生文件管理器 */
#include "eos_input_page.h"            /* 笔记: eos.ime.open -> 系统输入页(键盘) */
#include "spm.h"                       /* 笔记: ime 回调经 spm_call 调 JS */
#include "sni_callback_runtime.h"      /* 笔记: sni_cb_get_context */
#include "eos_ww_clock_hand.h"
#include "ui/system/eos_round_clip.h"   /* eos_round_clip() exposed to JS */
#include "sni_api_eos_permission.h"
/* Macros and Definitions -------------------------------------*/
#define EOS_API_NAME "eos"
#define CONSOLE_LOG_TAG script_engine_get_current_script_id()
/* Variables --------------------------------------------------*/
static jerry_value_t eos_api_obj;

typedef enum
{
    EOS_CONSOLE_LEVEL_LOG,
    EOS_CONSOLE_LEVEL_ERROR,
    EOS_CONSOLE_LEVEL_WARN,
    EOS_CONSOLE_LEVEL_DEBUG,
} eos_console_level_t;

static bool sni_api_eos_to_c_string(jerry_value_t js_val, char **out_str)
{
    if (!out_str || !jerry_value_is_string(js_val))
    {
        return false;
    }

    jerry_size_t str_len = jerry_string_size(js_val, JERRY_ENCODING_UTF8);
    char *str = eos_malloc(str_len + 1);
    if (!str)
    {
        return false;
    }

    jerry_string_to_buffer(js_val, JERRY_ENCODING_UTF8, (jerry_char_t *)str, str_len);
    str[str_len] = '\0';

    *out_str = str;
    return true;
}

static char *sni_api_eos_get_assets_file_str(jerry_value_t js_val)
{
    char *src = NULL;
    char path[EOS_FS_PATH_MAX];
    char *ret = NULL;
    const char *script_id = NULL;

    if (!sni_api_eos_to_c_string(js_val, &src))
    {
        return NULL;
    }

    script_id = script_engine_get_current_script_id();
    if (!script_id)
    {
        eos_free(src);
        return NULL;
    }

    if (script_engine_get_current_script_type() == SCRIPT_TYPE_APPLICATION)
    {
        snprintf(path, sizeof(path), EOS_APP_INSTALLED_DIR "%s/assets/%s", script_id, src);
    }
    else if (script_engine_get_current_script_type() == SCRIPT_TYPE_WATCHFACE)
    {
        snprintf(path, sizeof(path), EOS_WATCHFACE_INSTALLED_DIR "%s/assets/%s", script_id, src);
    }
    else
    {
        eos_free(src);
        return NULL;
    }

    eos_free(src);

    if (!eos_storage_is_file(path))
    {
        return NULL;
    }

    ret = eos_malloc(strlen(path) + 1);
    if (!ret)
    {
        return NULL;
    }

    strcpy(ret, path);
    return ret;
}

static bool sni_api_eos_config_write_to_file(cJSON *root)
{
    char *json_str = NULL;
    char config_file_path[EOS_FS_PATH_MAX];
    bool ret;

    if (!root)
    {
        return false;
    }

    json_str = cJSON_PrintUnformatted(root);
    if (!json_str)
    {
        return false;
    }

    if (script_engine_get_current_script_type() == SCRIPT_TYPE_APPLICATION)
    {
        snprintf(config_file_path,
                 sizeof(config_file_path),
                 EOS_APP_DATA_DIR "%s",
                 script_engine_get_current_script_id());
        eos_storage_mkdir_if_not_exist(config_file_path);
        snprintf(config_file_path,
                 sizeof(config_file_path),
                 EOS_APP_DATA_DIR "%s/config.json",
                 script_engine_get_current_script_id());
    }
    else if (script_engine_get_current_script_type() == SCRIPT_TYPE_WATCHFACE)
    {
        snprintf(config_file_path,
                 sizeof(config_file_path),
                 EOS_WATCHFACE_DATA_DIR "%s",
                 script_engine_get_current_script_id());
        eos_storage_mkdir_if_not_exist(config_file_path);
        snprintf(config_file_path,
                 sizeof(config_file_path),
                 EOS_WATCHFACE_DATA_DIR "%s/config.json",
                 script_engine_get_current_script_id());
    }
    else
    {
        cJSON_free(json_str); /* cJSON 分配,勿用 eos_free(会错位解析头导致系统堆崩溃) */
        return false;
    }

    ret = (eos_storage_write_file(config_file_path, json_str, strlen(json_str)) == EOS_OK);

    cJSON_free(json_str); /* cJSON 分配,勿用 eos_free */
    return ret;
}

static cJSON *sni_api_eos_config_load_from_file(void)
{
    char config_file_path[EOS_FS_PATH_MAX];
    char *data = NULL;
    cJSON *root = NULL;

    if (script_engine_get_current_script_type() == SCRIPT_TYPE_APPLICATION)
    {
        eos_storage_mkdir_if_not_exist(EOS_APP_DATA_DIR);
        snprintf(config_file_path,
                 sizeof(config_file_path),
                 EOS_APP_DATA_DIR "%s/config.json",
                 script_engine_get_current_script_id());
    }
    else if (script_engine_get_current_script_type() == SCRIPT_TYPE_WATCHFACE)
    {
        eos_storage_mkdir_if_not_exist(EOS_WATCHFACE_DATA_DIR);
        snprintf(config_file_path,
                 sizeof(config_file_path),
                 EOS_WATCHFACE_DATA_DIR "%s/config.json",
                 script_engine_get_current_script_id());
    }
    else
    {
        return NULL;
    }

    if (!eos_storage_is_file(config_file_path))
    {
        return cJSON_CreateObject();
    }

    data = eos_storage_read_file(config_file_path);

    if (!data)
    {
        return cJSON_CreateObject();
    }

    root = cJSON_Parse(data);
    eos_free(data);

    if (!root)
    {
        return cJSON_CreateObject();
    }

    return root;
}

static jerry_value_t sni_api_eos_console_write(const jerry_value_t args_p[],
                                               const jerry_length_t args_count,
                                               eos_console_level_t level)
{
    const char *str;

    if (args_count < 1)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    if (!jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Invalid argument type");
    }

    if (!sni_tb_js2c(args_p[0], SNI_T_STRING, &str))
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    switch (level)
    {
        case EOS_CONSOLE_LEVEL_LOG:
            EOS_LOG_I("[%s] %s", CONSOLE_LOG_TAG, str);
            break;
        case EOS_CONSOLE_LEVEL_ERROR:
            EOS_LOG_E("[%s] %s", CONSOLE_LOG_TAG, str);
            break;
        case EOS_CONSOLE_LEVEL_WARN:
            EOS_LOG_W("[%s] %s", CONSOLE_LOG_TAG, str);
            break;
        case EOS_CONSOLE_LEVEL_DEBUG:
            EOS_LOG_D("[%s] %s", CONSOLE_LOG_TAG, str);
            break;
        default:
            eos_free((void *)str);
            return sni_api_throw_error("Invalid console log level");
    }

    eos_free((void *)str);

    return jerry_undefined();
}
/* Function Implementations -----------------------------------*/

jerry_value_t sni_api_eos_view_active(const jerry_call_info_t *call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count)
{
    (void)args_p;
    (void)call_info_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    lv_obj_t *result = eos_view_active();
    return sni_tb_c2js(&result, SNI_H_LV_OBJ);
}

/* ---- eos.roundClip(view): expose eos_round_clip() to JS scripts ---- */
jerry_value_t sni_api_eos_round_clip(const jerry_call_info_t *call_info_p,
                                     const jerry_value_t args_p[],
                                     const jerry_length_t args_count)
{
    lv_obj_t *view;

    (void)call_info_p;

    if (args_count != 1)
    {
        return sni_api_throw_error("Usage: roundClip(view)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &view) || !view)
    {
        return sni_api_throw_error("Invalid view argument");
    }

    eos_round_clip(view);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_config_set_str(const jerry_call_info_t *call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count)
{
    char *key = NULL;
    char *value = NULL;
    cJSON *root = NULL;
    cJSON *item = NULL;

    (void)call_info_p;

    if (args_count != 2 || !jerry_value_is_string(args_p[0]) || !jerry_value_is_string(args_p[1]))
    {
        return sni_api_throw_error("Usage: config.setStr(key, value)");
    }

    if (!sni_api_eos_to_c_string(args_p[0], &key) || !sni_api_eos_to_c_string(args_p[1], &value))
    {
        if (key)
        {
            eos_free(key);
        }
        if (value)
        {
            eos_free(value);
        }
        return sni_api_throw_error("Failed to convert argument");
    }

    root = sni_api_eos_config_load_from_file();
    if (!root)
    {
        eos_free(key);
        eos_free(value);
        return sni_api_throw_error("Can't load config");
    }

    item = cJSON_GetObjectItem(root, key);
    if (item)
    {
        cJSON_ReplaceItemInObject(root, key, cJSON_CreateString(value));
    }
    else
    {
        cJSON_AddItemToObject(root, key, cJSON_CreateString(value));
    }

    sni_api_eos_config_write_to_file(root);
    cJSON_Delete(root);
    eos_free(key);
    eos_free(value);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_config_set_bool(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    char *key = NULL;
    bool value;
    cJSON *root = NULL;
    cJSON *item = NULL;

    (void)call_info_p;

    if (args_count != 2 || !jerry_value_is_string(args_p[0]) || !jerry_value_is_boolean(args_p[1]))
    {
        return sni_api_throw_error("Usage: config.setBool(key, bool)");
    }

    if (!sni_api_eos_to_c_string(args_p[0], &key))
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    value = jerry_value_is_true(args_p[1]);
    root = sni_api_eos_config_load_from_file();
    if (!root)
    {
        eos_free(key);
        return sni_api_throw_error("Can't load config");
    }

    item = cJSON_GetObjectItem(root, key);
    if (item)
    {
        cJSON_ReplaceItemInObject(root, key, cJSON_CreateBool(value));
    }
    else
    {
        cJSON_AddItemToObject(root, key, cJSON_CreateBool(value));
    }

    sni_api_eos_config_write_to_file(root);
    cJSON_Delete(root);
    eos_free(key);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_config_set_number(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    char *key = NULL;
    double value;
    cJSON *root = NULL;
    cJSON *item = NULL;

    (void)call_info_p;

    if (args_count != 2 || !jerry_value_is_string(args_p[0]) || !jerry_value_is_number(args_p[1]))
    {
        return sni_api_throw_error("Usage: config.setNumber(key, number)");
    }

    if (!sni_api_eos_to_c_string(args_p[0], &key))
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    value = jerry_value_as_number(args_p[1]);
    root = sni_api_eos_config_load_from_file();
    if (!root)
    {
        eos_free(key);
        return sni_api_throw_error("Can't load config");
    }

    item = cJSON_GetObjectItem(root, key);
    if (item)
    {
        cJSON_ReplaceItemInObject(root, key, cJSON_CreateNumber(value));
    }
    else
    {
        cJSON_AddItemToObject(root, key, cJSON_CreateNumber(value));
    }

    sni_api_eos_config_write_to_file(root);
    cJSON_Delete(root);
    eos_free(key);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_config_get_str(const jerry_call_info_t *call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count)
{
    char *key = NULL;
    cJSON *root = NULL;
    cJSON *item = NULL;
    jerry_value_t ret = jerry_undefined();

    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: config.getStr(key)");
    }

    if (!sni_api_eos_to_c_string(args_p[0], &key))
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    root = sni_api_eos_config_load_from_file();
    if (!root)
    {
        eos_free(key);
        return sni_api_throw_error("Can't load config");
    }

    item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsString(item))
    {
        ret = sni_tb_c2js_string(item->valuestring);
    }

    cJSON_Delete(root);
    eos_free(key);
    return ret;
}

jerry_value_t sni_api_eos_config_get_bool(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    char *key = NULL;
    cJSON *root = NULL;
    cJSON *item = NULL;
    jerry_value_t ret = jerry_boolean(false);

    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: config.getBool(key)");
    }

    if (!sni_api_eos_to_c_string(args_p[0], &key))
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    root = sni_api_eos_config_load_from_file();
    if (!root)
    {
        eos_free(key);
        return sni_api_throw_error("Can't load config");
    }

    item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsBool(item))
    {
        ret = jerry_boolean(item->valueint != 0);
    }

    cJSON_Delete(root);
    eos_free(key);
    return ret;
}

jerry_value_t sni_api_eos_config_get_number(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    char *key = NULL;
    cJSON *root = NULL;
    cJSON *item = NULL;
    jerry_value_t ret = jerry_number(0);

    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: config.getNumber(key)");
    }

    if (!sni_api_eos_to_c_string(args_p[0], &key))
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    root = sni_api_eos_config_load_from_file();
    if (!root)
    {
        eos_free(key);
        return sni_api_throw_error("Can't load config");
    }

    item = cJSON_GetObjectItem(root, key);
    if (item && cJSON_IsNumber(item))
    {
        ret = jerry_number(item->valuedouble);
    }

    cJSON_Delete(root);
    eos_free(key);
    return ret;
}

jerry_value_t sni_api_eos_time_get_now(const jerry_call_info_t *call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count)
{
    eos_datetime_t dt;
    jerry_value_t obj;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    dt = eos_time_get();
    obj = jerry_object();
    script_engine_set_prop_number(obj, "year", dt.year);
    script_engine_set_prop_number(obj, "month", dt.month);
    script_engine_set_prop_number(obj, "day", dt.day);
    script_engine_set_prop_number(obj, "hour", dt.hour);
    script_engine_set_prop_number(obj, "min", dt.min);
    script_engine_set_prop_number(obj, "sec", dt.sec);
    script_engine_set_prop_number(obj, "ms", dt.ms);
    script_engine_set_prop_number(obj, "day_of_week", dt.day_of_week);

    return obj;
}

/* ---- P0.5 相册: eos.fs.list(path) / eos.fs.size(path) ---- */
jerry_value_t sni_api_eos_fs_list(const jerry_call_info_t *call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count)
{
    char *path = NULL;
    eos_dir_t dir;
    jerry_value_t arr;
    uint32_t idx = 0;

    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: fs.list(path)");
    }

    path = (char *)sni_tb_js2c_string(args_p[0]);
    if (!path)
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    dir = eos_storage_dir_open(path);
    if (!dir)
    {
        eos_free(path);
        return sni_api_throw_error("Cannot open directory");
    }

    arr = jerry_array(0);
    for (;;)
    {
        char name[EOS_FS_PATH_MAX];
        if (eos_storage_dir_read(dir, name, sizeof(name)) != EOS_OK)
        {
            break;
        }
        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }
        jerry_value_t js_name = sni_tb_c2js_string(name);
        jerry_object_set_index(arr, idx++, js_name);
        jerry_value_free(js_name);
    }
    eos_storage_dir_close(dir);
    eos_free(path);

    return arr;
}

jerry_value_t sni_api_eos_fs_size(const jerry_call_info_t *call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count)
{
    char *path = NULL;
    eos_file_t fp;
    uint32_t size = 0;

    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: fs.size(path)");
    }

    path = (char *)sni_tb_js2c_string(args_p[0]);
    if (!path)
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    fp = eos_fs_open_read(path);
    if (!fp)
    {
        eos_free(path);
        return jerry_number(0);
    }

    if (eos_fs_size(fp, &size) != EOS_OK)
    {
        size = 0;
    }
    eos_fs_close(fp);
    eos_free(path);

    return jerry_number((double)size);
}

/* ---- 相册: eos.fs.remove(path) -> bool ---- */
jerry_value_t sni_api_eos_fs_remove(const jerry_call_info_t *call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_count)
{
    char *path = NULL;
    eos_result_t ret;

    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: fs.remove(path)");
    }

    path = (char *)sni_tb_js2c_string(args_p[0]);
    if (!path)
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    ret = eos_storage_file_remove(path);
    eos_free(path);

    return jerry_boolean(ret == EOS_OK);
}

/* ---- 笔记/画图: eos.fs.write(path, data) 兼容 string 与 Uint8Array ---- */
jerry_value_t sni_api_eos_fs_write(const jerry_call_info_t *call_info_p,
                                   const jerry_value_t args_p[],
                                   const jerry_length_t args_count)
{
    char *path = NULL;
    (void)call_info_p;

    if (args_count != 2 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: fs.write(path, string|Uint8Array)");
    }

    path = (char *)sni_tb_js2c_string(args_p[0]);
    if (!path)
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    eos_result_t r = EOS_ERR_INVALID_ARG;
    if (jerry_value_is_string(args_p[1]))
    {
        char *text = (char *)sni_tb_js2c_string(args_p[1]);
        if (text)
        {
            r = eos_storage_write_file(path, text, strlen(text));
            eos_free(text);
        }
    }
    else if (jerry_value_is_typedarray(args_p[1]))
    {
        jerry_size_t byte_offset = 0;
        jerry_size_t tlen = 0;
        jerry_value_t ab = jerry_typedarray_buffer(args_p[1], &byte_offset, &tlen);
        uint8_t *base = jerry_arraybuffer_data(ab);
        if (base)
        {
            r = eos_storage_write_file(path, base + byte_offset, (size_t)tlen);
        }
        jerry_value_free(ab);
    }
    eos_free(path);
    return (r == EOS_OK) ? jerry_boolean(true) : jerry_boolean(false);
}

/* ---- 笔记/画图: eos.fs.read(path) -> Uint8Array（二进制安全） ---- */
jerry_value_t sni_api_eos_fs_read(const jerry_call_info_t *call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count)
{
    char *path = NULL;
    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: fs.read(path)");
    }

    path = (char *)sni_tb_js2c_string(args_p[0]);
    if (!path)
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    eos_file_t fp = eos_fs_open_read(path);
    if (!fp)
    {
        eos_free(path);
        return jerry_undefined();
    }
    uint32_t sz = 0;
    if (eos_fs_size(fp, &sz) != EOS_OK || sz == 0)
    {
        eos_fs_close(fp);
        eos_free(path);
        return jerry_undefined();
    }
    uint8_t *buf = (uint8_t *)eos_malloc(sz);
    if (!buf)
    {
        eos_fs_close(fp);
        eos_free(path);
        return jerry_undefined();
    }
    int rd = eos_fs_read(fp, buf, sz);
    eos_fs_close(fp);
    eos_free(path);
    if (rd <= 0)
    {
        eos_free(buf);
        return jerry_undefined();
    }

    jerry_value_t ab = jerry_arraybuffer((jerry_length_t)sz);
    uint8_t *dst = jerry_arraybuffer_data(ab);
    if (dst)
    {
        memcpy(dst, buf, (size_t)sz);
    }
    eos_free(buf);

    jerry_value_t ta = jerry_typedarray_with_buffer(JERRY_TYPEDARRAY_UINT8, ab);
    jerry_value_free(ab);
    return ta;
}

/* ---- Album: eos.fs.peek(path, offset, len) -> Uint8Array ----
   只读取文件任意一段(默认从头 64 字节,上限 8KB),用于探测图片魔数/
   JPEG SOF(progressive)而不用 fs.read 整读大图撑爆 JS 堆。 */
jerry_value_t sni_api_eos_fs_peek(const jerry_call_info_t *call_info_p,
                                  const jerry_value_t args_p[],
                                  const jerry_length_t args_count)
{
    char *path = NULL;
    (void)call_info_p;

    if (args_count < 1 || !jerry_value_is_string(args_p[0]))
    {
        return sni_api_throw_error("Usage: fs.peek(path, offset, len)");
    }

    path = (char *)sni_tb_js2c_string(args_p[0]);
    if (!path)
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    uint32_t offset = 0, len = 64;
    if (args_count >= 2 && jerry_value_is_number(args_p[1]))
        offset = (uint32_t)jerry_value_as_number(args_p[1]);
    if (args_count >= 3 && jerry_value_is_number(args_p[2]))
        len = (uint32_t)jerry_value_as_number(args_p[2]);
    if (len > 8192)
        len = 8192;

    eos_file_t fp = eos_fs_open_read(path);
    if (!fp)
    {
        eos_free(path);
        return jerry_undefined();
    }
    if (eos_fs_seek(fp, offset) != EOS_OK)
    {
        eos_fs_close(fp);
        eos_free(path);
        return jerry_undefined();
    }
    uint8_t *buf = (uint8_t *)eos_malloc(len);
    if (!buf)
    {
        eos_fs_close(fp);
        eos_free(path);
        return jerry_undefined();
    }
    int rd = eos_fs_read(fp, buf, len);
    eos_fs_close(fp);
    eos_free(path);
    if (rd <= 0)
    {
        eos_free(buf);
        return jerry_undefined();
    }

    jerry_value_t ab = jerry_arraybuffer((jerry_length_t)rd);
    uint8_t *dst = jerry_arraybuffer_data(ab);
    if (dst)
    {
        memcpy(dst, buf, (size_t)rd);
    }
    eos_free(buf);

    jerry_value_t ta = jerry_typedarray_with_buffer(JERRY_TYPEDARRAY_UINT8, ab);
    jerry_value_free(ab);
    return ta;
}

/* ---- P0.5 相册: eos.app.openFiles() -> 原生文件管理器 ---- */
jerry_value_t sni_api_eos_app_open_files(const jerry_call_info_t *call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count)
{
    (void)call_info_p;
    (void)args_p;
    (void)args_count;

    eos_files_enter();
    return jerry_undefined();
}

/* ---- 笔记: eos.ime.open(callback) -> 系统输入页（键盘） ---- */
typedef struct
{
    jerry_value_t js_cb;
    sni_context_t *owner_ctx;
    uint8_t alive;
} sni_ime_ctx_t;

static void _sni_ime_close_cb(const char *text, eos_input_result_t result, void *user_data)
{
    sni_ime_ctx_t *ctx = (sni_ime_ctx_t *)user_data;
    if (!ctx || !ctx->alive)
    {
        return;
    }
    ctx->alive = 0;

    jerry_value_t js_text = jerry_undefined();
    if (text && result == EOS_INPUT_RESULT_OK)
    {
        js_text = sni_tb_c2js_string(text);
    }
    jerry_value_t args[1] = { js_text };
    jerry_value_t ret = spm_call(ctx->owner_ctx->owner, ctx->js_cb, jerry_undefined(), args, 1);
    if (jerry_value_is_error(ret) || jerry_value_is_exception(ret))
    {
        EOS_LOG_E("IME close callback encountered an error");
    }
    jerry_value_free(ret);
    jerry_value_free(js_text);
    jerry_value_free(ctx->js_cb);
    eos_free(ctx);
}

jerry_value_t sni_api_eos_ime_open(const jerry_call_info_t *call_info_p,
                                   const jerry_value_t args_p[],
                                   const jerry_length_t args_count)
{
    (void)call_info_p;

    if (args_count != 1 || !jerry_value_is_function(args_p[0]))
    {
        return sni_api_throw_error("Usage: ime.open(callback)");
    }

    sni_ime_ctx_t *ctx = (sni_ime_ctx_t *)eos_malloc(sizeof(sni_ime_ctx_t));
    if (!ctx)
    {
        return sni_api_throw_error("Out of memory");
    }
    memset(ctx, 0, sizeof(sni_ime_ctx_t));
    ctx->js_cb = jerry_value_copy(args_p[0]);
    ctx->owner_ctx = sni_cb_get_context();
    ctx->alive = 1;

    if (eos_input_page_open_with_callback(NULL, _sni_ime_close_cb, ctx) != EOS_OK)
    {
        ctx->alive = 0;
        jerry_value_free(ctx->js_cb);
        eos_free(ctx);
        return sni_api_throw_error("Failed to open input page");
    }
    return jerry_undefined();
}

jerry_value_t sni_api_eos_app_header_set_title(const jerry_call_info_t *call_info_p,
                                               const jerry_value_t args_p[],
                                               const jerry_length_t args_count)
{
    eos_activity_t *current;
    char *title = NULL;
    lv_obj_t *view;

    (void)call_info_p;

    if (args_count != 2)
    {
        return sni_api_throw_error("Usage: appHeader.setTitle(view, title)");
    }

    if (!jerry_value_is_null(args_p[0]) && !jerry_value_is_object(args_p[0]))
    {
        return sni_api_throw_error("Invalid view argument type");
    }

    if (!jerry_value_is_null(args_p[0]))
    {
        if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &view))
        {
            return sni_api_throw_error("Invalid view argument");
        }
    }

    if (!jerry_value_is_null(args_p[1]) && !jerry_value_is_undefined(args_p[1]))
    {
        if (!sni_api_eos_to_c_string(args_p[1], &title))
        {
            return sni_api_throw_error("Invalid title argument");
        }
    }

    current = eos_activity_get_current();
    if (!current)
    {
        if (title)
        {
            eos_free(title);
        }
        return sni_api_throw_error("No current activity");
    }

    eos_activity_set_title(current, title);
    if (title)
    {
        eos_free(title);
    }

    return jerry_undefined();
}

jerry_value_t sni_api_eos_app_header_hide(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    eos_app_header_hide();
    return jerry_undefined();
}

jerry_value_t sni_api_eos_app_header_show(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    eos_activity_t *current;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    current = eos_activity_get_current();
    if (!current)
    {
        return sni_api_throw_error("No current activity");
    }

    eos_app_header_show(current);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_clock_hand_create(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    lv_obj_t *obj;
    char *src;
    int32_t type;
    int32_t cx;
    int32_t cy;
    lv_obj_t *ret_obj;

    (void)call_info_p;

    if (args_count != 5)
    {
        return sni_api_throw_error("Usage: clockHand.create(obj, src, type, cx, cy)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &obj))
    {
        return sni_api_throw_error("Invalid object argument");
    }

    if (!jerry_value_is_string(args_p[1]) || !jerry_value_is_number(args_p[2]) || !jerry_value_is_number(args_p[3])
        || !jerry_value_is_number(args_p[4]))
    {
        return sni_api_throw_error("Invalid argument type");
    }

    src = sni_api_eos_get_assets_file_str(args_p[1]);
    if (!src)
    {
        return sni_api_throw_error("Invalid image source");
    }

    type = (int32_t)jerry_value_as_number(args_p[2]);
    cx = (int32_t)jerry_value_as_number(args_p[3]);
    cy = (int32_t)jerry_value_as_number(args_p[4]);

    ret_obj = eos_clock_hand_create(obj, src, (eos_clock_hand_type_t)type, cx, cy);
    eos_free(src);
    return sni_tb_c2js(&ret_obj, SNI_H_LV_OBJ);
}

jerry_value_t sni_api_eos_clock_hand_center(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    lv_obj_t *obj;

    (void)call_info_p;

    if (args_count != 1)
    {
        return sni_api_throw_error("Usage: clockHand.center(obj)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &obj))
    {
        return sni_api_throw_error("Invalid object argument");
    }

    eos_clock_hand_center(obj);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_clock_hand_place_pivot(const jerry_call_info_t *call_info_p,
                                                 const jerry_value_t args_p[],
                                                 const jerry_length_t args_count)
{
    lv_obj_t *obj;
    int32_t x;
    int32_t y;

    (void)call_info_p;

    if (args_count != 3)
    {
        return sni_api_throw_error("Usage: clockHand.placePivot(obj, x, y)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &obj) || !jerry_value_is_number(args_p[1])
        || !jerry_value_is_number(args_p[2]))
    {
        return sni_api_throw_error("Invalid argument type");
    }

    x = (int32_t)jerry_value_as_number(args_p[1]);
    y = (int32_t)jerry_value_as_number(args_p[2]);
    eos_clock_hand_place_pivot(obj, x, y);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_clock_hand_attach(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    lv_obj_t *obj;
    int32_t type;

    (void)call_info_p;

    if (args_count != 2)
    {
        return sni_api_throw_error("Usage: clockHand.attach(obj, type)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &obj) || !jerry_value_is_number(args_p[1]))
    {
        return sni_api_throw_error("Invalid argument type");
    }

    type = (int32_t)jerry_value_as_number(args_p[1]);
    eos_clock_hand_attach(obj, (eos_clock_hand_type_t)type);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_clock_hand_center_style(const jerry_call_info_t *call_info_p,
                                                  const jerry_value_t args_p[],
                                                  const jerry_length_t args_count)
{
    lv_obj_t *obj;
    int32_t px;
    int32_t py;

    (void)call_info_p;

    if (args_count != 3)
    {
        return sni_api_throw_error("Usage: clockHand.centerStyle(obj, pivotX, pivotY)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &obj) || !jerry_value_is_number(args_p[1])
        || !jerry_value_is_number(args_p[2]))
    {
        return sni_api_throw_error("Invalid argument type");
    }

    px = (int32_t)jerry_value_as_number(args_p[1]);
    py = (int32_t)jerry_value_as_number(args_p[2]);
    eos_clock_hand_center_style(obj, px, py);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_activity_current(const jerry_call_info_t *call_info_p,
                                           const jerry_value_t args_p[],
                                           const jerry_length_t args_count)
{
    eos_activity_t *activity;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    activity = eos_activity_get_current();
    return sni_tb_c2js(&activity, SNI_H_EOS_ACTIVITY);
}

jerry_value_t sni_api_eos_activity_visible(const jerry_call_info_t *call_info_p,
                                           const jerry_value_t args_p[],
                                           const jerry_length_t args_count)
{
    eos_activity_t *activity;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    activity = eos_activity_get_visible();
    return sni_tb_c2js(&activity, SNI_H_EOS_ACTIVITY);
}

jerry_value_t sni_api_eos_activity_bottom(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    eos_activity_t *activity;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    activity = eos_activity_get_bottom();
    return sni_tb_c2js(&activity, SNI_H_EOS_ACTIVITY);
}

jerry_value_t sni_api_eos_activity_watchface(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    eos_activity_t *activity;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    activity = eos_activity_get_watchface();
    return sni_tb_c2js(&activity, SNI_H_EOS_ACTIVITY);
}

jerry_value_t sni_api_eos_activity_get_view(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    eos_activity_t *activity;
    lv_obj_t *view;

    (void)call_info_p;

    if (args_count != 1)
    {
        return sni_api_throw_error("Usage: activity.getView(activity)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity))
    {
        return sni_api_throw_error("Invalid activity argument");
    }

    view = eos_activity_get_view(activity);
    return sni_tb_c2js(&view, SNI_H_LV_OBJ);
}

jerry_value_t sni_api_eos_activity_set_view(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    eos_activity_t *activity;
    lv_obj_t *view;

    (void)call_info_p;

    if (args_count != 2)
    {
        return sni_api_throw_error("Usage: activity.setView(activity, view)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity) || !sni_tb_js2c(args_p[1], SNI_H_LV_OBJ, &view))
    {
        return sni_api_throw_error("Invalid argument type");
    }

    eos_activity_set_view(activity, view);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_activity_get_title(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    eos_activity_t *activity;
    const char *title;

    (void)call_info_p;

    if (args_count != 1)
    {
        return sni_api_throw_error("Usage: activity.getTitle(activity)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity))
    {
        return sni_api_throw_error("Invalid activity argument");
    }

    title = eos_activity_get_title(activity);
    if (!title)
    {
        return jerry_undefined();
    }

    return sni_tb_c2js_string(title);
}

jerry_value_t sni_api_eos_activity_set_title(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    eos_activity_t *activity;
    char *title = NULL;

    (void)call_info_p;

    if (args_count != 2 || !jerry_value_is_string(args_p[1]))
    {
        return sni_api_throw_error("Usage: activity.setTitle(activity, title)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity) || !sni_api_eos_to_c_string(args_p[1], &title))
    {
        return sni_api_throw_error("Invalid argument type");
    }

    eos_activity_set_title(activity, title);
    eos_free(title);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_activity_set_type(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    eos_activity_t *activity;
    int32_t type;

    (void)call_info_p;

    if (args_count != 2 || !jerry_value_is_number(args_p[1]))
    {
        return sni_api_throw_error("Usage: activity.setType(activity, type)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity))
    {
        return sni_api_throw_error("Invalid activity argument");
    }

    type = (int32_t)jerry_value_as_number(args_p[1]);
    eos_activity_set_type(activity, (eos_activity_type_t)type);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_activity_get_type(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    eos_activity_t *activity;
    eos_activity_type_t type;

    (void)call_info_p;

    if (args_count != 1)
    {
        return sni_api_throw_error("Usage: activity.getType(activity)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity))
    {
        return sni_api_throw_error("Invalid activity argument");
    }

    type = eos_activity_get_type(activity);
    return jerry_number((double)type);
}

jerry_value_t sni_api_eos_activity_set_app_header_visible(const jerry_call_info_t *call_info_p,
                                                          const jerry_value_t args_p[],
                                                          const jerry_length_t args_count)
{
    eos_activity_t *activity;
    bool visible;

    (void)call_info_p;

    if (args_count != 2 || !jerry_value_is_boolean(args_p[1]))
    {
        return sni_api_throw_error("Usage: activity.setAppHeaderVisible(activity, visible)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity))
    {
        return sni_api_throw_error("Invalid activity argument");
    }

    visible = jerry_value_is_true(args_p[1]);
    eos_activity_set_app_header_visible(activity, visible);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_activity_is_app_header_visible(const jerry_call_info_t *call_info_p,
                                                         const jerry_value_t args_p[],
                                                         const jerry_length_t args_count)
{
    eos_activity_t *activity;
    bool visible;

    (void)call_info_p;

    if (args_count != 1)
    {
        return sni_api_throw_error("Usage: activity.isAppHeaderVisible(activity)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity))
    {
        return sni_api_throw_error("Invalid activity argument");
    }

    visible = eos_activity_is_app_header_visible(activity);
    return jerry_boolean(visible);
}

jerry_value_t sni_api_eos_activity_enter(const jerry_call_info_t *call_info_p,
                                         const jerry_value_t args_p[],
                                         const jerry_length_t args_count)
{
    eos_activity_t *activity;

    (void)call_info_p;

    if (args_count != 1)
    {
        return sni_api_throw_error("Usage: activity.enter(activity)");
    }

    if (!sni_tb_js2c(args_p[0], SNI_H_EOS_ACTIVITY, &activity))
    {
        return sni_api_throw_error("Invalid activity argument");
    }

    eos_activity_enter(activity);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_activity_back(const jerry_call_info_t *call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count)
{
    eos_result_t ret;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    ret = eos_activity_back();
    return jerry_boolean(ret == EOS_OK);
}

jerry_value_t sni_api_eos_activity_root_screen(const jerry_call_info_t *call_info_p,
                                               const jerry_value_t args_p[],
                                               const jerry_length_t args_count)
{
    lv_obj_t *screen;

    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    screen = eos_activity_get_root_screen();
    return sni_tb_c2js(&screen, SNI_H_LV_OBJ);
}

jerry_value_t sni_api_eos_activity_is_transition_in_progress(const jerry_call_info_t *call_info_p,
                                                             const jerry_value_t args_p[],
                                                             const jerry_length_t args_count)
{
    (void)call_info_p;
    (void)args_p;

    if (args_count != 0)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    return jerry_boolean(eos_activity_is_transition_in_progress());
}

jerry_value_t sni_api_eos_console_log(const jerry_call_info_t *call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_count)
{
    (void)call_info_p;

    return sni_api_eos_console_write(args_p, args_count, EOS_CONSOLE_LEVEL_LOG);
}

jerry_value_t sni_api_eos_console_error(const jerry_call_info_t *call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count)
{
    (void)call_info_p;

    return sni_api_eos_console_write(args_p, args_count, EOS_CONSOLE_LEVEL_ERROR);
}

jerry_value_t sni_api_eos_console_warn(const jerry_call_info_t *call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_count)
{
    (void)call_info_p;

    return sni_api_eos_console_write(args_p, args_count, EOS_CONSOLE_LEVEL_WARN);
}

jerry_value_t sni_api_eos_console_debug(const jerry_call_info_t *call_info_p,
                                        const jerry_value_t args_p[],
                                        const jerry_length_t args_count)
{
    (void)call_info_p;

    return sni_api_eos_console_write(args_p, args_count, EOS_CONSOLE_LEVEL_DEBUG);
}

const sni_method_desc_t eos_class_static_methods_view[] = {
    {.name = "active", .handler = sni_api_eos_view_active},
    {.name = NULL, .handler = NULL},
};

const sni_method_desc_t eos_class_static_methods_console[] = {
    {.name = "log", .handler = sni_api_eos_console_log},
    {.name = "error", .handler = sni_api_eos_console_error},
    {.name = "warn", .handler = sni_api_eos_console_warn},
    {.name = "info", .handler = sni_api_eos_console_log},
    {.name = "debug", .handler = sni_api_eos_console_debug},
    {.name = NULL, .handler = NULL},
};

const sni_method_desc_t eos_class_static_methods_config[] = {
    {.name = "setStr", .handler = sni_api_eos_config_set_str},
    {.name = "setBool", .handler = sni_api_eos_config_set_bool},
    {.name = "setNumber", .handler = sni_api_eos_config_set_number},
    {.name = "getStr", .handler = sni_api_eos_config_get_str},
    {.name = "getBool", .handler = sni_api_eos_config_get_bool},
    {.name = "getNumber", .handler = sni_api_eos_config_get_number},
    {.name = NULL, .handler = NULL},
};

const sni_method_desc_t eos_class_static_methods_time[] = {
    {.name = "getNow", .handler = sni_api_eos_time_get_now},
    {.name = NULL, .handler = NULL},
};

/* P0.5 相册: eos.fs.* */
const sni_method_desc_t eos_class_static_methods_fs[] = {
    {.name = "list", .handler = sni_api_eos_fs_list},
    {.name = "size", .handler = sni_api_eos_fs_size},
    {.name = "remove", .handler = sni_api_eos_fs_remove},   /* 相册删除 */
    {.name = "write", .handler = sni_api_eos_fs_write},   /* 笔记/画图 */
    {.name = "read", .handler = sni_api_eos_fs_read},     /* 笔记/画图 */
    {.name = "peek", .handler = sni_api_eos_fs_peek},     /* 相册探测(不整读) */
    {.name = NULL, .handler = NULL},
};

const sni_class_desc_t eos_class_desc_fs = {
    .name = "fs",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_fs,
    .constants = NULL,
};

/* P0.5 相册: eos.app.* */
const sni_method_desc_t eos_class_static_methods_app[] = {
    {.name = "openFiles", .handler = sni_api_eos_app_open_files},
    {.name = NULL, .handler = NULL},
};

const sni_class_desc_t eos_class_desc_app = {
    .name = "app",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_app,
    .constants = NULL,
};

/* 笔记/画图: eos.ime.open(cb) */
const sni_method_desc_t eos_class_static_methods_ime[] = {
    {.name = "open", .handler = sni_api_eos_ime_open},
    {.name = NULL, .handler = NULL},
};

const sni_class_desc_t eos_class_desc_ime = {
    .name = "ime",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_ime,
    .constants = NULL,
};

const sni_method_desc_t eos_class_static_methods_app_header[] = {
    {.name = "setTitle", .handler = sni_api_eos_app_header_set_title},
    {.name = "hide", .handler = sni_api_eos_app_header_hide},
    {.name = "show", .handler = sni_api_eos_app_header_show},
    {.name = NULL, .handler = NULL},
};

const sni_method_desc_t eos_class_static_methods_clock_hand[] = {
    {.name = "create", .handler = sni_api_eos_clock_hand_create},
    {.name = "center", .handler = sni_api_eos_clock_hand_center},
    {.name = "placePivot", .handler = sni_api_eos_clock_hand_place_pivot},
    {.name = "attach", .handler = sni_api_eos_clock_hand_attach},
    {.name = "centerStyle", .handler = sni_api_eos_clock_hand_center_style},
    {.name = NULL, .handler = NULL},
};

const sni_method_desc_t eos_class_static_methods_activity[] = {
    {.name = "current", .handler = sni_api_eos_activity_current},
    {.name = "visible", .handler = sni_api_eos_activity_visible},
    {.name = "bottom", .handler = sni_api_eos_activity_bottom},
    {.name = "watchface", .handler = sni_api_eos_activity_watchface},
    {.name = "rootScreen", .handler = sni_api_eos_activity_root_screen},
    {.name = "getView", .handler = sni_api_eos_activity_get_view},
    {.name = "setView", .handler = sni_api_eos_activity_set_view},
    {.name = "getTitle", .handler = sni_api_eos_activity_get_title},
    {.name = "setTitle", .handler = sni_api_eos_activity_set_title},
    {.name = "getType", .handler = sni_api_eos_activity_get_type},
    {.name = "setType", .handler = sni_api_eos_activity_set_type},
    {.name = "setAppHeaderVisible", .handler = sni_api_eos_activity_set_app_header_visible},
    {.name = "isAppHeaderVisible", .handler = sni_api_eos_activity_is_app_header_visible},
    {.name = "enter", .handler = sni_api_eos_activity_enter},
    {.name = "back", .handler = sni_api_eos_activity_back},
    {.name = "isTransitionInProgress", .handler = sni_api_eos_activity_is_transition_in_progress},
    {.name = NULL, .handler = NULL},
};

const sni_class_desc_t eos_class_desc_view = {
    .name = "view",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_view,
    .constants = NULL,
};

const sni_class_desc_t eos_class_desc_console = {
    .name = "console",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_console,
    .constants = NULL,
};

const sni_class_desc_t eos_class_desc_config = {
    .name = "config",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_config,
    .constants = NULL,
};

const sni_class_desc_t eos_class_desc_time = {
    .name = "time",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_time,
    .constants = NULL,
};

const sni_class_desc_t eos_class_desc_app_header = {
    .name = "appHeader",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_app_header,
    .constants = NULL,
};

const sni_class_desc_t eos_class_desc_clock_hand = {
    .name = "clockHand",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_clock_hand,
    .constants = NULL,
};

const sni_class_desc_t eos_class_desc_activity = {
    .name = "activity",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_activity,
    .constants = NULL,
};

const sni_method_desc_t eos_class_static_methods_permission[] = {
    {.name = "request", .handler = sni_api_eos_permission_request},
    {.name = "check", .handler = sni_api_eos_permission_check},
    {.name = NULL, .handler = NULL},
};

const sni_class_desc_t eos_class_desc_permission = {
    .name = "permission",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_permission,
    .constants = NULL,
};

/* ---- 画图: eos.draw.* (C 持有 canvas 像素缓冲，JS 只发绘制指令) ---- */
typedef struct
{
    lv_obj_t *canvas;
    lv_color_t *buf;
    lv_coord_t w, h;
    lv_color_t pen;
    bool inited;
} eos_draw_state_t;
static eos_draw_state_t g_draw = {0};

/* 在 canvas 局部坐标画 Bresenham 线段，并触发重绘 */
static void _eos_draw_line_local(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (!g_draw.inited || !g_draw.buf) return;
    int32_t dx = abs(x1 - x0), dy = abs(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    int32_t x = x0, y = y0;
    for (;;)
    {
        if (x >= 0 && x < g_draw.w && y >= 0 && y < g_draw.h)
            g_draw.buf[y * g_draw.w + x] = g_draw.pen;
        if (x == x1 && y == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
    lv_obj_invalidate(g_draw.canvas);
}

jerry_value_t sni_api_eos_draw_create(const jerry_call_info_t *call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_cnt)
{
    (void)call_info_p;
    if (args_cnt < 3)
        return sni_api_throw_error("Usage: draw.create(parent, w, h)");
    lv_obj_t *parent = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &parent) || !parent)
        return sni_api_throw_error("draw.create: invalid parent");
    lv_coord_t w = (lv_coord_t)jerry_value_as_integer(args_p[1]);
    lv_coord_t h = (lv_coord_t)jerry_value_as_integer(args_p[2]);
    if (w <= 0 || h <= 0)
        return sni_api_throw_error("draw.create: invalid size");

    if (g_draw.canvas) { lv_obj_del(g_draw.canvas); g_draw.canvas = NULL; }
    if (g_draw.buf) { eos_free(g_draw.buf); g_draw.buf = NULL; }

    g_draw.buf = (lv_color_t *)eos_malloc((size_t)w * (size_t)h * sizeof(lv_color_t));
    if (!g_draw.buf)
        return sni_api_throw_error("draw.create: out of memory");

    g_draw.canvas = lv_canvas_create(parent);
    if (!g_draw.canvas)
    {
        eos_free(g_draw.buf); g_draw.buf = NULL;
        return sni_api_throw_error("draw.create: canvas create failed");
    }
    lv_canvas_set_buffer(g_draw.canvas, g_draw.buf, w, h, LV_COLOR_FORMAT_NATIVE);
    lv_obj_add_flag(g_draw.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_draw.canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_draw.canvas, LV_OBJ_FLAG_IGNORE_LAYOUT);
    g_draw.w = w; g_draw.h = h;
    g_draw.pen = lv_color_white();
    g_draw.inited = true;

    /* 注意：canvas 句柄不回传 JS。SNI 的 sni_tb_c2js 只给裸指针、不带 lv_obj 原型，
     * 回传后 JS 调 setPos / setStyle* / addEventCb 会 "Expected a function"。
     * 改为：C 持有 canvas（探针按 lv_canvas_class 查找），JS 把 PRESSED/PRESSING 挂在
     * R.root 上，由 eos.draw.line 在 C 层把屏幕坐标转 canvas 局部坐标。 */
    return jerry_boolean(true);
}

jerry_value_t sni_api_eos_draw_set_pen(const jerry_call_info_t *call_info_p,
                                       const jerry_value_t args_p[],
                                       const jerry_length_t args_cnt)
{
    (void)call_info_p;
    if (args_cnt < 3)
        return sni_api_throw_error("Usage: draw.setPen(r, g, b)");
    uint8_t r = (uint8_t)jerry_value_as_integer(args_p[0]);
    uint8_t g = (uint8_t)jerry_value_as_integer(args_p[1]);
    uint8_t b = (uint8_t)jerry_value_as_integer(args_p[2]);
    g_draw.pen = lv_color_make(r, g, b);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_draw_line(const jerry_call_info_t *call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_cnt)
{
    (void)call_info_p;
    if (args_cnt < 4 || !g_draw.inited)
        return sni_api_throw_error("Usage: draw.line(x0, y0, x1, y1)");
    lv_area_t a; lv_obj_get_coords(g_draw.canvas, &a);
    int32_t x0 = (int32_t)jerry_value_as_integer(args_p[0]) - a.x1;
    int32_t y0 = (int32_t)jerry_value_as_integer(args_p[1]) - a.y1;
    int32_t x1 = (int32_t)jerry_value_as_integer(args_p[2]) - a.x1;
    int32_t y1 = (int32_t)jerry_value_as_integer(args_p[3]) - a.y1;
    _eos_draw_line_local(x0, y0, x1, y1);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_draw_clear(const jerry_call_info_t *call_info_p,
                                     const jerry_value_t args_p[],
                                     const jerry_length_t args_cnt)
{
    (void)call_info_p;
    if (args_cnt < 3 || !g_draw.inited || !g_draw.buf)
        return sni_api_throw_error("Usage: draw.clear(r, g, b)");
    lv_color_t c = lv_color_make((uint8_t)jerry_value_as_integer(args_p[0]),
                                 (uint8_t)jerry_value_as_integer(args_p[1]),
                                 (uint8_t)jerry_value_as_integer(args_p[2]));
    uint32_t n = (uint32_t)g_draw.w * (uint32_t)g_draw.h;
    for (uint32_t i = 0; i < n; i++) g_draw.buf[i] = c;
    lv_obj_invalidate(g_draw.canvas);
    return jerry_undefined();
}

jerry_value_t sni_api_eos_draw_size(const jerry_call_info_t *call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_cnt)
{
    (void)call_info_p; (void)args_p; (void)args_cnt;
    jerry_value_t obj = jerry_object();
    jerry_value_t jw = jerry_number((double)g_draw.w);
    jerry_value_t jh = jerry_number((double)g_draw.h);
    jerry_value_free(jerry_object_set_sz(obj, "w", jw));
    jerry_value_free(jw);
    jerry_value_free(jerry_object_set_sz(obj, "h", jh));
    jerry_value_free(jh);
    return obj;
}

/* P0/P1a 笔刷：在 canvas 局部坐标 (x,y) 落一个像素（3×3 笔刷在 JS 侧循环调用）。
 * 直接写 g_draw.buf（与 line/clear 同缓冲），lv_obj_invalidate 触发重绘。 */
jerry_value_t sni_api_eos_draw_set_px(const jerry_call_info_t *call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_cnt)
{
    (void)call_info_p;
    if (args_cnt < 5 || !g_draw.inited || !g_draw.buf)
        return sni_api_throw_error("Usage: draw.setPx(x, y, r, g, b)");
    int32_t x = (int32_t)jerry_value_as_integer(args_p[0]);
    int32_t y = (int32_t)jerry_value_as_integer(args_p[1]);
    uint8_t r = (uint8_t)jerry_value_as_integer(args_p[2]);
    uint8_t g = (uint8_t)jerry_value_as_integer(args_p[3]);
    uint8_t b = (uint8_t)jerry_value_as_integer(args_p[4]);
    if (x >= 0 && x < g_draw.w && y >= 0 && y < g_draw.h)
        g_draw.buf[y * g_draw.w + x] = lv_color_make(r, g, b);
    lv_obj_invalidate(g_draw.canvas);
    return jerry_undefined();
}

/* P0 探针回读：局部坐标 (x,y) 读回 {r,g,b}；越界返回 {0,0,0}。 */
jerry_value_t sni_api_eos_draw_get_px(const jerry_call_info_t *call_info_p,
                                      const jerry_value_t args_p[],
                                      const jerry_length_t args_cnt)
{
    (void)call_info_p;
    jerry_value_t obj = jerry_object();
    if (args_cnt < 2 || !g_draw.inited || !g_draw.buf) {
        jerry_value_free(jerry_object_set_sz(obj, "r", jerry_number(0)));
        jerry_value_free(jerry_object_set_sz(obj, "g", jerry_number(0)));
        jerry_value_free(jerry_object_set_sz(obj, "b", jerry_number(0)));
        return obj;
    }
    int32_t x = (int32_t)jerry_value_as_integer(args_p[0]);
    int32_t y = (int32_t)jerry_value_as_integer(args_p[1]);
    uint8_t r = 0, g = 0, b = 0;
    if (x >= 0 && x < g_draw.w && y >= 0 && y < g_draw.h) {
        lv_color_t c = g_draw.buf[y * g_draw.w + x];
        r = c.red; g = c.green; b = c.blue;
    }
    jerry_value_free(jerry_object_set_sz(obj, "r", jerry_number(r)));
    jerry_value_free(jerry_object_set_sz(obj, "g", jerry_number(g)));
    jerry_value_free(jerry_object_set_sz(obj, "b", jerry_number(b)));
    return obj;
}

jerry_value_t sni_api_eos_draw_save(const jerry_call_info_t *call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_cnt)
{
    (void)call_info_p;
    if (args_cnt < 1 || !g_draw.inited || !g_draw.buf)
        return sni_api_throw_error("Usage: draw.save(path)");
    const char *path = sni_tb_js2c_string(args_p[0]);
    if (!path)
        return sni_api_throw_error("draw.save: invalid path");

    /* 确保父目录存在（如 /sdcard/Drawings） */
    char dir[256];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir)
    {
        *slash = '\0';
        eos_storage_mkdir_if_not_exist(dir);
    }

    /* 组装 .edrw: "ELDRW1" + w(2) + h(2) + fmt(1) + buf(RGB565, 2 字节/像素)。
     * native lv_color_t 可能是 RGB565 或 RGB888（取决于 LV_COLOR_DEPTH），统一用
     * lv_color_to_u16 转成 RGB565 小端写入，保证文件格式稳定、可被 P2 回读。 */
    uint32_t px = (uint32_t)g_draw.w * (uint32_t)g_draw.h;
    uint32_t px_bytes = px * 2U; /* RGB565 = 2 bytes/pixel */
    uint32_t total = 6 + 2 + 2 + 1 + px_bytes;
    uint8_t *out = (uint8_t *)eos_malloc(total);
    if (!out)
        return sni_api_throw_error("draw.save: out of memory");
    uint8_t *p = out;
    memcpy(p, "ELDRW1", 6); p += 6;
    *p++ = (uint8_t)(g_draw.w & 0xFF);
    *p++ = (uint8_t)((g_draw.w >> 8) & 0xFF);
    *p++ = (uint8_t)(g_draw.h & 0xFF);
    *p++ = (uint8_t)((g_draw.h >> 8) & 0xFF);
    *p++ = 0; /* format 0 = RGB565 (2 bytes/pixel) */
    const lv_color_t *src = g_draw.buf;
    for (uint32_t i = 0; i < px; i++)
    {
        uint16_t c16 = lv_color_to_u16(src[i]);
        *p++ = (uint8_t)(c16 & 0xFF);
        *p++ = (uint8_t)((c16 >> 8) & 0xFF);
    }

    bool ok = false;
    eos_file_t fp = eos_fs_open_write(path);
    if (fp)
    {
        ssize_t written = eos_fs_write(fp, out, total);
        eos_fs_close(fp);
        ok = (written == (ssize_t)total);
    }
    eos_free(out);
    return jerry_boolean(ok);
}

/* 回读 .edrw：解析 "ELDRW1"+w+h+fmt+RGB565，把像素写回 canvas 缓冲（RGB565 -> native） */
jerry_value_t sni_api_eos_draw_load(const jerry_call_info_t *call_info_p,
                                    const jerry_value_t args_p[],
                                    const jerry_length_t args_cnt)
{
    (void)call_info_p;
    if (args_cnt < 1 || !g_draw.inited || !g_draw.buf)
        return sni_api_throw_error("Usage: draw.load(path)");
    const char *path = sni_tb_js2c_string(args_p[0]);
    if (!path)
        return sni_api_throw_error("draw.load: invalid path");

    eos_file_t fp = eos_fs_open_read(path);
    if (!fp)
        return jerry_boolean(false);

    uint32_t fsize = 0;
    if (eos_fs_size(fp, &fsize) != EOS_OK || fsize < 11)
    {
        eos_fs_close(fp);
        return jerry_boolean(false);
    }

    uint8_t *raw = (uint8_t *)eos_malloc(fsize);
    if (!raw)
    {
        eos_fs_close(fp);
        return sni_api_throw_error("draw.load: out of memory");
    }
    int rd = eos_fs_read(fp, raw, fsize);
    eos_fs_close(fp);
    if (rd != (int)fsize)
    {
        eos_free(raw);
        return jerry_boolean(false);
    }

    if (memcmp(raw, "ELDRW1", 6) != 0)          /* 魔术字 */
    {
        eos_free(raw);
        return jerry_boolean(false);
    }
    uint32_t fw = (uint32_t)raw[6] | ((uint32_t)raw[7] << 8);
    uint32_t fh = (uint32_t)raw[8] | ((uint32_t)raw[9] << 8);
    uint8_t  fmt = raw[10];
    if (fmt != 0)                                /* 仅支持 RGB565 */
    {
        eos_free(raw);
        return jerry_boolean(false);
    }
    const uint8_t *px = raw + 11;
    if ((fsize - 11) < (uint32_t)fw * (uint32_t)fh * 2U)
    {
        eos_free(raw);
        return jerry_boolean(false);
    }

    /* 尺寸一致整拷贝；不一致则左上角重叠区最佳努力拷贝（本 app 固定 176x176，一般相等） */
    uint32_t copy_w = (fw < (uint32_t)g_draw.w) ? fw : (uint32_t)g_draw.w;
    uint32_t copy_h = (fh < (uint32_t)g_draw.h) ? fh : (uint32_t)g_draw.h;
    for (uint32_t y = 0; y < copy_h; y++)
    {
        for (uint32_t x = 0; x < copy_w; x++)
        {
            uint32_t si = y * fw + x;
            uint32_t di = y * (uint32_t)g_draw.w + x;
            uint16_t c16 = (uint16_t)(px[si * 2] | ((uint16_t)px[si * 2 + 1] << 8));
            uint8_t r5 = (c16 >> 11) & 0x1F;
            uint8_t g6 = (c16 >> 5)  & 0x3F;
            uint8_t b5 = c16 & 0x1F;
            uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
            uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
            uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
            g_draw.buf[di] = lv_color_make(r, g, b);
        }
    }
    lv_obj_invalidate(g_draw.canvas);
    eos_free(raw);
    return jerry_boolean(true);
}

const sni_method_desc_t eos_class_static_methods_draw[] = {
    {.name = "create", .handler = sni_api_eos_draw_create},
    {.name = "setPen", .handler = sni_api_eos_draw_set_pen},
    {.name = "line",   .handler = sni_api_eos_draw_line},
    {.name = "clear",  .handler = sni_api_eos_draw_clear},
    {.name = "size",   .handler = sni_api_eos_draw_size},
    {.name = "setPx",  .handler = sni_api_eos_draw_set_px},
    {.name = "getPx",  .handler = sni_api_eos_draw_get_px},
    {.name = "save",   .handler = sni_api_eos_draw_save},
    {.name = "load",   .handler = sni_api_eos_draw_load},
    {.name = NULL, .handler = NULL},
};

const sni_class_desc_t eos_class_desc_draw = {
    .name = "draw",
    .constructor = NULL,
    .base_class = NULL,
    .methods = NULL,
    .properties = NULL,
    .static_methods = eos_class_static_methods_draw,
    .constants = NULL,
};

const sni_class_desc_t *const eos_api_classes[] = {
    &eos_class_desc_view,
    &eos_class_desc_console,
    &eos_class_desc_config,
    &eos_class_desc_time,
    &eos_class_desc_app_header,
    &eos_class_desc_clock_hand,
    &eos_class_desc_activity,
    &eos_class_desc_permission,
    &eos_class_desc_fs,      /* P0.5 相册 */
    &eos_class_desc_app,     /* P0.5 相册 */
    &eos_class_desc_ime,     /* 笔记: 系统键盘输入 */
    &eos_class_desc_draw,    /* 画图: eos.draw.* */
    NULL,
};

const sni_constant_desc_t eos_root_constants[] = {
    {.name = "FONT_SIZE_LARGE", .type = SNI_CONST_INT, .value.i = EOS_FONT_SIZE_LARGE},
    {.name = "FONT_SIZE_MEDIUM", .type = SNI_CONST_INT, .value.i = EOS_FONT_SIZE_MEDIUM},
    {.name = "FONT_SIZE_SMALL", .type = SNI_CONST_INT, .value.i = EOS_FONT_SIZE_SMALL},
    {.name = "DISPLAY_WIDTH", .type = SNI_CONST_INT, .value.i = EOS_DISPLAY_WIDTH},
    {.name = "DISPLAY_HEIGHT", .type = SNI_CONST_INT, .value.i = EOS_DISPLAY_HEIGHT},
    {.name = "CLOCK_HAND_HOUR", .type = SNI_CONST_INT, .value.i = EOS_CLOCK_HAND_HOUR},
    {.name = "CLOCK_HAND_MINUTE", .type = SNI_CONST_INT, .value.i = EOS_CLOCK_HAND_MINUTE},
    {.name = "CLOCK_HAND_SECOND", .type = SNI_CONST_INT, .value.i = EOS_CLOCK_HAND_SECOND},
    {.name = "ACTIVITY_TYPE_NULL", .type = SNI_CONST_INT, .value.i = EOS_ACTIVITY_TYPE_NULL},
    {.name = "ACTIVITY_TYPE_APP", .type = SNI_CONST_INT, .value.i = EOS_ACTIVITY_TYPE_APP},
    {.name = "ACTIVITY_TYPE_APP_LIST", .type = SNI_CONST_INT, .value.i = EOS_ACTIVITY_TYPE_APP_LIST},
    {.name = "ACTIVITY_TYPE_WATCHFACE", .type = SNI_CONST_INT, .value.i = EOS_ACTIVITY_TYPE_WATCHFACE},
    {.name = "ACTIVITY_TYPE_WATCHFACE_LIST", .type = SNI_CONST_INT, .value.i = EOS_ACTIVITY_TYPE_WATCHFACE_LIST},
    {.name = NULL, .type = SNI_CONST_INT, .value.i = 0},
};

/* Root-level methods on the `eos` object (e.g. eos.roundClip(view)).
 * sni_register_methods is file-static in sni_api_export.c and not reusable,
 * so we mirror ui_register_methods here. */
static const sni_method_desc_t eos_root_static_methods[] = {
    {.name = "roundClip", .handler = sni_api_eos_round_clip},
    {.name = NULL, .handler = NULL},
};

static void sni_api_eos_register_methods(const sni_method_desc_t *methods, jerry_value_t target)
{
    if (!methods)
        return;
    for (size_t i = 0; methods[i].name != NULL && methods[i].handler != NULL; i++)
    {
        jerry_value_t func = jerry_function_external(methods[i].handler);
        if (jerry_value_is_exception(func))
        {
            jerry_value_free(func);
            return;
        }
        jerry_value_t set = jerry_object_set_sz(target, methods[i].name, func);
        jerry_value_free(func);
        jerry_value_free(set);
    }
}

void sni_api_eos_init(void)
{
    eos_api_obj = sni_api_build(eos_api_classes);
    if (!jerry_value_is_object(eos_api_obj))
    {
        EOS_LOG_E("Failed to build ElenixOS API object");
        return;
    }
    if (!sni_api_register_constants(eos_root_constants, eos_api_obj))
    {
        EOS_LOG_E("Failed to register ElenixOS API constants");
    }
    sni_api_eos_register_methods(eos_root_static_methods, eos_api_obj);
}

void sni_api_eos_mount(jerry_value_t realm)
{
    bool result = sni_api_mount(realm, eos_api_obj, EOS_API_NAME);
    if (!result)
    {
        EOS_LOG_E("Failed to mount ElenixOS API");
    }
}
