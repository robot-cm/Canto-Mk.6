/**
 * @file cos_service_storage.c
 * @brief Storage service file operation utilities
 */

#include "cos_service_storage.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_config.h"
#include "cos_log.h"
#include "cos_mem.h"
#include "cos_dfw.h"
#include "cJSON.h"
#include "cos_lvgl_fs.h"
/* Macros and Definitions -------------------------------------*/
#define _FILE_NAME_MAX_LENGTH 256
/* Variables --------------------------------------------------*/

/* Path handling utilities -------------------------------------*/

bool cos_storage_is_valid_filename(const char *name)
{
    if (!name || name[0] == '\0')
    {
        COS_LOG_E("Filename NULL");
        return false;
    }

    const char *invalid_chars = "/\\:*?\"<>|";

    for (const char *p = name; *p; p++)
    {
        if ((unsigned char)*p < 32)
        {
            COS_LOG_E("Filename control char");
            return false;
        }
        if (strchr(invalid_chars, *p))
        {
            COS_LOG_E("Filename invalid char");
            return false;
        }
    }
    return true;
}

/* Function Implementations -----------------------------------*/

/************************** JSON Storage API **************************/

cJSON *cos_storage_json_load(const char *path)
{
    if (!cos_storage_is_file(path))
    {
        return NULL;
    }

    char *content = cos_storage_read_file(path);
    if (!content)
    {
        return NULL;
    }

    cJSON *root = cJSON_Parse(content);
    cos_free(content);

    return root;
}

cos_result_t cos_storage_json_save(const char *path, cJSON *root)
{
    COS_CHECK_PTR_RETURN_VAL(path && root, COS_ERR_INVALID_ARG);

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str)
    {
        return COS_ERR_JSON_ERROR;
    }

    cos_result_t ret = cos_storage_write_file(path, json_str, strlen(json_str));
    cJSON_free(json_str);

    return ret;
}

bool cos_storage_json_get_bool(const char *path, const char *key, bool default_value)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        return default_value;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    bool result = (item && cJSON_IsBool(item)) ? cJSON_IsTrue(item) : default_value;

    cJSON_Delete(root);
    return result;
}

cos_result_t cos_storage_json_set_bool(const char *path, const char *key, bool value)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
        {
            return COS_ERR_MEM;
        }
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
    {
        cJSON_SetBoolValue(item, value);
    }
    else
    {
        cJSON_AddBoolToObject(root, key, value);
    }

    cos_result_t ret = cos_storage_json_save(path, root);
    cJSON_Delete(root);

    return ret;
}

char *cos_storage_json_get_string(const char *path, const char *key, const char *default_value)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        return cos_strdup(default_value);
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    char *result = (item && cJSON_IsString(item)) ? cos_strdup(item->valuestring) : cos_strdup(default_value);

    cJSON_Delete(root);
    return result;
}

cos_result_t cos_storage_json_set_string(const char *path, const char *key, const char *value)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
        {
            return COS_ERR_MEM;
        }
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
    {
        cJSON_SetValuestring(item, value);
    }
    else
    {
        cJSON_AddStringToObject(root, key, value);
    }

    cos_result_t ret = cos_storage_json_save(path, root);
    cJSON_Delete(root);

    return ret;
}

double cos_storage_json_get_number(const char *path, const char *key, double default_value)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        return default_value;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    double result = (item && cJSON_IsNumber(item)) ? item->valuedouble : default_value;

    cJSON_Delete(root);
    return result;
}

cos_result_t cos_storage_json_set_number(const char *path, const char *key, double value)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
        {
            return COS_ERR_MEM;
        }
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
    {
        cJSON_SetNumberValue(item, value);
    }
    else
    {
        cJSON_AddNumberToObject(root, key, value);
    }

    cos_result_t ret = cos_storage_json_save(path, root);
    cJSON_Delete(root);

    return ret;
}

cJSON *cos_storage_json_get_json(const char *path, const char *key)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (!item || (!cJSON_IsObject(item) && !cJSON_IsArray(item)))
    {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_DetachItemViaPointer(root, item);
    cJSON_Delete(root);

    return item;
}

