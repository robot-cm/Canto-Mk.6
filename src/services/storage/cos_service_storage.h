/**
 * @file cos_service_storage.h
 * @brief Storage service file operation APIs
 */

#ifndef COS_SERVICE_STORAGE_H
#define COS_SERVICE_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "cos_port.h"
#include "cos_error.h"
#include "cJSON.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Check if it's a valid filename for storage operations
 * @param name File name
 * @return true if valid, false otherwise
 */
bool cos_storage_is_valid_filename(const char *name);

/************************** JSON Storage API **************************/

/**
 * @brief Load JSON file from storage
 * @param path File path
 * @return cJSON root object or NULL on failure
 */
cJSON *cos_storage_json_load(const char *path);

/**
 * @brief Save JSON object to file
 * @param path File path
 * @param root cJSON root object
 * @return Operation result
 */
cos_result_t cos_storage_json_save(const char *path, cJSON *root);

/**
 * @brief Get boolean value from JSON file
 * @param path File path
 * @param key Key name
 * @param default_value Default value if key not found
 * @return Boolean value
 */
bool cos_storage_json_get_bool(const char *path, const char *key, bool default_value);

/**
 * @brief Set boolean value in JSON file
 * @param path File path
 * @param key Key name
 * @param value Boolean value
 * @return Operation result
 */
cos_result_t cos_storage_json_set_bool(const char *path, const char *key, bool value);

/**
 * @brief Get string value from JSON file
 * @param path File path
 * @param key Key name
 * @param default_value Default value if key not found
 * @return Allocated string (must free with cos_free), or default value
 */
char *cos_storage_json_get_string(const char *path, const char *key, const char *default_value);

/**
 * @brief Set string value in JSON file
 * @param path File path
 * @param key Key name
 * @param value String value
 * @return Operation result
 */
cos_result_t cos_storage_json_set_string(const char *path, const char *key, const char *value);

/**
 * @brief Get number value from JSON file
 * @param path File path
 * @param key Key name
 * @param default_value Default value if key not found
 * @return Number value
 */
double cos_storage_json_get_number(const char *path, const char *key, double default_value);

/**
 * @brief Set number value in JSON file
 * @param path File path
 * @param key Key name
 * @param value Number value
 * @return Operation result
 */
cos_result_t cos_storage_json_set_number(const char *path, const char *key, double value);

/**
 * @brief Get JSON object from JSON file
 * @param path File path
 * @param key Key name
 * @return cJSON object (must call cJSON_Delete), or NULL if not found
 */
cJSON *cos_storage_json_get_json(const char *path, const char *key);

/**
 * @brief Set JSON object in JSON file
 * @param path File path
 * @param key Key name
 * @param json_value cJSON object
 * @return Operation result
 */
cos_result_t cos_storage_json_set_json(const char *path, const char *key, cJSON *json_value);

/**
 * @brief Create JSON file with default content if not exist
 * @param path File path
 * @param default_json Default JSON string (NULL for empty object)
 * @return Operation result
 */
cos_result_t cos_storage_json_create_if_not_exist(const char *path, const char *default_json);

/**
 * @brief Check if target path is a directory
 */
bool cos_storage_is_dir(const char *path);

/**
 * @brief Check if target path is a file
 */
bool cos_storage_is_file(const char *path);

/**
 * @brief Write string to file (automatically appends newline at the end)
 * @param s String to write
 * @param fp File pointer (opened by cos_fs_open_write or cos_fs_open_readwrite)
 * @return cos_result_t COS_OK on success, negative error code on failure
 */
cos_result_t cos_storage_puts(const char *s, cos_file_t fp);

/**
 * @brief Check if directory exists, create if it doesn't
 * @param path Directory path
 * @return cos_result_t COS_OK on success, negative error code on failure
 */
cos_result_t cos_storage_mkdir_if_not_exist(const char *path);

/**
 * @brief Create file and write default content if file doesn't exist
 * @param path File path
 * @param default_content Default content to write when file doesn't exist, can be NULL (create empty file)
 * @return cos_result_t COS_OK on success, negative error code on failure
 */
cos_result_t cos_storage_create_file_if_not_exist(const char *path, const char *default_content);

