/**
 * @file cantomk6_os.h
 * @brief Just include this file externally
 */

#ifndef CANTOMK6_OS_H
#define CANTOMK6_OS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include "cos_config.h"
#include "cos_core.h"
#if COS_USE_VIRTUAL_DISPLAY
#include "cos_virtual_display.h"
#endif /* COS_USE_VIRTUAL_DISPLAY */
#include "cos_side_button.h"
#include "cos_crown.h"
#include "cos_log.h"
#include "cos_error.h"
#include "cos_device.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* CANTOMK6_OS_H */
