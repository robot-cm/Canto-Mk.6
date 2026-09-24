/**
 * @file cos_pkg_mgr.c
 * @brief Package manager
 */

#include "cos_pkg_mgr.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cos_port.h"
#define COS_LOG_TAG "PackageManager"
#include "cos_log.h"
#include "cos_service_storage.h"
#include "cos_mem.h"
/* Macros and Definitions -------------------------------------*/
#define COS_PKG_HEADER_LENGTH COS_PKG_TABLE_OFFSET
/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

void cos_pkg_free(script_pkg_t *pkg)
{
    COS_CHECK_PTR_RETURN(pkg);

    if (pkg->id)
        cos_free((void *)pkg->id);
    if (pkg->name)
        cos_free((void *)pkg->name);
    if (pkg->version)
        cos_free((void *)pkg->version);
    if (pkg->author)
        cos_free((void *)pkg->author);
    if (pkg->description)
        cos_free((void *)pkg->description);
    if (pkg->script_str)
        cos_free((void *)pkg->script_str);
    if (pkg->base_path)
        cos_free((void *)pkg->base_path);
    if (pkg->permissions)
    {
        for (uint8_t i = 0; i < pkg->permission_count; i++)
        {
            if (pkg->permissions[i])
                cos_free((void *)pkg->permissions[i]);
        }
        cos_free(pkg->permissions);
    }
    pkg->id = NULL;
    pkg->name = NULL;
    pkg->type = SCRIPT_TYPE_UNKNOWN;
    pkg->version = NULL;
    pkg->author = NULL;
    pkg->description = NULL;
    pkg->script_str = NULL;
    pkg->base_path = NULL;
    pkg->permissions = NULL;
    pkg->permission_count = 0;
    pkg->min_api_level = 0;
    pkg->target_api_level = 0;
}

