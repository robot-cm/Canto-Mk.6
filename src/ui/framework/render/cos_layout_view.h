/**
 * @file cos_layout_view.h
 * @brief LayoutManager grid renderer (P8 rendering layer, SQUARE/RECTANGLE).
 *
 * Renders a set of COS_WIDGET_ICON declarations through the active
 * LayoutManager (auto grid for square/rectangle) into real LVGL glass cards.
 * Position/size come entirely from the LayoutManager — no hardcoded coords.
 */
#ifndef COS_LAYOUT_VIEW_H
#define COS_LAYOUT_VIEW_H

#include "lvgl.h"
#include "cos_display_profile.h"
#include "cos_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cos_layout_view_item {
    lv_obj_t *card;
    lv_obj_t *label;
} cos_layout_view_item_t;

typedef struct cos_layout_view {
    const cos_display_profile_t *profile;
    cos_layout_manager_t *mgr;
    lv_obj_t *root;
    cos_widget_t *widgets;          /**< layout input + resolved px        */
    cos_layout_view_item_t *items;
    int count;
    float card_px;                  /**< nominal card size (px)            */
    void (*on_select)(int index, void *user);
    void *user;
} cos_layout_view_t;

cos_layout_view_t *cos_layout_view_create(lv_obj_t *parent,
                                          const cos_display_profile_t *p,
                                          const char **names, int n,
                                          const char **icon_paths);
/** @brief Set the selection callback (fired when a card is tapped). */
void cos_layout_view_set_on_select(cos_layout_view_t *v,
        void (*cb)(int index, void *user), void *user);
void cos_layout_view_destroy(cos_layout_view_t *v);
int  cos_layout_view_count(const cos_layout_view_t *v);
lv_obj_t *cos_layout_view_card_at(const cos_layout_view_t *v, int i);

#ifdef __cplusplus
}
#endif
#endif /* COS_LAYOUT_VIEW_H */
