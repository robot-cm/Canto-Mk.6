/**
 * @file cos_fs_port.c
 * @brief File system porting
 */

#include "cos_config.h"

#if COS_FS_TYPE == COS_FS_POSIX

#include "cos_fs_port.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include "cos_port.h"
#include "cos_log.h"
/* Macros and Definitions -------------------------------------*/
#define FS_PATH_BUF_SIZE 512

/* ---- Windows simulator: UTF-8 <-> UTF-16 path conversion ---------------
 * fopen/opendir on Windows interpret narrow paths with the system ANSI
 * code page (GBK on zh-CN), so any UTF-8 path containing CJK gets mojibake
 * (e.g. "测试.txt" lands on disk as "娴嬭瘯.txt"). Real target (ESP32 +
 * FreeRTOS) uses the POSIX branch below, untouched. (Round 38b) */
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
static wchar_t *_cos_utf8_to_wide(const char *utf8)
{
    if (!utf8) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, len);
    return w;
}
static char *_cos_wide_to_utf8(const wchar_t *w)
{
    if (!w) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *s = (char *)malloc((size_t)len);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, len, NULL, NULL);
    return s;
}
static FILE *_cos_fopen_utf8(const char *path, const char *mode)
{
    wchar_t *wp = _cos_utf8_to_wide(path);
    wchar_t *wm = _cos_utf8_to_wide(mode);
    if (!wp || !wm)
    {
        free(wp);
        free(wm);
        return NULL;
    }
    FILE *f = _wfopen(wp, wm);
    free(wp);
    free(wm);
    return f;
}
static int _cos_wstat_utf8(const char *path, struct stat *st)
{
    wchar_t *wp = _cos_utf8_to_wide(path);
    if (!wp) return -1;
    struct _stat wst;
    int r = _wstat(wp, &wst);
    free(wp);
    if (r == 0 && st)
    {
        st->st_mode = wst.st_mode;
        st->st_size = wst.st_size;
    }
    return r;
}
typedef struct
{
    intptr_t handle;
    struct _wfinddata_t data;
    int first;
    int done;
} _cos_win_dir_t;
#endif /* _WIN32 */

/* Variables --------------------------------------------------*/

/** Runtime root directory.
 *  When COS_SYS_ROOT_DIR is defined (simulator build), use it as the filesystem root.
 *  Otherwise defaults to "/" → pass-through. */
#ifdef COS_SYS_ROOT_DIR
static char _s_fs_root[FS_PATH_BUF_SIZE] = COS_SYS_ROOT_DIR;
#else
static char _s_fs_root[FS_PATH_BUF_SIZE] = "/";
#endif

void cos_fs_set_root(const char *root)
{
    if (!root || root[0] == '\0')
    {
        _s_fs_root[0] = '/';
        _s_fs_root[1] = '\0';
        return;
    }
    size_t len = strlen(root);
    while (len > 0 && root[len - 1] == '/')
        len--;
    if (len == 0)
    {
        _s_fs_root[0] = '/';
        _s_fs_root[1] = '\0';
        return;
    }
    snprintf(_s_fs_root, sizeof(_s_fs_root), "%.*s", (int)len, root);
}

const char *cos_fs_realpath(const char *path, char *buf, size_t bufsz)
{
    if (!path || !buf || bufsz == 0)
        return NULL;
    if (path[0] != '/')
    {
        snprintf(buf, bufsz, "%s", path);
        return buf;
    }
    size_t root_len = strlen(_s_fs_root);
    while (root_len > 0 && _s_fs_root[root_len - 1] == '/')
        root_len--;
    if (strncmp(path, _s_fs_root, root_len) == 0)
    {
        snprintf(buf, bufsz, "%s", path);
        return buf;
    }
    snprintf(buf, bufsz, "%.*s%s", (int)root_len, _s_fs_root, path);
    return buf;
}

/* Function Implementations -----------------------------------*/

/* ---------------- POSIX FS implementation ------------------ */

/* Open file read-only */
cos_file_t cos_fs_open_read(const char *path)
{
    if (!path)
        return NULL;
    char resolved[FS_PATH_BUF_SIZE];
#ifdef _WIN32
    return _cos_fopen_utf8(cos_fs_realpath(path, resolved, sizeof(resolved)), "rb");
#else
    FILE *fp = fopen(cos_fs_realpath(path, resolved, sizeof(resolved)), "rb");
    if (fp == NULL)
        COS_LOG_E("[cos_fs] open_read FAIL: '%s' errno=%d (%s) root='%s'",
                  resolved, errno, strerror(errno), _s_fs_root);
    return fp;
#endif
}

