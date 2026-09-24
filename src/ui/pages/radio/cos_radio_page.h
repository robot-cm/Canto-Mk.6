/**
 * @file cos_radio_page.h
 * @brief Radio page - single-selection list page
 */

#ifndef COS_RADIO_PAGE_H
#define COS_RADIO_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
/* Public macros ----------------------------------------------*/

#define COS_INVALID_RADIO_INDEX UINT32_MAX /**< Invalid index value */

/* Public typedefs --------------------------------------------*/

typedef struct cos_radio_page_t cos_radio_page_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Add new option to radio list
 *
 * Option index values increment from 0, for example:
 *
 * ```c
 * cos_radio_page_t *rp = cos_radio_page_create("Title")
 * cos_radio_page_add_item(rp,"Option 1");     // index = 0
 * cos_radio_page_add_item(rp,"Option 2");     // index = 1
 * cos_radio_page_add_item(rp,"Option 3");     // index = 2
 * cos_radio_page_show(rp);                    // enter activity
 * ```
 * @param rp Radio page pointer
 * @param txt Option text string
 * @return uint32_t Returns index value of the radio option on success, otherwise returns `COS_INVALID_RADIO_INDEX`
 */
uint32_t cos_radio_page_add_item(cos_radio_page_t *rp, const char *txt);

/**
 * @brief Add subtitle to the top of radio page
 * @param subtitle Subtitle string
 */
void cos_radio_page_set_subtitle(cos_radio_page_t *rp, const char *subtitle);

/**
 * @brief Add comment to the bottom of radio page
 * @param comment Comment string
 */
void cos_radio_page_set_comment(cos_radio_page_t *rp, const char *comment);

/**
 * @brief Add callback function for radio page selection
 * @param event_cb Standard `lv_event_cb_t`, use `lv_event_get_param()` to get the selected item's index (0-based)
 * @param user_data User data
 */
void cos_radio_page_add_event_cb(cos_radio_page_t *rp, lv_event_cb_t event_cb, void *user_data);

/**
 * @brief Create new radio page (without entering activity)
 *
 * After creating the page, add items via cos_radio_page_add_item(),
 * then call cos_radio_page_show() to enter the activity.
 * @param title Title string
 * @return cos_radio_page_t* Returns radio page pointer on success, NULL on failure
 */
cos_radio_page_t *cos_radio_page_create(const char *title);

/**
 * @brief Show/enter the radio page activity
 *
 * Must be called after all items are added via cos_radio_page_add_item().
 * @param rp Radio page pointer
 */
void cos_radio_page_show(cos_radio_page_t *rp);

/**
 * @brief Check the radio option at specified index
 * @param index Index of the target radio option
 */
void cos_radio_page_check(cos_radio_page_t *rp, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* COS_RADIO_PAGE_H */
