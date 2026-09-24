/**
 * @file cos_pkg_mgr.h
 * @brief Package manager
 */

#ifndef COS_PKG_MGR_H
#define COS_PKG_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_core.h"
#include "cos_app.h"
#include "cos_watchface.h"
#include "script_engine_core.h"
/* Public macros ----------------------------------------------*/
#define COS_PKG_APP_MAGIC "EAPK"
#define COS_PKG_WATCHFACE_MAGIC "EWPK"
#define COS_PKG_READ_BLOCK 512 /*< Block size for data reading */
#define COS_PKG_NAME_LEN_MAX 256 /*< Last byte is forced to "\0", maximum name length 255 bytes */
#define COS_PKG_ID_LEN_MAX 256 /*< Last byte is forced to "\0", maximum name length 255 bytes */
#define COS_PKG_VERSION_LEN_MAX 256 /*< Last byte is forced to "\0", maximum name length 255 bytes */

#define COS_PKG_MAGIC_OFFSET 0
#define COS_PKG_NAME_OFFSET COS_PKG_MAGIC_OFFSET + 4
#define COS_PKG_ID_OFFSET COS_PKG_NAME_OFFSET + COS_PKG_NAME_LEN_MAX
#define COS_PKG_VERSION_OFFSET COS_PKG_ID_OFFSET + COS_PKG_ID_LEN_MAX
#define COS_PKG_MIN_API_OFFSET COS_PKG_VERSION_OFFSET + COS_PKG_VERSION_LEN_MAX
#define COS_PKG_TARGET_API_OFFSET COS_PKG_MIN_API_OFFSET + 2
#define COS_PKG_FILE_COUNT_OFFSET COS_PKG_TARGET_API_OFFSET + 2
#define COS_PKG_RESERVED_OFFSET COS_PKG_FILE_COUNT_OFFSET + 4
#define COS_PKG_TABLE_OFFSET COS_PKG_RESERVED_OFFSET + 4

/* Public typedefs --------------------------------------------*/
/**
 * @brief Define package file header
 */
typedef struct
{
    char magic[4]; // Magic Number
    char pkg_name[COS_PKG_NAME_LEN_MAX]; // Package name
    char pkg_id[COS_PKG_ID_LEN_MAX]; // Software ID
    char pkg_version[COS_PKG_VERSION_LEN_MAX]; // Software version
    uint16_t min_api_level; // Minimum required API level
    uint16_t target_api_level; // Target API level
    uint32_t file_count; // File count
    uint32_t reserved; // Reserved field for future expansion
} cos_pkg_header_t;

/**
 * @brief This structure is not used, but cos_pkg_mgr_unpack parses according to this structure
 * This structure is used to define the information structure of a single file/directory
 ***********************************
    typedef struct
    {
        uint32_t name_len;  // File name length
        char name[];        // File name (variable length)
        uint32_t is_dir;    // Whether directory (0=file,1=directory)
        uint32_t offset;    // Offset of data in package
        uint32_t size;      // File size
    } cos_pkg_entry_t;
 ************************************/

/* Public function prototypes --------------------------------*/

/**
 * @brief Release and clear data inside script package
 * @param pkg Target script package
 */
void cos_pkg_free(script_pkg_t *pkg);

/**
 * @brief Read package header
 * @param pkg_path Package path
 * @param header Package header structure pointer
 * @return cos_result_t Execution result
 */
cos_result_t cos_pkg_read_header(const char *pkg_path, cos_pkg_header_t *header);

/**
 * @brief Unpack EAPK/EWPK files (e.g., app.eapk, watchface.ewpk)
 * @param pkg_path Package file path
 * @param output_path Output directory
 * @param pkg_type Package type (SCRIPT_TYPE_APPLICATION/SCRIPT_TYPE_WATCHFACE)
 * @return cos_result_t Execution result
 */
cos_result_t cos_pkg_mgr_unpack(const char *pkg_path, const char *output_path, const script_pkg_type_t pkg_type);
#ifdef __cplusplus
}
#endif

#endif /* COS_PKG_MGR_H */