/* Open file write-only (create if not exist, overwrite if exist) */
cos_file_t cos_fs_open_write(const char *path)
{
    if (!path)
        return NULL;
    char resolved[FS_PATH_BUF_SIZE];
#ifdef _WIN32
    return _cos_fopen_utf8(cos_fs_realpath(path, resolved, sizeof(resolved)), "wb");
#else
    FILE *fp = fopen(cos_fs_realpath(path, resolved, sizeof(resolved)), "wb");
    if (fp == NULL)
        COS_LOG_E("[cos_fs] open_write FAIL: '%s' errno=%d (%s) root='%s'",
                  resolved, errno, strerror(errno), _s_fs_root);
    return fp;
#endif
}

/* Read file data */
int cos_fs_read(cos_file_t fp, void *buf, size_t len)
{
    if (!fp || !buf)
        return -1;
    size_t n = fread(buf, 1, len, (FILE *)fp);
    if (n == 0 && ferror((FILE *)fp))
        return -1;
    return (int)n;
}

/* Write file data */
int cos_fs_write(cos_file_t fp, const void *buf, size_t len)
{
    if (!fp || !buf)
        return -1;
    size_t n = fwrite(buf, 1, len, (FILE *)fp);
    if (n < len)
        return -1;
    return (int)n;
}

/* File positioning */
cos_result_t cos_fs_seek(cos_file_t fp, uint32_t pos)
{
    if (!fp)
        return COS_ERR_IO;
    return fseek((FILE *)fp, (long)pos, SEEK_SET) == 0 ? COS_OK : COS_ERR_IO;
}

/* Get file size */
cos_result_t cos_fs_size(cos_file_t fp, uint32_t *size)
{
    if (!fp || !size)
        return COS_ERR_IO;
    long cur = ftell((FILE *)fp);
    if (cur < 0)
        return COS_ERR_IO;
    if (fseek((FILE *)fp, 0, SEEK_END) != 0)
        return COS_ERR_IO;
    long end = ftell((FILE *)fp);
    if (end < 0)
        return COS_ERR_IO;
    *size = (uint32_t)end;
    fseek((FILE *)fp, cur, SEEK_SET);
    return COS_OK;
}

/* Get current file position */
cos_result_t cos_fs_tell(cos_file_t fp, uint32_t *pos)
{
    if (!fp || !pos)
        return COS_ERR_IO;
    long cur = ftell((FILE *)fp);
    if (cur < 0)
        return COS_ERR_IO;
    *pos = (uint32_t)cur;
    return COS_OK;
}

/* Close file */
void cos_fs_close(cos_file_t fp)
{
    if (fp)
        fclose((FILE *)fp);
}

/* Create directory (single level directory) */
cos_result_t cos_fs_mkdir(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    char resolved[FS_PATH_BUF_SIZE];
    const char *rp = cos_fs_realpath(path, resolved, sizeof(resolved));
    if (!rp)
        return COS_ERR_IO;
#ifdef _WIN32
    wchar_t *wp = _cos_utf8_to_wide(rp);
    int r = wp ? _wmkdir(wp) : -1;
    free(wp);
    return r == 0 ? COS_OK : COS_ERR_IO;
#else
    int mr = mkdir(rp, 0755);
    if (mr != 0)
        COS_LOG_E("[cos_fs] mkdir FAIL: '%s' errno=%d (%s)", rp, errno, strerror(errno));
    return mr == 0 ? COS_OK : COS_ERR_IO;
#endif
}

/* Remove empty directory */
cos_result_t cos_fs_rmdir(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    char resolved[FS_PATH_BUF_SIZE];
    const char *rp = cos_fs_realpath(path, resolved, sizeof(resolved));
    if (!rp)
        return COS_ERR_IO;
#ifdef _WIN32
    wchar_t *wp = _cos_utf8_to_wide(rp);
    int r = wp ? _wrmdir(wp) : -1;
    free(wp);
    return r == 0 ? COS_OK : COS_ERR_IO;
#else
    return rmdir(rp) == 0 ? COS_OK : COS_ERR_IO;
#endif
}

/* Remove file */
cos_result_t cos_fs_remove(const char *path)
{
    if (!path)
        return COS_ERR_IO;
    char resolved[FS_PATH_BUF_SIZE];
    const char *rp = cos_fs_realpath(path, resolved, sizeof(resolved));
    if (!rp)
        return COS_ERR_IO;
#ifdef _WIN32
    wchar_t *wp = _cos_utf8_to_wide(rp);
    int r = wp ? _wremove(wp) : -1;
    free(wp);
    return r == 0 ? COS_OK : COS_ERR_IO;
#else
    return remove(rp) == 0 ? COS_OK : COS_ERR_IO;
#endif
}

