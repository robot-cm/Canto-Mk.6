/**
 * @file cos_framework_home.h
 * @brief Framework Home screen (P8 rendering layer).
 *
 * Combines a Liquid Glass status bar (positioned via the LayoutManager anchor
 * system — no hardcoded coords) with the app list: ArcList on round screens,
 * LayoutManager grid on square/rectangle. Entry uses the page-open animation.
 * Apps never know which screen shape they run on.
 */
#ifndef COS_FRAMEWORK_HOME_H
#define COS_FRAMEWORK_HOME_H

#include "lvgl.h"
#include "cos_display_profile.h"
#include "cos_arclist_view.h"
#include "cos_layout_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cos_framework_home {
    /** @brief Display profile OWNED BY VALUE (heap-allocated with the home) so
     *         child views' profile pointers stay valid for the whole home
     *         lifetime — even across the 16ms step timer that re-lays-out the
     *         ArcList after the caller's stack-local profile has gone out of
     *         scope (a dangling-pointer bug caused cards to clump at 0,0). */
    cos_display_profile_t profile;
    lv_obj_t *root;
    lv_obj_t *statusbar;        /**< Liquid Glass panel (top, anchored)    */
    lv_obj_t *focus_label;      /**< selected app NAME shown in the bar    */
    float sb_x, sb_y, sb_w, sb_h;  /**< resolved status-bar rect (px)      */
    bool  is_circle;
    cos_arclist_view_t *arc;    /**< valid when is_circle (Huawei ArcList) */
    cos_layout_view_t  *grid;   /**< valid when !is_circle && grid mode    */
    lv_obj_t *bubble;           /**< valid when !is_circle && bubble mode  */
    int bubble_count;           /**< item count when in bubble mode        */
    const char **names;         /**< app name list (for the focus label)   */
    const char **icon_paths;     /**< optional per-app icon paths           */
    int n_names;
    void (*on_select)(int index, void *user);  /**< launch callback (set by caller) */
    void *select_user;
} cos_framework_home_t;

cos_framework_home_t *cos_framework_home_create(lv_obj_t *parent,
        const cos_display_profile_t *p, const char **names, int n,
        const char **icon_paths);

void cos_framework_home_step(cos_framework_home_t *h, float dt_ms);
void cos_framework_home_drag(cos_framework_home_t *h, float dy_px);
void cos_framework_home_release(cos_framework_home_t *h);

void cos_framework_home_destroy(cos_framework_home_t *h);
lv_obj_t *cos_framework_home_root(const cos_framework_home_t *h);

/** @brief Forward a card-tap selection callback to the active app-list view. */
void cos_framework_home_set_on_select(cos_framework_home_t *h,
        void (*cb)(int index, void *user), void *user);

/**
 * @brief For a SQUARE/RECTANGLE home that was built as a grid, swap it to the
 *        Apple-style honeycomb bubble grid (per the user's square-mode
 *        setting). No-op on round screens or if already in bubble mode. The
 *        caller (Launcher) decides the mode from settings — the render layer
 *        itself stays config-free so it can run in headless tests.
 */
void cos_framework_home_use_bubble(cos_framework_home_t *h);

/** @brief Resolved status-bar rectangle (px) — for safe-area verification. */
void cos_framework_home_statusbar_rect(const cos_framework_home_t *h,
                                       float *x, float *y, float *w, float *h_out);

/** @brief Number of app items in the active list view (arc or grid). */
int cos_framework_home_count(const cos_framework_home_t *h);

/** @brief Resolved center (px) of app item i — for safe-area verification.
 *  Circle: arc item center (already the stored geometry). Square/Rect:
 *  grid widget center (x + w/2, y + h/2). Out-of-range i leaves x,y = 0. */
void cos_framework_home_item_center(const cos_framework_home_t *h, int i,
                                    float *x, float *y);

#ifdef __cplusplus
}
#endif
#endif /* COS_FRAMEWORK_HOME_H */