cos_result_t cos_storage_json_set_json(const char *path, const char *key, cJSON *json_value)
{
    cJSON *root = cos_storage_json_load(path);
    if (!root)
    {
        root = cJSON_CreateObject();
        if (!root)
        {
            return COS_ERR_MEM;
        }
    }

    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item)
    {
        cJSON_ReplaceItemInObject(root, key, json_value);
    }
    else
    {
        cJSON_AddItemToObject(root, key, json_value);
    }

    cos_result_t ret = cos_storage_json_save(path, root);
    cJSON_Delete(root);

    return ret;
}

cos_result_t cos_storage_json_create_if_not_exist(const char *path, const char *default_json)
{
    if (cos_storage_is_file(path))
    {
        return COS_OK;
    }

    const char *content = default_json ? default_json : "{}";
    return cos_storage_create_file_if_not_exist(path, content);
}

bool cos_storage_is_dir(const char *path)
{
    return (cos_fs_type(path) == COS_FS_TYPE_DIR) ? true : false;
}

bool cos_storage_is_file(const char *path)
{
    return (cos_fs_type(path) == COS_FS_TYPE_FILE) ? true : false;
}

cos_result_t cos_storage_puts(const char *s, cos_file_t fp)
{
    if (fp == COS_FILE_INVALID || !s)
        return COS_ERR_INVALID_ARG;

    ssize_t written = cos_fs_write(fp, s, strlen(s));
    return (written < 0) ? COS_ERR_IO : COS_OK;
}

cos_result_t cos_storage_mkdir_if_not_exist(const char *path)
{
    int type = cos_fs_type(path);
    if (type == COS_FS_TYPE_DIR)
    {
        return COS_OK;
    }

    if (type == COS_FS_TYPE_FILE)
    {
        return COS_ERR_ALREADY_EXISTS;
    }

    if (type == COS_FS_TYPE_NOT_EXIST)
    {
        return (cos_fs_mkdir(path) == COS_OK) ? COS_OK : COS_ERR_FILE_ERROR;
    }

    return COS_ERR_FILE_ERROR;
}

cos_result_t cos_storage_create_file_if_not_exist(const char *path, const char *default_content)
{
    int type = cos_fs_type(path);
    if (type == COS_FS_TYPE_FILE)
    {
        return COS_OK;
    }

    if (type == COS_FS_TYPE_DIR)
    {
        return COS_ERR_ALREADY_EXISTS;
    }

    if (type == COS_FS_TYPE_NOT_EXIST)
    {
        cos_file_t fp = cos_fs_open_write(path);
        if (fp == COS_FILE_INVALID)
            return COS_ERR_FILE_ERROR;

        if (default_content)
        {
            ssize_t len = strlen(default_content);
            ssize_t written = cos_fs_write(fp, default_content, len);
            if (written != len)
            {
                COS_LOG_E("write %s failed, written=%zd", path, written);
                cos_fs_close(fp);
                return COS_ERR_IO;
            }
        }

        cos_fs_close(fp);
        COS_LOG_I("Created file: %s", path);
        return COS_OK;
    }

    return COS_ERR_FILE_ERROR;
}

