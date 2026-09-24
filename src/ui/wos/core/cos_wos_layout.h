/**
 * @file cos_wos_layout.h
 * @brief WOS layout helpers — flex / grid / glass-card containers.
 *
 * Pages are built with these helpers instead of hard-coded coordinates
 * (see design doc §5). Only small alignment offsets are allowed.
 */
#ifndef COS_WOS_LAYOUT_H
#define COS_WOS_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create a flex-row container (children laid out left→right, gap 8).
 */
lv_obj_t *wos_row(lv_obj_t *parent);

/**
 * @brief Create a flex-column container (children top→bottom, gap 8).
 */
lv_obj_t *wos_column(lv_obj_t *parent);

/**
 * @brief Create a glass card (translucent rounded container). See §1.2.
 */
lv_obj_t *wos_card(lv_obj_t *parent);

/**
 * @brief Create a grid container.
 * @param parent Parent object
 * @param cols   Column count
 * @param gap    Gap in px
 */
lv_obj_t *wos_grid(lv_obj_t *parent, int cols, int gap);

/**
 * @brief Make a child grow to fill free space (flex-grow = 1).
 */
void wos_flex_grow(lv_obj_t *obj);

/**
 * @brief Reserve the round-display safe area on a page root: positions the
 *        root over the full screen and lets flex content stay inside the
 *        inscribed circle (uses COS_PROFILE_240C safe values).
 * @param root Page root container (created by wos_column)
 * @note The root itself must NOT be round-clipped (transform anims break
 *       under clip_corner); clipping is applied one level above instead.
 */
void wos_make_safe_area(lv_obj_t *root);

#ifdef __cplusplus
}
#endif

#endif /* COS_WOS_LAYOUT_H */
