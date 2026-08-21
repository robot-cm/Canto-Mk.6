/**
 * @file eos_framework_home.c
 * @brief Framework Home screen — adaptive app launcher on the UI framework.
 *
 * Architecture (per the Launcher redesign): the WatchFace is the home/root
 * activity (it owns the clock). This Home is the SEPARATE app-list page,
 * entered from the watchface. It uses a FIXED layout driven by the
 * LayoutManager / ArcList — no free-floating physics. Physics is limited to
 * scroll inertia, bounce, and page transitions.
 *
 *   - Round  -> Huawei-style ArcList (eos_arclist_view): apps on an arc, the
 *               centered item is the largest/selected.
 *   - Square -> LayoutManager grid by default, or Apple-style honeycomb bubble
 *               grid when the user enables "bubble" mode in settings.
 *
 * The status bar is a lightweight Liquid Glass panel that shows the currently
 * SELECTED app name (not a live clock — time lives on the watchface).
 */
#include "eos_framework_home.h"
#include "eos_liquid_glass.h"
#include "eos_ui_anim.h"
#include "eos_layout.h"
#include "eos_arclist.h"
#include "eos_arclist_view.h"
#include "eos_layout_view.h"
#include "eos_bubble_grid.h"
#include "eos_mem.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Internal selection / focus wiring                                  */
/* ------------------------------------------------------------------ */

/* Forward the card-tap to the caller's launch callback, after updating the
 * status-bar selected-app name. Used by arc / grid / bubble alike. */
static void _home_on_select_internal(int index, void *user)
{
    eos_framework_home_t *h = (eos_framework_home_t *)user;
    if (!h) return;
    if (index >= 0 && index < h->n_names && h->names && h->names[index])
        lv_label_set_text(h->focus_label, h->names[index]);
    else
        lv_label_set_text(h->focus_label, "Apps");
    if (h->on_select)
        h->on_select(index, h->select_user);
}

/* ArcList focus-change: keep the status-bar name in sync with the centered
 * (selected) item as the user scrolls. */
static void _home_focus_cb(int index, void *user)
{
    eos_framework_home_t *h = (eos_framework_home_t *)user;
    if (!h) return;
    if (index >= 0 && index < h->n_names && h->names && h->names[index])
        lv_label_set_text(h->focus_label, h->names[index]);
    else
        lv_label_set_text(h->focus_label, "Apps");
}

/* Bubble-grid click: the click event carries the icon's user_data (the app
 * index we stored at create time). */
static void _bubble_click_cb(lv_event_t *e)
{
    eos_framework_home_t *h = (eos_framework_home_t *)lv_event_get_user_data(e);
    if (!h) return;
    eos_bubble_click_event_t *ce = (eos_bubble_click_event_t *)lv_event_get_param(e);
    if (!ce) return;
    int index = (int)(intptr_t)ce->icon_user_data;
    _home_on_select_internal(index, h);
}

/* Simple palette so bubble mode is visible without per-app PNG icons.
   Initialized lazily (lv_color_hex is not a compile-time constant). */
static lv_color_t s_bubble_palette[8];
static bool s_bubble_palette_init = false;
static void _init_bubble_palette(void)
{
    if (s_bubble_palette_init) return;
    s_bubble_palette[0] = lv_color_hex(0x27AE60);
    s_bubble_palette[1] = lv_color_hex(0xEB5757);
    s_bubble_palette[2] = lv_color_hex(0x9B59B6);
    s_bubble_palette[3] = lv_color_hex(0x00B8D4);
    s_bubble_palette[4] = lv_color_hex(0xF2C94C);
    s_bubble_palette[5] = lv_color_hex(0x2F80ED);
    s_bubble_palette[6] = lv_color_hex(0xE67E22);
    s_bubble_palette[7] = lv_color_hex(0x16A085);
    s_bubble_palette_init = true;
}

