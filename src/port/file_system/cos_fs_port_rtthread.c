/**
 * @file cos_fs_port_rtthread.c
 * @brief File system porting to RTThread OS
 */

#include "cos_config.h"

#if COS_FS_TYPE == COS_FS_RTTHREAD

#include "cos_fs_port.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "cos_log.h"

/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

void cos_fs_set_root(const char *root)
{
    (void)root;
}

const char *cos_fs_realpath(const char *path, char *buf, size_t bufsz)
{
    if (!path || !buf || bufsz == 0)
        return NULL;
    snprintf(buf, bufsz, "%s", path);
    return buf;
}

cos_file_t cos_fs_open_read(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    return open(path, O_RDONLY, 0);
}

cos_file_t cos_fs_open_write(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
}

int cos_fs_read(cos_file_t fp, void *buf, size_t len)
{
    if (fp < 0 || !buf)
        return COS_ERR_IO;
    int n = read(fp, buf, len);
    return n >= 0 ? n : -1;
}

int cos_fs_write(cos_file_t fp, const void *buf, size_t len)
{
    if (fp < 0 || !buf)
        return COS_ERR_IO;
    int n = write(fp, buf, len);
    return n == (int)len ? n : -1;
}

cos_result_t cos_fs_seek(cos_file_t fp, uint32_t pos)
{
    if (fp < 0)
        return COS_ERR_IO;
    int r = lseek(fp, pos, SEEK_SET);
    return r >= 0 ? COS_OK : COS_ERR_IO;
}

cos_result_t cos_fs_size(cos_file_t fp, uint32_t *size)
{
    if (fp < 0 || !size)
        return COS_ERR_IO;
    struct stat st;
    if (fstat(fp, &st) != 0)
        return COS_ERR_IO;
    *size = st.st_size;
    return COS_OK;
}

cos_result_t cos_fs_tell(cos_file_t fp, uint32_t *pos)
{
    if (fp < 0 || !pos)
        return COS_ERR_IO;
    off_t cur = lseek(fp, 0, SEEK_CUR);
    if (cur < 0)
        return COS_ERR_IO;
    *pos = (uint32_t)cur;
    return COS_OK;
}

void cos_fs_close(cos_file_t fp)
{
    if (fp >= 0)
        close(fp);
}

cos_result_t cos_fs_mkdir(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    return mkdir(path, 0755) == 0 ? COS_OK : COS_ERR_IO;
}

cos_result_t cos_fs_rmdir(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    return rmdir(path) == 0 ? COS_OK : COS_ERR_IO;
}

cos_result_t cos_fs_remove(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    return unlink(path) == 0 ? COS_OK : COS_ERR_IO;
}

int cos_fs_exists(const char *path)
{
    if (!path)
        return COS_OK;
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

int cos_fs_type(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return COS_FS_TYPE_NOT_EXIST;
    if (S_ISDIR(st.st_mode))
        return COS_FS_TYPE_DIR;
    return COS_FS_TYPE_FILE;
}

cos_dir_t cos_fs_opendir(const char *path)
{
    return opendir(path);
}

cos_result_t cos_fs_readdir(cos_dir_t dir, char *name, size_t max_len)
{
    if (!dir)
        return COS_ERR_IO;
    struct dirent *entry = readdir(dir);
    if (!entry)
        return COS_ERR_IO;
    strncpy(name, entry->d_name, max_len - 1);
    name[max_len - 1] = '\0';
    return COS_OK;
}

void cos_fs_closedir(cos_dir_t dir)
{
    if (dir)
        closedir(dir);
}

cos_result_t cos_fs_mv(const char *old_path, const char *new_path)
{
    return rename(old_path, new_path) == 0 ? COS_OK : COS_ERR_IO;
}

cos_result_t cos_fs_sync(cos_file_t fp)
{
    if (fp < 0)
        return COS_ERR_IO;
    return fsync(fp) == 0 ? COS_OK : COS_ERR_IO;
}

#endif /* COS_FS_TYPE */
