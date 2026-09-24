/**
 * @file cos_input_page.h
 * @brief Interface for the input page of Canto Mk.6
 */

#ifndef COS_INPUT_PAGE_H
#define COS_INPUT_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_core.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Input page operation result
 */
typedef enum
{
    COS_INPUT_RESULT_CANCEL = 0, /**< User cancelled the input */
    COS_INPUT_RESULT_OK = 1, /**< User confirmed the input */
} cos_input_result_t;

/**
 * @brief Input page close callback
 *
 * @note The `text` pointer passed to the callback is transient and only
 *       guaranteed to be valid for the duration of the callback call. If the
 *       caller needs to retain the text after the callback returns it MUST
 *       make its own copy (for example by allocating memory and copying the
 *       string). The framework will free the temporary buffer after the
 *       callback returns.
 *
 * @param text The text from textarea (transient, copy if retained)
 * @param result The operation result (cancel or ok)
 * @param user_data User data passed to the callback
 */
typedef void (*cos_input_close_callback_t)(const char *text, cos_input_result_t result, void *user_data);

/**
 * @brief Open the input page
 * @param label The label object to write the result to (can be NULL if callback is set)
 * @return cos_result_t Result code
 */
cos_result_t cos_input_page_open(lv_obj_t *label);

/**
 * @brief Open the input page with callback
 * @param label The label object to write the result to (can be NULL)
 * @param close_callback The callback function when input page is closed
 * @param user_data User data passed to the callback
 * @return cos_result_t Result code
 */
cos_result_t cos_input_page_open_with_callback(lv_obj_t *label,
                                               cos_input_close_callback_t close_callback,
                                               void *user_data);

/* Public function prototypes --------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* COS_INPUT_PAGE_H */
