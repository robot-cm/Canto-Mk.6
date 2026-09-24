/**
 * @file cos_msg_list.h
 * @brief Drop-down message list
 */

#ifndef COS_MSG_LIST_H
#define COS_MSG_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_chrome_manager.h"
#include "cos_swipe_panel.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef struct cos_msg_list_t cos_msg_list_t; // Forward declaration

/**
 * @brief Message list item structure
 * Hierarchy:
 * container{
 * row1[icon title_label   time_label]
 *      msg_label
 * }
 */
typedef struct
{
    cos_msg_list_t *msg_list;
    lv_obj_t *container;
    lv_obj_t *row1;
    lv_obj_t *icon;
    lv_obj_t *title_label;
    lv_obj_t *msg_label;
    lv_obj_t *time_label;
    const char *msg_str; /**< Message string */
    bool is_deleted;
} cos_msg_list_item_t;

/**
 * @brief Message list structure
 * Hierarchy:
 * swipe_obj{
 *      list{
 *          clear_all_btn
 *          msg_item
 *          ...
 *          msg_item
 *      }
 *      no_msg_label
 * }
 */
struct cos_msg_list_t
{
    cos_swipe_panel_t *swipe_panel; /**< Drag object pointer */
    lv_obj_t *list; /**< List object pointer */
    lv_obj_t *clear_all_btn; /**< Clear all messages button pointer */
    lv_obj_t *no_msg_label; /**< No message prompt label */
    uint16_t animating_count; /**< Number of animating messages after clear-all press */
};

/* Public function prototypes --------------------------------*/
/**
 * @brief Create message item
 * @param list Parent message list of the message item
 * @return cos_msg_list_item_t* Pointer to the created message item (dynamic memory allocation)
 */
cos_msg_list_item_t *cos_msg_list_item_create(cos_msg_list_t *list);
/**
 * @brief Delete message item
 * @param item Pointer to the message item to delete
 */
void cos_msg_list_item_delete(cos_msg_list_item_t *item);
/**
 * @brief Set message content
 * @param item Target message item
 * @param msg Message string
 */
void cos_msg_list_item_set_msg(cos_msg_list_item_t *item, const char *msg);

/**
 * @brief Set title
 * @param item Target message item
 * @param title Message title (APP) string
 */
void cos_msg_list_item_set_title(cos_msg_list_item_t *item, const char *title);

/**
 * @brief Set time text
 * @param item Target message item
 * @param time Message receive time string (e.g.: "12:30", "One hour ago")
 */
void cos_msg_list_item_set_time(cos_msg_list_item_t *item, const char *time);
/**
 * @brief Set icon
 * @param item Target message item
 * @param src Image source
 */
void cos_msg_list_item_icon_set_src(cos_msg_list_item_t *item, const char *src);
/**
 * @brief Delete message list
 * @param list Target list
 */
void cos_msg_list_delete(cos_msg_list_t *list);
/**
 * @brief Get message list instance
 * @return cos_msg_list_t*
 */
cos_msg_list_t *cos_msg_list_get_instance(void);
/**
 * @brief Initialize message list
 */
void cos_msg_list_init(void);
/**
 * @brief Close detail page if open (for chrome manager integration)
 */
void cos_msg_list_close_detail(void);
/**
 * @brief Show message list
 */
void cos_msg_list_show(void);
/**
 * @brief Hide message list
 */
void cos_msg_list_hide(void);
const cos_chrome_overlay_t *cos_msg_list_get_overlay_descriptor(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_MSG_LIST_H */