/**
 * @brief Read entire file content into dynamically allocated buffer (can only read text files)
 * @param path File path
 * @return char* Returns dynamically allocated buffer; returns NULL on failure
 * @warning Must use `cos_free` to release memory after using the data
 */
char *cos_storage_read_file(const char *path);

/**
 * @brief Write to file
 * @param path File path
 * @param data Data pointer
 * @param data_size Data length
 * @return cos_result_t COS_OK on success, negative error code on failure
 */
cos_result_t cos_storage_write_file(const char *path, const void *data, size_t data_size);

/**
 * @brief Write to file immediately (bypass deferred writer)
 * @param path File path
 * @param data Data pointer
 * @param data_size Data length
 * @return cos_result_t COS_OK on success, negative error code on failure
 */
cos_result_t cos_storage_write_file_immediate(const char *path, const void *data, size_t data_size);

/**
 * @brief Read entire file content immediately (bypass deferred writer)
 * @param path File path
 * @return char* Returns dynamically allocated buffer; returns NULL on failure
 * @warning Must use `cos_free` to release memory after using the data
 */
char *cos_storage_read_file_immediate(const char *path);

/**
 * @brief Recursively create directory tree
 * @param path Target path, e.g. a/b/c
 * @return cos_result_t COS_OK on success, negative error code on failure
 */
cos_result_t cos_storage_mkdir_recursive(const char *path);

/**
 * @brief Recursively delete directory and all files and subdirectories inside
 * @param path Directory path
 * @return cos_result_t COS_OK on success, negative error code on failure
 */
cos_result_t cos_storage_rm_recursive(const char *path);

/************************** File Handle API **************************/

/**
 * @brief Open file for reading with path validation
 * @param path File path
 * @return File handle or COS_FILE_INVALID on failure
 */
cos_file_t cos_storage_file_open_read(const char *path);

/**
 * @brief Open file for writing with path validation
 * @param path File path
 * @return File handle or COS_FILE_INVALID on failure
 */
cos_file_t cos_storage_file_open_write(const char *path);

/**
 * @brief Close file handle
 * @param fp File handle
 */
void cos_storage_file_close(cos_file_t fp);

/**
 * @brief Seek to offset in file
 * @param fp File handle
 * @param offset Offset to seek to
 * @return COS_OK on success, error code on failure
 */
cos_result_t cos_storage_file_seek(cos_file_t fp, uint32_t offset);

/**
 * @brief Read data from file
 * @param fp File handle
 * @param buf Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read, or negative error code
 */
ssize_t cos_storage_file_read(cos_file_t fp, void *buf, size_t size);

/**
 * @brief Write data to file
 * @param fp File handle
 * @param buf Buffer to write from
 * @param size Number of bytes to write
 * @return Number of bytes written, or negative error code
 */
ssize_t cos_storage_file_write(cos_file_t fp, const void *buf, size_t size);

/**
 * @brief Get file size
 * @param fp File handle
 * @param size Output pointer for file size
 * @return COS_OK on success, error code on failure
 */
cos_result_t cos_storage_file_size(cos_file_t fp, uint32_t *size);

/**
 * @brief Get current file position
 * @param fp File handle
 * @param pos Output pointer for current position
 * @return COS_OK on success, error code on failure
 */
cos_result_t cos_storage_file_tell(cos_file_t fp, uint32_t *pos);

/**
 * @brief Remove file with path validation
 * @param path File path
 * @return COS_OK on success, error code on failure
 */
cos_result_t cos_storage_file_remove(const char *path);

/**
 * @brief Open directory with path validation
 * @param path Directory path
 * @return Directory handle or NULL on failure
 */
cos_dir_t cos_storage_dir_open(const char *path);

/**
 * @brief Read directory entry
 * @param dir Directory handle
 * @param name_buf Buffer to store entry name
 * @param buf_size Size of buffer
 * @return COS_OK on success, error code on failure
 */
cos_result_t cos_storage_dir_read(cos_dir_t dir, char *name_buf, size_t buf_size);

/**
 * @brief Close directory handle
 * @param dir Directory handle
 */
void cos_storage_dir_close(cos_dir_t dir);

/**
 * @brief Initialize storage service
 */
void cos_service_storage_init(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_STORAGE_H */
