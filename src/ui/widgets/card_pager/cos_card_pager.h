/**
 * @file cos_card_pager.h
 * @brief Card page view
 */

#ifndef COS_CARD_PAGER_H
#define COS_CARD_PAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_slide_widget.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Direction definition
 */
typedef enum
{
    COS_CARD_PAGER_DIR_VER,
    COS_CARD_PAGER_DIR_HOR
} cos_card_pager_dir_t;

typedef struct cos_card_pager_node_t cos_card_pager_node_t; // Forward declaration
typedef struct cos_card_pager_t cos_card_pager_t;

typedef void (*cos_card_pager_page_changed_cb_t)(cos_card_pager_t *cp, uint8_t current_page_index, void *user_data);

/**
 * @brief Doubly linked list for storing pages
 */
struct cos_card_pager_node_t
{
    lv_obj_t *page;
    lv_obj_t *indicator;
    cos_card_pager_node_t *prev;
    cos_card_pager_node_t *next;
};

/**
 * @brief Card page view structure definition
 */
struct cos_card_pager_t
{
    lv_obj_t *container;
    lv_obj_t *background;
    lv_obj_t *indicator_container;
    lv_obj_t *touch_area;
    cos_card_pager_node_t *page_list_head;
    uint8_t current_page_index;
    uint8_t page_count;
    cos_slide_widget_t *sw;
    cos_card_pager_dir_t dir;
    bool loop;
    cos_card_pager_page_changed_cb_t page_changed_cb;
    void *page_changed_user_data;
};
/* Public function prototypes --------------------------------*/

/**
 * @brief Create card page view manager
 * @param parent Parent LVGL object, pages will be created inside this object
 * @param dir Page slide direction (horizontal COS_CARD_PAGER_DIR_HOR or vertical COS_CARD_PAGER_DIR_VER)
 * @return cos_card_pager_t* Returns page manager pointer on success, NULL on failure
 * @note The created page manager includes background layer, page container and indicator
 */
cos_card_pager_t *cos_card_pager_create(lv_obj_t *parent, cos_card_pager_dir_t dir);

/**
 * @brief Create a new page in the page manager
 * @param cp Page manager pointer
 * @return lv_obj_t* Returns newly created page object on success, NULL on failure
 * @note Each new page will automatically create a corresponding indicator and update page navigation logic
 */
lv_obj_t *cos_card_pager_create_page(cos_card_pager_t *cp);

/**
 * @brief Set page loop mode
 * @param cp Page manager pointer
 * @param loop true to enable loop mode, false to disable loop mode
 * @note In loop mode, sliding from the last page will automatically jump to the first page and vice versa
 */
void cos_card_pager_set_loop(cos_card_pager_t *cp, bool loop);

/**
 * @brief Delete specified page
 * @param cp Page manager pointer
 * @param page_index Index of page to delete (0-based)
 * @return bool Returns true on success, false on failure
 * @note If the deleted page is the currently displayed page, it will automatically switch to the first page (index 0)
 */
bool cos_card_pager_remove_page(cos_card_pager_t *cp, uint8_t page_index);

/**
 * @brief Get specified page object
 * @param cp Page manager pointer
 * @param page_index Page index (0-based)
 * @return lv_obj_t* Returns page object on success, NULL on failure
 */
lv_obj_t *cos_card_pager_get_page(cos_card_pager_t *cp, uint8_t page_index);

/**
 * @brief Get indicator object of specified page
 * @param cp Page manager pointer
 * @param page_index Page index (0-based)
 * @return lv_obj_t* Returns indicator object on success, NULL on failure
 * @note The indicator is used to display small dots for the current page position
 */
lv_obj_t *cos_card_pager_get_indicator(cos_card_pager_t *cp, uint8_t page_index);

/**
 * @brief Switch to specified page
 * @param cp Page manager pointer
 * @param page_index Target page index (0-based)
 * @note Will automatically update indicator status and page navigation logic
 */
void cos_card_pager_move_page(cos_card_pager_t *cp, uint8_t page_index);

/**
 * @brief Move page node position
 * @param cp Page manager pointer
 * @param from_index Page index to move (0-based)
 * @param to_index Target position index (0-based)
 * @return bool Returns true on success, false on failure
 * @note This operation changes the logical order of pages but does not immediately change the display state
 *       If the moved page is the currently displayed page, the current page index will be updated
 */
bool cos_card_pager_move_node(cos_card_pager_t *cp, uint8_t from_index, uint8_t to_index);

void cos_card_pager_set_page_changed_cb(cos_card_pager_t *cp, cos_card_pager_page_changed_cb_t cb, void *user_data);
#ifdef __cplusplus
}
#endif

#endif /* COS_CARD_PAGER_H */
