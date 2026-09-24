/**
 * @file cos_lvgl_fs.c
 * @brief LVGL file system interface implementation using Canto Mk.6 storage service
 */

#include "cos_lvgl_fs.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cos_config.h"
#include "cos_log.h"
#include "cos_service_storage.h"
#include "cos_mem.h"

/* Macros and Definitions -------------------------------------*/

#if !defined(COS_LVGL_FS_LETTER)
#error "COS_LVGL_FS_LETTER is not defined in cos_config.h"
#endif

/**
 * LVGL FS must route to Canto Mk.6 storage backend.
 * Ensure the default driver letter matches COS driver.
 *
 * Note:
 *   Applications should always use POSIX-style paths ("/xxx").
 */
#if LV_FS_DEFAULT_DRIVER_LETTER != COS_LVGL_FS_LETTER
#error "LV_FS_DEFAULT_DRIVER_LETTER must match COS_LVGL_FS_LETTER"
#endif

#define LVGL_FS_MAX_PATH COS_FS_PATH_MAX

typedef struct
{
    cos_file_t file_handle;
    cos_dir_t dir_handle;
    uint8_t type; // 0: file, 1: directory
} cos_lvgl_fs_handle_t;

/* Variables --------------------------------------------------*/
static lv_fs_drv_t fs_drv = {0};

/* Function Implementations -----------------------------------*/

static void *_drv_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv; // unused

    if (!path)
        return NULL;

    COS_LOG_I("[LVGL_FS] open path: %s, mode: %d", path, (int)mode);

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)cos_malloc(sizeof(cos_lvgl_fs_handle_t));
    if (!handle)
        return NULL;

    memset(handle, 0, sizeof(cos_lvgl_fs_handle_t));
    handle->type = 0; // file

    cos_file_t fp = COS_FILE_INVALID;

    if (mode == (LV_FS_MODE_RD | LV_FS_MODE_WR))
    {
        // Read-write mode not supported, fallback to read
        fp = cos_storage_file_open_read(path);
    }
    else if (mode & LV_FS_MODE_WR)
    {
        // Write mode
        fp = cos_storage_file_open_write(path);
    }
    else if (mode & LV_FS_MODE_RD)
    {
        // Read mode
        fp = cos_storage_file_open_read(path);
    }

    if (fp == COS_FILE_INVALID)
    {
        /* Normal on first run / missing optional resources: keep as WARN so
         * genuine errors are not drowned by expected "file absent" noise. */
        COS_LOG_W("Failed to open file: %s", path);
        cos_free(handle);
        return NULL;
    }

    COS_LOG_I("[LVGL_FS] successfully opened: %s", path);
    handle->file_handle = fp;
    return (void *)handle;
}

static lv_fs_res_t _drv_close_cb(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv; // unused

    if (!file_p)
        return LV_FS_RES_INV_PARAM;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)file_p;

    if (handle->type == 0) // file
    {
        cos_storage_file_close(handle->file_handle);
    }
    else if (handle->type == 1) // directory
    {
        cos_storage_dir_close(handle->dir_handle);
    }

    cos_free(handle);
    return LV_FS_RES_OK;
}

static lv_fs_res_t _drv_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv; // unused

    if (!file_p || !buf || !br)
        return LV_FS_RES_INV_PARAM;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)file_p;

    if (handle->type != 0) // not a file
        return LV_FS_RES_FS_ERR;

    ssize_t bytes_read = cos_storage_file_read(handle->file_handle, buf, btr);

    if (bytes_read < 0)
    {
        COS_LOG_E("Failed to read file");
        return LV_FS_RES_FS_ERR;
    }

    *br = (uint32_t)bytes_read;
    return LV_FS_RES_OK;
}

static lv_fs_res_t _drv_write_cb(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw)
{
    (void)drv; // unused

    if (!file_p || !buf || !bw)
        return LV_FS_RES_INV_PARAM;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)file_p;

    if (handle->type != 0) // not a file
        return LV_FS_RES_FS_ERR;

    ssize_t bytes_written = cos_storage_file_write(handle->file_handle, buf, btw);

    if (bytes_written < 0)
    {
        COS_LOG_E("Failed to write file");
        return LV_FS_RES_FS_ERR;
    }

    *bw = (uint32_t)bytes_written;
    return LV_FS_RES_OK;
}

