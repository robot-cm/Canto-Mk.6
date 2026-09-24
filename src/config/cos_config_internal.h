/**
 * @file cos_config_internal.h
 * @brief Configuration file internal definitions
 */

#ifndef COS_CONFIG_INTERNAL_H
#define COS_CONFIG_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

#define DEBUG 0
#define RELEASE 1

#define NORMAL_MODE 0
#define TEST_MODE 1

#define COS_FONT_C_MULTI 0
#define COS_FONT_TTF 1

#define COS_FONT_TTF_DATA 0
#define COS_FONT_TTF_FILE 1

#define COS_FS_POSIX 0
#define COS_FS_FATFS 1
#define COS_FS_LITTLEFS 2
#define COS_FS_RTTHREAD 3
#define COS_FS_CUSTOM 4

#define COS_MEM_PROVIDER_STDLIB_CLIB 0
#define COS_MEM_PROVIDER_AUTO 1
#define COS_MEM_PROVIDER_CUSTOM 2

#define COS_RTOS_BARE_METAL 0
#define COS_RTOS_FREERTOS 1
#define COS_RTOS_RTTHREAD 2
#define COS_RTOS_CUSTOM 3
#define COS_RTOS_POSIX 4

#define COS_SYSMON_DISABLE 0
#define COS_SYSMON_USE_INTERNAL 1
#define COS_SYSMON_USE_CUSTOM 2

#define COS_LANG_EN 0
#define COS_LANG_ZH 1

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* COS_CONFIG_INTERNAL_H */