cos_result_t cos_storage_mkdir_recursive(const char *path)
{
    char tmp[_FILE_NAME_MAX_LENGTH];
    char *p = NULL;
    size_t len;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    len = strlen(tmp);

#if COS_FS_TYPE == COS_FS_FATFS
    if (len > 0 && (tmp[len - 1] == '\\' || tmp[len - 1] == '/'))
    {
        tmp[len - 1] = '\0';
        len--;
    }
#else
    if (len > 0 && tmp[len - 1] == '/')
    {
        tmp[len - 1] = '\0';
        len--;
    }
#endif

    if (len >= sizeof(tmp) - 1)
    {
        return COS_ERR_PATH_TOO_LONG;
    }

    p = tmp;
#if COS_FS_TYPE == COS_FS_FATFS
    if (len >= 2 && tmp[1] == ':')
    {
        p = tmp + 2;
        if (*p == '\\' || *p == '/')
        {
            p++;
        }
    }
#else
    if (tmp[0] == '/')
    {
        p = tmp + 1;
    }
#endif

    for (; *p; p++)
    {
#if COS_FS_TYPE == COS_FS_FATFS
        if (*p == '\\' || *p == '/')
        {
#else
        if (*p == '/')
        {
#endif
            *p = '\0';

            int type = cos_fs_type(tmp);
            if (type == COS_FS_TYPE_NOT_EXIST)
            {
                if (cos_fs_mkdir(tmp) != COS_OK)
                {
                    return COS_ERR_FILE_ERROR;
                }
            }
            else if (type != COS_FS_TYPE_DIR)
            {
                return COS_ERR_INVALID_STATE;
            }

#if COS_FS_TYPE == COS_FS_FATFS
            *p = '\\';
#else
            *p = '/';
#endif
        }
    }

    int type = cos_fs_type(tmp);
    if (type == COS_FS_TYPE_NOT_EXIST)
    {
        if (cos_fs_mkdir(tmp) != COS_OK)
        {
            return COS_ERR_FILE_ERROR;
        }
    }
    else if (type != COS_FS_TYPE_DIR)
    {
        return COS_ERR_INVALID_STATE;
    }

    return COS_OK;
}

cos_result_t cos_storage_write_file_immediate(const char *path, const void *data, size_t data_size)
{
    COS_CHECK_PTR_RETURN_VAL(data, COS_ERR_INVALID_ARG);
    if (data_size == 0)
    {
        return COS_ERR_INVALID_ARG;
    }

    cos_file_t fp = cos_fs_open_write(path);
    if (fp == COS_FILE_INVALID)
        return COS_ERR_FILE_ERROR;

    ssize_t written = cos_fs_write(fp, data, data_size);

    cos_fs_close(fp);

    return (written == data_size) ? COS_OK : COS_ERR_IO;
}

char *cos_storage_read_file_immediate(const char *path)
{
    cos_file_t fp = cos_fs_open_read(path);
    if (fp == COS_FILE_INVALID)
    {
        COS_LOG_E("Failed to open file: %s", path);
        return NULL;
    }

    uint32_t file_size = 0;
    cos_fs_size(fp, &file_size);

    if (file_size <= 0)
    {
        COS_LOG_E("Invalid file size");
        cos_fs_close(fp);
        return NULL;
    }

    char *buf = cos_malloc(file_size + 1);
    if (!buf)
    {
        COS_LOG_E("Failed to allocate memory for file");
        cos_fs_close(fp);
        return NULL;
    }

    ssize_t bytes_read = cos_fs_read(fp, buf, file_size);

    if (bytes_read != file_size)
    {
        COS_LOG_E("Failed to read complete file (read %zd of %ld bytes)", bytes_read, file_size);
        cos_fs_close(fp);
        return NULL;
    }
    cos_fs_close(fp);
    buf[file_size] = '\0';
    return buf;
}

cos_result_t cos_storage_write_file(const char *path, const void *data, size_t data_size)
{
#if COS_DFW_ENABLE
    if (!path || !data || data_size == 0)
    {
        return COS_ERR_INVALID_ARG;
    }
    return cos_dfw_write(path, (const uint8_t *)data, data_size) ? COS_OK : COS_ERR_FILE_ERROR;
#else
    return cos_storage_write_file_immediate(path, data, data_size);
#endif
}

char *cos_storage_read_file(const char *path)
{
#if COS_DFW_ENABLE
    return (char *)cos_dfw_read(path);
#else
    return cos_storage_read_file_immediate(path);
#endif
}

cos_result_t cos_storage_rm_recursive(const char *path)
{
    if (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0)
    {
        return COS_ERR_INVALID_ARG;
    }

    int type = cos_fs_type(path);

    switch (type)
    {
        case COS_FS_TYPE_NOT_EXIST:
            return COS_OK;

        case COS_FS_TYPE_FILE:
            return (cos_storage_file_remove(path) == COS_OK) ? COS_OK : COS_ERR_FILE_ERROR;

        case COS_FS_TYPE_DIR:
        {
            cos_dir_t dir = cos_storage_dir_open(path);
            if (!dir)
            {
                return COS_ERR_FILE_ERROR;
            }

            char filename[_FILE_NAME_MAX_LENGTH];
            char fullpath[COS_FS_PATH_MAX + COS_FS_NAME_MAX];
            cos_result_t result = COS_OK;

            while (cos_storage_dir_read(dir, filename, sizeof(filename)) == COS_OK)
            {
                if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)
                {
                    continue;
                }

#if COS_FS_TYPE == COS_FS_FATFS
                snprintf(fullpath, sizeof(fullpath), "%s\\%s", path, filename);
#else
                snprintf(fullpath, sizeof(fullpath), "%s/%s", path, filename);
#endif

                if (cos_storage_rm_recursive(fullpath) != COS_OK)
                {
                    result = COS_ERR_FILE_ERROR;
                    break;
                }
            }

            cos_storage_dir_close(dir);

            if (result != COS_OK)
            {
                return result;
            }

            return (cos_fs_rmdir(path) == COS_OK) ? COS_OK : COS_ERR_FILE_ERROR;
        }

        default:
            return COS_ERR_FILE_ERROR;
    }
}

