/**
 * @file eos_arclist_view.h
 * @brief ArcList LVGL renderer (P8 rendering layer).
 *
 * Bridges the pure-math ArcList geometry (eos_arclist) to real LVGL objects:
 * one Liquid Glass card + name label per app, positioned / scaled / faded
 * every frame from the resolved geometry. Physical scrolling (inertial +
 * bounce) is driven through eos_arclist's physics scroller.
 */
#ifndef EOS_ARCLIST_VIEW_H
#define EOS_ARCLIST_VIEW_H

#include "lvgl.h"
#include "eos_display_profile.h"
#include "eos_arclist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct eos_arclist_view_item {
    lv_obj_t *card;     /**< Liquid Glass circular card (icon host)     */
    lv_obj_t *label;    /**< app name label (child of root, below card)*/
} eos_arclist_view_item_t;

typedef struct eos_arclist_view {
    const eos_display_profile_t *profile;
    eos_arclist_t al;
    lv_obj_t *root;
    eos_arclist_view_item_t items[EOS_ARCLIST_MAX];
    int  count;
    float card_px;          /**< base card diameter (px)                */
    int  last_y;            /**< pointer tracking for drag              */
    int  press_y;           /**< press start Y for drag-threshold       */
    bool dragging;
    void (*on_select)(int index, void *user);
    void *user;
    /* Focus-change callback: fired when the centered (focused) item index
     * changes during scroll, so the parent (e.g. the Home status bar) can
     * reflect the currently-selected app name without a tap. NULL = disabled. */
    void (*on_focus_change)(int index, void *user);
    void *focus_user;
    int last_focus;   /**< last reported focus index (change detection) */
} eos_arclist_view_t;

/** @brief Build the view. names[i] (and optional icon_paths[i]) per app. */
eos_arclist_view_t *eos_arclist_view_create(lv_obj_t *parent,
                                            const eos_display_profile_t *p,
                                            const char **names, int n,
                                            const char **icon_paths);

/** @brief Advance physics by dt_ms and re-apply geometry to LVGL objects. */
void eos_arclist_view_step(eos_arclist_view_t *v, float dt_ms);

/** @brief Finger drag: dy_px (screen px, +down). */
void eos_arclist_view_drag(eos_arclist_view_t *v, float dy_px);

/** @brief Finger released: begin inertial + bounce. */
void eos_arclist_view_release(eos_arclist_view_t *v);

/** @brief Snap-scroll so app `index` is at the focus. */
void eos_arclist_view_focus(eos_arclist_view_t *v, int index);

int  eos_arclist_view_focus_index(const eos_arclist_view_t *v);
int  eos_arclist_view_count(const eos_arclist_view_t *v);
lv_obj_t *eos_arclist_view_card_at(const eos_arclist_view_t *v, int i);

/** @brief Set the selection callback (fired when a card is tapped). */
void eos_arclist_view_set_on_select(eos_arclist_view_t *v,
        void (*cb)(int index, void *user), void *user);

/** @brief Set the focus-change callback (fired when the centered item changes). */
void eos_arclist_view_set_on_focus_change(eos_arclist_view_t *v,
        void (*cb)(int index, void *user), void *user);

void eos_arclist_view_destroy(eos_arclist_view_t *v);

#ifdef __cplusplus
}
#endif
#endif /* EOS_ARCLIST_VIEW_H */