eos_framework_home_t *eos_framework_home_create(lv_obj_t *parent,
        const eos_display_profile_t *p, const char **names, int n,
        const char **icon_paths)
{
    eos_framework_home_t *h = (eos_framework_home_t *)eos_malloc(sizeof(*h));
    if (!h) return NULL;
    memset(h, 0, sizeof(*h));
    /* Copy the profile BY VALUE so child views (ArcList / grid) hold a stable
     * pointer into this heap-allocated home. The caller may pass a stack-local
     * profile (e.g. the Launcher's on_enter) that is destroyed on return; the
     * step timer must still be able to re-read center_x/center_y safely. */
    h->profile = *p;
    h->is_circle = (p->shape == EOS_DISPLAY_SHAPE_CIRCLE);
    h->n_names = n;

    /* The caller's names/icon_paths arrays are frequently STACK-LOCAL and
       freed once this function returns, but the Home reads them at runtime
       (the focus-change callback refreshes the status-bar app name while
       scrolling; bubble mode reads icon paths on build). Own the ARRAY
       containers so we never dereference freed stack memory. The string
       pointers themselves are assumed persistent (literals / static buffers). */
    if (n > 0)
    {
        h->names = (const char **)eos_malloc((size_t)n * sizeof(const char *));
        h->icon_paths = (const char **)eos_malloc((size_t)n * sizeof(const char *));
        for (int i = 0; i < n; i++)
        {
            h->names[i]     = (names && names[i]) ? eos_strdup(names[i]) : NULL;
            h->icon_paths[i] = (icon_paths && icon_paths[i]) ? eos_strdup(icon_paths[i]) : NULL;
        }
    }
    else
    {
        h->names = NULL;
        h->icon_paths = NULL;
    }

    h->root = lv_obj_create(parent);
    lv_obj_remove_style_all(h->root);
    lv_obj_set_size(h->root, (int)p->width, (int)p->height);
    lv_obj_set_pos(h->root, 0, 0);
    lv_obj_clear_flag(h->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(h->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(h->root, 0, 0);

    /* ---- App list FIRST so the status bar renders ON TOP of it (covers any
     *      item that curves up near the top on round screens). ---- */
    if (h->is_circle)
    {
        h->arc = eos_arclist_view_create(h->root, &h->profile, names, n, icon_paths);
    }
    else
    {
        /* Square/Rectangle: LayoutManager grid by default. The Apple-style
           honeycomb bubble grid is opt-in and applied by the Launcher via
           eos_framework_home_use_bubble() (the render layer itself stays
           config-free so it runs in headless tests without the config
           service). */
        h->grid = eos_layout_view_create(h->root, &h->profile, names, n, icon_paths);
    }

    /* ---- Status bar via LayoutManager anchor (responsive, no hardcoded) ---- */
    eos_widget_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.kind = EOS_WIDGET_CARD;
    sb.anchor = EOS_ANCHOR_TOP_CENTER;
    sb.margin_dp = h->is_circle ? 30.0f : 10.0f;
    sb.w_dp = h->is_circle ? 132.0f : ((p->safe_w / p->dp_scale) - 8.0f);
    sb.h_dp = 26.0f;
    eos_layout_place_anchored(p, &sb);

    /* Guarantee the whole panel (corners) stays inside the safe area: clamp
     * its center by the half-diagonal radius + 2px safety margin (covers
     * integer rounding of LVGL positions and a small visual gap). Prevents
     * rectangular overflow on round screens. */
    float cx = sb.x + sb.w * 0.5f;
    float cy = sb.y + sb.h * 0.5f;
    float r  = sqrtf((sb.w * 0.5f) * (sb.w * 0.5f) + (sb.h * 0.5f) * (sb.h * 0.5f)) + 2.0f;
    eos_display_profile_clamp_inside(p, &cx, &cy, r);
    sb.x = cx - sb.w * 0.5f;
    sb.y = cy - sb.h * 0.5f;

    h->statusbar = lv_obj_create(h->root);
    lv_obj_remove_style_all(h->statusbar);
    lv_obj_set_size(h->statusbar, (int)sb.w, (int)sb.h);
    lv_obj_set_pos(h->statusbar, (int)(sb.x + 0.5f), (int)(sb.y + 0.5f));
    h->sb_x = sb.x; h->sb_y = sb.y; h->sb_w = sb.w; h->sb_h = sb.h;
    eos_liquid_glass_panel(h->statusbar);
    lv_obj_set_style_opa(h->statusbar, LV_OPA_COVER, 0);

    /* The bar shows the SELECTED app name (time stays on the watchface). */
    h->focus_label = lv_label_create(h->statusbar);
    lv_label_set_text(h->focus_label,
                      (n > 0 && names && names[0]) ? names[0] : "Apps");
    lv_obj_set_style_text_font(h->focus_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h->focus_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(h->focus_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(h->focus_label);

    /* Wire the selection / focus callbacks. */
    if (h->arc)
    {
        eos_arclist_view_set_on_select(h->arc, _home_on_select_internal, h);
        eos_arclist_view_set_on_focus_change(h->arc, _home_focus_cb, h);
        _home_focus_cb(eos_arclist_view_focus_index(h->arc), h);
    }
    if (h->grid)
    {
        eos_layout_view_set_on_select(h->grid, _home_on_select_internal, h);
        _home_on_select_internal(0, h);   /* default selected = first app */
    }
    /* bubble: on_select is handled in _bubble_click_cb via _home_on_select_internal */

    /* Entry animation (page open: fade + scale 0.8 -> 1.0). */
    eos_anim_page_open(h->root, 300);

    return h;
}

void eos_framework_home_step(eos_framework_home_t *h, float dt_ms)
{
    if (!h) return;
    if (h->is_circle && h->arc) eos_arclist_view_step(h->arc, dt_ms);
    /* Grid + bubble layouts are static (bubble drives its own input/physics). */
}

void eos_framework_home_drag(eos_framework_home_t *h, float dy_px)
{
    if (!h) return;
    if (h->is_circle && h->arc) eos_arclist_view_drag(h->arc, dy_px);
    /* Grid (square/rect) is static; bubble handles its own drag. */
}

void eos_framework_home_release(eos_framework_home_t *h)
{
    if (!h) return;
    if (h->is_circle && h->arc) eos_arclist_view_release(h->arc);
}

void eos_framework_home_destroy(eos_framework_home_t *h)
{
    if (!h) return;
    /* Bubble/grid/arc are children of root, deleted with it. No clock timer
     * to clean up (time lives on the watchface, not in this page). */
    if (h->root) lv_obj_del(h->root);
    if (h->names)
    {
        for (int i = 0; i < h->n_names; i++)
            if (h->names[i]) eos_free((void *)h->names[i]);
        eos_free((void *)h->names);
    }
    if (h->icon_paths)
    {
        for (int i = 0; i < h->n_names; i++)
            if (h->icon_paths[i]) eos_free((void *)h->icon_paths[i]);
        eos_free((void *)h->icon_paths);
    }
    eos_free(h);
}

void eos_framework_home_set_on_select(eos_framework_home_t *h,
        void (*cb)(int index, void *user), void *user)
{
    if (!h) return;
    h->on_select = cb;
    h->select_user = user;
    /* Re-apply the internal wrapper so every view routes through
     * _home_on_select_internal (which updates the status-bar name). */
    if (h->arc) eos_arclist_view_set_on_select(h->arc, _home_on_select_internal, h);
    if (h->grid) eos_layout_view_set_on_select(h->grid, _home_on_select_internal, h);
    /* bubble already calls _home_on_select_internal from _bubble_click_cb */
}

/* Build the Apple-style honeycomb bubble grid from the stored app list. */
static void _home_build_bubble(eos_framework_home_t *h)
{
    _init_bubble_palette();
    h->bubble = eos_bubble_create(h->root);
    if (!h->bubble) return;
    lv_obj_set_size(h->bubble, (int)h->profile.width, (int)h->profile.height);
    lv_obj_center(h->bubble);
    lv_obj_add_event_cb(h->bubble, _bubble_click_cb, LV_EVENT_CLICKED, h);
    size_t np = sizeof(s_bubble_palette) / sizeof(s_bubble_palette[0]);
    int n = h->n_names;
    for (int i = 0; i < n; i++)
    {
        eos_bubble_set_icon_color(h->bubble, (uint32_t)i, s_bubble_palette[i % np]);
        if (h->icon_paths && h->icon_paths[i] && h->icon_paths[i][0])
            eos_bubble_set_icon_src(h->bubble, (uint32_t)i, h->icon_paths[i]);
        eos_bubble_set_icon_user_data(h->bubble, (uint32_t)i, (void *)(intptr_t)i);
    }
    h->bubble_count = n;
}

void eos_framework_home_use_bubble(eos_framework_home_t *h)
{
    if (!h || h->is_circle || h->bubble)
        return;
    if (h->grid)
    {
        eos_layout_view_destroy(h->grid);
        h->grid = NULL;
    }
    _home_build_bubble(h);
    /* Match the page-open spec (fade 0->255 + scale 0.8->1.0) for the bubble. */
    if (h->bubble)
        eos_anim_page_open(h->bubble, 300);
}

lv_obj_t *eos_framework_home_root(const eos_framework_home_t *h)
{
    return h ? h->root : NULL;
}

void eos_framework_home_statusbar_rect(const eos_framework_home_t *h,
                                       float *x, float *y, float *w, float *h_out)
{
    if (!h) { if (x) *x = 0; if (y) *y = 0; if (w) *w = 0; if (h_out) *h_out = 0; return; }
    if (x) *x = h->sb_x;
    if (y) *y = h->sb_y;
    if (w) *w = h->sb_w;
    if (h_out) *h_out = h->sb_h;
}

int eos_framework_home_count(const eos_framework_home_t *h)
{
    if (!h) return 0;
    if (h->is_circle && h->arc) return eos_arclist_view_count(h->arc);
    if (h->grid) return eos_layout_view_count(h->grid);
    if (h->bubble) return h->bubble_count;
    return 0;
}

void eos_framework_home_item_center(const eos_framework_home_t *h, int i,
                                    float *x, float *y)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (!h || i < 0) return;

    if (h->is_circle && h->arc)
    {
        const eos_arclist_view_t *v = h->arc;
        if (i >= v->count) return;
        *x = v->al.items[i].x;
        *y = v->al.items[i].y;
    }
    else if (h->grid)
    {
        const eos_layout_view_t *v = h->grid;
        if (i >= v->count) return;
        const eos_widget_t *w = &v->widgets[i];
        *x = w->x + w->w * 0.5f;
        *y = w->y + w->h * 0.5f;
    }
    else if (h->bubble)
    {
        /* The bubble widget does not expose resolved item geometry through the
         * public API; report the screen center so any caller needing a
         * fallback still gets an in-safe-area point. */
        *x = h->profile.center_x;
        *y = h->profile.center_y;
    }
}