static lv_fs_res_t _drv_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv; // unused

    if (!file_p)
        return LV_FS_RES_INV_PARAM;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)file_p;

    if (handle->type != 0) // not a file
        return LV_FS_RES_FS_ERR;

    /* Translate LVGL's whence into an absolute offset understood by the
     * storage layer (which only does absolute seeks). SEEK_END is required
     * by lodepng's file-size probe (lv_fs_seek(END) + lv_fs_tell), so without
     * this, every file-backed PNG failed to load with "Failed to open image". */
    uint32_t abs_pos = pos;
    if (whence == LV_FS_SEEK_CUR)
    {
        uint32_t cur = 0;
        if (cos_storage_file_tell(handle->file_handle, &cur) != COS_OK)
            return LV_FS_RES_FS_ERR;
        abs_pos = cur + pos;
    }
    else if (whence == LV_FS_SEEK_END)
    {
        uint32_t fsize = 0;
        if (cos_storage_file_size(handle->file_handle, &fsize) != COS_OK)
            return LV_FS_RES_FS_ERR;
        abs_pos = fsize + pos;
    }
    /* LV_FS_SEEK_SET: abs_pos already equals pos */

    if (cos_storage_file_seek(handle->file_handle, abs_pos) != COS_OK)
    {
        COS_LOG_E("Failed to seek in file");
        return LV_FS_RES_FS_ERR;
    }

    return LV_FS_RES_OK;
}

static lv_fs_res_t _drv_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    (void)drv; // unused

    if (!file_p || !pos_p)
        return LV_FS_RES_INV_PARAM;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)file_p;

    if (handle->type != 0) // not a file
        return LV_FS_RES_FS_ERR;

    if (cos_storage_file_tell(handle->file_handle, pos_p) != COS_OK)
    {
        COS_LOG_E("Failed to get file position");
        return LV_FS_RES_FS_ERR;
    }

    return LV_FS_RES_OK;
}

static void *_drv_dir_open_cb(lv_fs_drv_t *drv, const char *path)
{
    (void)drv; // unused

    if (!path)
        return NULL;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)cos_malloc(sizeof(cos_lvgl_fs_handle_t));
    if (!handle)
        return NULL;

    memset(handle, 0, sizeof(cos_lvgl_fs_handle_t));
    handle->type = 1; // directory

    cos_dir_t dir = cos_storage_dir_open(path);
    if (dir == NULL)
    {
        COS_LOG_E("Failed to open directory: %s", path);
        cos_free(handle);
        return NULL;
    }

    handle->dir_handle = dir;
    return (void *)handle;
}

static lv_fs_res_t _drv_dir_read_cb(lv_fs_drv_t *drv, void *rddir_p, char *fn, uint32_t fn_len)
{
    (void)drv; // unused

    if (!rddir_p || !fn || fn_len == 0)
        return LV_FS_RES_INV_PARAM;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)rddir_p;

    if (handle->type != 1) // not a directory
        return LV_FS_RES_FS_ERR;

    if (cos_storage_dir_read(handle->dir_handle, fn, fn_len) != COS_OK)
    {
        // End of directory or error
        return LV_FS_RES_FS_ERR;
    }

    return LV_FS_RES_OK;
}

static lv_fs_res_t _drv_dir_close_cb(lv_fs_drv_t *drv, void *rddir_p)
{
    (void)drv; // unused

    if (!rddir_p)
        return LV_FS_RES_INV_PARAM;

    cos_lvgl_fs_handle_t *handle = (cos_lvgl_fs_handle_t *)rddir_p;

    if (handle->type != 1) // not a directory
        return LV_FS_RES_FS_ERR;

    cos_storage_dir_close(handle->dir_handle);
    cos_free(handle);

    return LV_FS_RES_OK;
}

void cos_lvgl_fs_register(void)
{
    lv_fs_drv_init(&fs_drv); /*Basic initialization*/

    fs_drv.letter = COS_LVGL_FS_LETTER; /*An uppercase letter to identify the drive */
    fs_drv.cache_size = 0; /*Cache size for reading in bytes. 0 to not cache.*/

    fs_drv.ready_cb = NULL; /*Callback to tell if the drive is ready to use */
    fs_drv.open_cb = _drv_open_cb; /*Callback to open a file */
    fs_drv.close_cb = _drv_close_cb; /*Callback to close a file */
    fs_drv.read_cb = _drv_read_cb; /*Callback to read a file */
    fs_drv.write_cb = _drv_write_cb; /*Callback to write a file */
    fs_drv.seek_cb = _drv_seek_cb; /*Callback to seek in a file (Move cursor) */
    fs_drv.tell_cb = _drv_tell_cb; /*Callback to tell the cursor position  */

    fs_drv.dir_open_cb = _drv_dir_open_cb; /*Callback to open directory to read its content */
    fs_drv.dir_read_cb = _drv_dir_read_cb; /*Callback to read a directory's content */
    fs_drv.dir_close_cb = _drv_dir_close_cb; /*Callback to close a directory */

    fs_drv.user_data = NULL; /*Any custom data if required*/

    lv_fs_drv_register(&fs_drv);

    COS_LOG_I("LVGL file system driver registered");
}