/************************** File Handle API Implementations **************************/

cos_file_t cos_storage_file_open_read(const char *path)
{
    return cos_fs_open_read(path);
}

cos_file_t cos_storage_file_open_write(const char *path)
{
    return cos_fs_open_write(path);
}

void cos_storage_file_close(cos_file_t fp)
{
    cos_fs_close(fp);
}

cos_result_t cos_storage_file_seek(cos_file_t fp, uint32_t offset)
{
    if (fp == COS_FILE_INVALID)
    {
        COS_LOG_E("Invalid file handle");
        return COS_ERR_INVALID_ARG;
    }

    return cos_fs_seek(fp, offset);
}

ssize_t cos_storage_file_read(cos_file_t fp, void *buf, size_t size)
{
    if (fp == COS_FILE_INVALID || !buf)
    {
        COS_LOG_E("Invalid parameters");
        return -1;
    }

    return cos_fs_read(fp, buf, size);
}

ssize_t cos_storage_file_write(cos_file_t fp, const void *buf, size_t size)
{
    if (fp == COS_FILE_INVALID || !buf)
    {
        COS_LOG_E("Invalid parameters");
        return -1;
    }

    return cos_fs_write(fp, buf, size);
}

cos_result_t cos_storage_file_size(cos_file_t fp, uint32_t *size)
{
    if (fp == COS_FILE_INVALID || !size)
    {
        COS_LOG_E("Invalid parameters");
        return COS_ERR_INVALID_ARG;
    }

    return cos_fs_size(fp, size);
}

cos_result_t cos_storage_file_tell(cos_file_t fp, uint32_t *pos)
{
    if (fp == COS_FILE_INVALID || !pos)
    {
        COS_LOG_E("Invalid parameters");
        return COS_ERR_INVALID_ARG;
    }

    return cos_fs_tell(fp, pos);
}

cos_result_t cos_storage_file_remove(const char *path)
{
    return cos_fs_remove(path);
}

cos_dir_t cos_storage_dir_open(const char *path)
{
    return cos_fs_opendir(path);
}

cos_result_t cos_storage_dir_read(cos_dir_t dir, char *name_buf, size_t buf_size)
{
    if (!dir || !name_buf || buf_size == 0)
    {
        COS_LOG_E("Invalid parameters");
        return COS_ERR_INVALID_ARG;
    }

    return cos_fs_readdir(dir, name_buf, buf_size);
}

void cos_storage_dir_close(cos_dir_t dir)
{
    cos_fs_closedir(dir);
}

void cos_service_storage_init(void)
{
    cos_lvgl_fs_register();
#if COS_DFW_ENABLE
    cos_dfw_init();
#endif /* COS_DFW_ENABLE */
    COS_LOG_I("Storage service initialized");
}