cos_result_t cos_pkg_read_header(const char *pkg_path, cos_pkg_header_t *header)
{
    // Validate input parameters
    if (!pkg_path || !header)
    {
        COS_LOG_E("Invalid parameters: pkg_path=%p, header=%p", pkg_path, header);
        return COS_ERR_VAR_NULL;
    }

    // Check if it's a regular file
    if (!cos_storage_is_file(pkg_path))
    {
        COS_LOG_E("Path is not a file: %s", pkg_path);
        return COS_ERR_FILE_ERROR;
    }

    // Open package file
    cos_file_t fp = cos_storage_file_open_read(pkg_path);
    if (fp == COS_FILE_INVALID)
    {
        COS_LOG_E("Failed to open package file: %s", pkg_path);
        return COS_ERR_FILE_ERROR;
    }

    // Initialize header with zeros
    memset(header, 0, sizeof(cos_pkg_header_t));

    // Read magic number
    if (cos_storage_file_seek(fp, COS_PKG_MAGIC_OFFSET) != COS_OK || cos_storage_file_read(fp, header->magic, 4) != 4)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read magic number");
        return COS_ERR_FILE_ERROR;
    }

    // Read package name
    if (cos_storage_file_seek(fp, COS_PKG_NAME_OFFSET) != COS_OK
        || cos_storage_file_read(fp, header->pkg_name, COS_PKG_NAME_LEN_MAX) != COS_PKG_NAME_LEN_MAX)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read package name");
        return COS_ERR_FILE_ERROR;
    }
    header->pkg_name[COS_PKG_NAME_LEN_MAX - 1] = '\0';

    // Read package ID
    if (cos_storage_file_seek(fp, COS_PKG_ID_OFFSET) != COS_OK
        || cos_storage_file_read(fp, header->pkg_id, COS_PKG_ID_LEN_MAX) != COS_PKG_ID_LEN_MAX)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read package id");
        return COS_ERR_FILE_ERROR;
    }
    header->pkg_id[COS_PKG_ID_LEN_MAX - 1] = '\0';

    // Read package version
    if (cos_storage_file_seek(fp, COS_PKG_VERSION_OFFSET) != COS_OK
        || cos_storage_file_read(fp, header->pkg_version, COS_PKG_VERSION_LEN_MAX) != COS_PKG_VERSION_LEN_MAX)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read package version");
        return COS_ERR_FILE_ERROR;
    }
    header->pkg_version[COS_PKG_VERSION_LEN_MAX - 1] = '\0';

    // Read min_api_level
    if (cos_storage_file_seek(fp, COS_PKG_MIN_API_OFFSET) != COS_OK
        || cos_storage_file_read(fp, &header->min_api_level, sizeof(uint16_t)) != sizeof(uint16_t))
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read min_api_level");
        return COS_ERR_FILE_ERROR;
    }

    // Read target_api_level
    if (cos_storage_file_seek(fp, COS_PKG_TARGET_API_OFFSET) != COS_OK
        || cos_storage_file_read(fp, &header->target_api_level, sizeof(uint16_t)) != sizeof(uint16_t))
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read target_api_level");
        return COS_ERR_FILE_ERROR;
    }

    // Read file_count
    if (cos_storage_file_seek(fp, COS_PKG_FILE_COUNT_OFFSET) != COS_OK
        || cos_storage_file_read(fp, &header->file_count, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read file count");
        return COS_ERR_FILE_ERROR;
    }

    // Read reserved field
    if (cos_storage_file_seek(fp, COS_PKG_RESERVED_OFFSET) != COS_OK
        || cos_storage_file_read(fp, &header->reserved, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to read reserved field");
        return COS_ERR_FILE_ERROR;
    }
    cos_storage_file_close(fp);
    COS_LOG_D("\n"
              "=============================\n"
              "Magic: %s | Pkg Name: %s | Pkg Version: %s\n"
              "File Count: %d | Table Offset: %d\n"
              "=============================",
              header->magic,
              header->pkg_name,
              header->pkg_version,
              header->file_count,
              COS_PKG_HEADER_LENGTH);
    return COS_OK;
}

cos_result_t cos_pkg_mgr_unpack(const char *pkg_path, const char *output_path, const script_pkg_type_t pkg_type)
{
    // Open package file
    cos_file_t fp = cos_storage_file_open_read(pkg_path);
    if (fp == COS_FILE_INVALID)
    {
        COS_LOG_E("Failed to open package file");
        return COS_ERR_FILE_ERROR;
    }

    // Read package header
    cos_pkg_header_t header;
    if (cos_pkg_read_header(pkg_path, &header) != COS_OK)
    {
        COS_LOG_E("Failed to read header");
        cos_storage_file_close(fp);
        return COS_FAILED;
    }

    // Validate magic number
    script_pkg_type_t unpack_type = SCRIPT_TYPE_UNKNOWN;
    if (memcmp(header.magic, COS_PKG_APP_MAGIC, 4) == 0)
    {
        unpack_type = SCRIPT_TYPE_APPLICATION;
    }
    else if (memcmp(header.magic, COS_PKG_WATCHFACE_MAGIC, 4) == 0)
    {
        unpack_type = SCRIPT_TYPE_WATCHFACE;
    }
    else
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Invalid magic number");
        return COS_ERR_FILE_ERROR;
    }

    // Check if package type matches
    if (unpack_type != pkg_type)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Package type mismatch: expected %d, got %d", pkg_type, unpack_type);
        return COS_ERR_VALUE_MISMATCH;
    }

    // Get file size
    uint32_t file_size = 0;
    if (cos_storage_file_size(fp, &file_size) != COS_OK)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to get file size");
        return COS_ERR_FILE_ERROR;
    }

    // Seek to the file table position (immediately after the file header)
    if (cos_storage_file_seek(fp, COS_PKG_TABLE_OFFSET) != COS_OK)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to seek to file table at offset %u", COS_PKG_TABLE_OFFSET);
        return COS_ERR_FILE_ERROR;
    }

    // Create output directory
    if (cos_storage_mkdir_recursive(output_path) != COS_OK)
    {
        cos_storage_file_close(fp);
        COS_LOG_E("Failed to create output directory");
        return COS_ERR_FILE_ERROR;
    }

    // Track the current file table offset instead of querying the underlying file position
    uint32_t table_pos = COS_PKG_TABLE_OFFSET;

    // Process each file entry
    for (uint32_t i = 0; i < header.file_count; i++)
    {
        // Read the file name length
        uint32_t name_len;
        if (cos_storage_file_read(fp, &name_len, sizeof(uint32_t)) != sizeof(uint32_t))
        {
            cos_storage_file_close(fp);
            COS_LOG_E("Failed to read name length for entry %u", i);
            return COS_ERR_FILE_ERROR;
        }
        table_pos += sizeof(uint32_t);

        // Validate the file name length
        if (name_len >= COS_FS_PATH_MAX)
        {
            cos_storage_file_close(fp);
            COS_LOG_E("Name length %u too long for entry %u", name_len, i);
            return COS_ERR_FILE_ERROR;
        }

        // Read the file name
        char name[COS_FS_PATH_MAX];
        if (cos_storage_file_read(fp, name, name_len) != (int)name_len)
        {
            cos_storage_file_close(fp);
            COS_LOG_E("Failed to read name for entry %u", i);
            return COS_ERR_FILE_ERROR;
        }
        name[name_len] = '\0';
        table_pos += name_len;

        // Read the remaining entry fields
        uint32_t is_dir, offset, size;
        if (cos_storage_file_read(fp, &is_dir, sizeof(uint32_t)) != sizeof(uint32_t)
            || cos_storage_file_read(fp, &offset, sizeof(uint32_t)) != sizeof(uint32_t)
            || cos_storage_file_read(fp, &size, sizeof(uint32_t)) != sizeof(uint32_t))
        {
            cos_storage_file_close(fp);
            COS_LOG_E("Failed to read entry fields for %s", name);
            return COS_ERR_FILE_ERROR;
        }
        table_pos += sizeof(uint32_t) * 3;

        // Compute the offset of the next entry in the table
        uint32_t next_entry_pos = table_pos;

        // Build the full output path
        char full_path[COS_FS_PATH_MAX + COS_FS_NAME_MAX] = {0};
        snprintf(full_path, sizeof(full_path), "%s/%s", output_path, name);

        if (is_dir)
        {
            // Create the directory
            if (cos_storage_mkdir_recursive(full_path) != COS_OK)
            {
                cos_storage_file_close(fp);
                COS_LOG_E("Failed to create directory: %s", full_path);
                return COS_ERR_FILE_ERROR;
            }
            COS_LOG_D("Created directory: %s", full_path);
        }
        else
        {
            // Validate the file offset and size
            if (offset < COS_PKG_TABLE_OFFSET || offset >= file_size)
            {
                cos_storage_file_close(fp);
                COS_LOG_E("Invalid file offset: %u for %s", offset, name);
                return COS_ERR_FILE_ERROR;
            }

            if (offset + size > file_size)
            {
                cos_storage_file_close(fp);
                COS_LOG_E("File size overflow: %u+%u=%u for %s", offset, size, offset + size, name);
                return COS_ERR_FILE_ERROR;
            }

            // Ensure the parent directory exists
            char *last_slash = strrchr(full_path, '/');
            if (last_slash)
            {
                *last_slash = '\0';
                if (cos_storage_mkdir_recursive(full_path) != COS_OK)
                {
                    cos_storage_file_close(fp);
                    COS_LOG_E("Failed to create parent directory: %s", full_path);
                    return COS_ERR_FILE_ERROR;
                }
                *last_slash = '/';
            }

            // Create the file and write data
            cos_file_t out_fp = cos_storage_file_open_write(full_path);
            if (out_fp == COS_FILE_INVALID)
            {
                cos_storage_file_close(fp);
                COS_LOG_E("Failed to create file: %s", full_path);
                return COS_ERR_FILE_ERROR;
            }

            // Seek to the file data
            if (cos_storage_file_seek(fp, offset) != COS_OK)
            {
                cos_storage_file_close(out_fp);
                cos_storage_file_close(fp);
                COS_LOG_E("Failed to seek to file data for %s", name);
                return COS_ERR_FILE_ERROR;
            }

            // Read and write the file in chunks
            uint32_t remaining = size;
            uint8_t buffer[COS_PKG_READ_BLOCK];
            while (remaining > 0)
            {
                size_t to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
                int r = cos_storage_file_read(fp, buffer, to_read);
                if (r <= 0)
                {
                    cos_storage_file_close(out_fp);
                    cos_storage_file_close(fp);
                    COS_LOG_E("Failed to read file data for %s", name);
                    return COS_ERR_FILE_ERROR;
                }
                if (cos_storage_file_write(out_fp, buffer, r) != r)
                {
                    cos_storage_file_close(out_fp);
                    cos_storage_file_close(fp);
                    COS_LOG_E("Failed to write file data for %s", name);
                    return COS_ERR_FILE_ERROR;
                }
                remaining -= r;
            }

            cos_storage_file_close(out_fp);
            COS_LOG_D("Created file: %s (size: %u bytes)", full_path, size);

            // Restore the file table position for the next entry before reading the next file name
            if (cos_storage_file_seek(fp, next_entry_pos) != COS_OK)
            {
                cos_storage_file_close(fp);
                COS_LOG_E("Failed to restore table position after extracting %s", name);
                return COS_ERR_FILE_ERROR;
            }
        }

        if (is_dir)
        {
            if (cos_storage_file_seek(fp, next_entry_pos) != COS_OK)
            {
                cos_storage_file_close(fp);
                COS_LOG_E("Failed to seek to next table entry after creating dir %s", full_path);
                return COS_ERR_FILE_ERROR;
            }
        }

        table_pos = next_entry_pos;
    }

    cos_storage_file_close(fp);
    return COS_OK;
}