/* Check if file or directory exists */
int cos_fs_exists(const char *path)
{
    if (!path)
        return COS_OK;
    char resolved[FS_PATH_BUF_SIZE];
    const char *rp = cos_fs_realpath(path, resolved, sizeof(resolved));
    if (!rp)
        return COS_OK;
#ifdef _WIN32
    struct stat st;
    return _cos_wstat_utf8(rp, &st) == 0 ? 1 : 0;
#else
    struct stat st;
    return stat(rp, &st) == 0 ? 1 : 0;
#endif
}

int cos_fs_type(const char *path)
{
    char resolved[FS_PATH_BUF_SIZE];
    const char *rp = cos_fs_realpath(path, resolved, sizeof(resolved));
    if (!rp)
        return COS_FS_TYPE_NOT_EXIST;
    struct stat st;
#ifdef _WIN32
    if (_cos_wstat_utf8(rp, &st) != 0)
        return COS_FS_TYPE_NOT_EXIST;
#else
    if (stat(rp, &st) != 0)
        return COS_FS_TYPE_NOT_EXIST;
#endif
    if (S_ISDIR(st.st_mode))
        return COS_FS_TYPE_DIR;
    return COS_FS_TYPE_FILE;
}

cos_dir_t cos_fs_opendir(const char *path)
{
    if (!path)
        return NULL;
    char resolved[FS_PATH_BUF_SIZE];
    const char *rp = cos_fs_realpath(path, resolved, sizeof(resolved));
    if (!rp)
        return NULL;
#ifdef _WIN32
    _cos_win_dir_t *wd = (_cos_win_dir_t *)calloc(1, sizeof(_cos_win_dir_t));
    if (!wd)
        return NULL;
    wchar_t pattern[FS_PATH_BUF_SIZE * 2];
    wchar_t *wp = _cos_utf8_to_wide(rp);
    if (!wp)
    {
        free(wd);
        return NULL;
    }
    _snwprintf(pattern, FS_PATH_BUF_SIZE * 2, L"%s\\*", wp);
    free(wp);
    wd->handle = _wfindfirst(pattern, &wd->data);
    if (wd->handle == -1)
    {
        free(wd);
        return NULL;
    }
    wd->first = 1;
    return (cos_dir_t)wd;
#else
    return opendir(rp);
#endif
}

cos_result_t cos_fs_readdir(cos_dir_t dir, char *name, size_t max_len)
{
    if (!dir)
        return COS_ERR_IO;
#ifdef _WIN32
    _cos_win_dir_t *wd = (_cos_win_dir_t *)dir;
    if (wd->done)
        return COS_ERR_IO;
    if (wd->first)
    {
        wd->first = 0;
    }
    else
    {
        if (_wfindnext(wd->handle, &wd->data) != 0)
        {
            wd->done = 1;
            return COS_ERR_IO;
        }
    }
    char *u8 = _cos_wide_to_utf8(wd->data.name);
    if (!u8)
        return COS_ERR_IO;
    strncpy(name, u8, max_len - 1);
    name[max_len - 1] = '\0';
    free(u8);
    return COS_OK;
#else
    struct dirent *entry = readdir(dir);
    if (!entry)
        return COS_ERR_IO;

    strncpy(name, entry->d_name, max_len - 1);
    name[max_len - 1] = '\0';
    return COS_OK;
#endif
}

void cos_fs_closedir(cos_dir_t dir)
{
    if (!dir)
        return;
#ifdef _WIN32
    _cos_win_dir_t *wd = (_cos_win_dir_t *)dir;
    if (wd->handle != -1)
        _findclose(wd->handle);
    free(wd);
#else
    closedir(dir);
#endif
}

cos_result_t cos_fs_mv(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path)
        return COS_ERR_IO;
    char old_r[FS_PATH_BUF_SIZE], new_r[FS_PATH_BUF_SIZE];
    const char *op = cos_fs_realpath(old_path, old_r, sizeof(old_r));
    const char *np = cos_fs_realpath(new_path, new_r, sizeof(new_r));
    if (!op || !np)
        return COS_ERR_IO;
    if (rename(op, np) != 0)
    {
        perror("rename failed");
        return COS_ERR_IO;
    }
    return COS_OK;
}

cos_result_t cos_fs_sync(cos_file_t fp)
{
    return fflush(fp) == 0 ? COS_OK : COS_ERR_IO;
}

#endif /* COS_FS_TYPE */
