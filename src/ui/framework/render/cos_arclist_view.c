/**
 * @file cos_arclist_view.c
 * @brief ArcList LVGL renderer — Huawei-watch style arc app list on screen.
 */
#include "cos_arclist_view.h"
#include "cos_liquid_glass.h"
#include "cos_mem.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Base card size in dp (scaled by profile.dp_scale at create time). */
#define COS_ARCLIST_CARD_DP 64.0f

static void _item_click_cb(lv_event_t *e);
static void _root_pressed_cb(lv_event_t *e);
static void _root_pressing_cb(lv_event_t *e);
static void _root_released_cb(lv_event_t *e);

/* Safe pointer read: returns (0,0) when no indev is active (headless). */
static void _get_point(lv_point_t *p)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, p);
    else { p->x = 0; p->y = 0; }
}

static void _apply(cos_arclist_view_t *v)
{
    float half = v->card_px * 0.5f;
    int focus = cos_arclist_focus_index(&v->al);
    for (int i = 0; i < v->count; i++) {
        const cos_arclist_item_t *it = &v->al.items[i];
        lv_obj_t *card = v->items[i].card;
        lv_obj_t *name = v->items[i].label;
        if (!it->visible) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(name, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);

        /* Position uses the BASE size; transform_scale does the visual zoom
         * around the card center, so the visual center stays at (it->x,it->y). */
        lv_obj_set_pos(card, (int)(it->x - half), (int)(it->y - half));
        lv_obj_set_style_opa(card, (lv_opa_t)(it->opacity * 255.0f + 0.5f), 0);
        lv_obj_set_style_transform_scale(card, (int)(it->scale * 256.0f + 0.5f), 0);

        /* Show the app NAME only for the focused (center) item. Neighbours are
         * icon-only, so the wide 92px name labels can never overlap each other
         * (they would on the arc — that was the "字体重叠" bug). */
        if (i == focus) {
            lv_obj_clear_flag(name, LV_OBJ_FLAG_HIDDEN);
            float name_y = it->y + half * it->scale + 4.0f;
            lv_obj_set_pos(name, (int)(it->x - (half + 14.0f)), (int)name_y);
            lv_obj_set_style_opa(name, (lv_opa_t)(it->opacity * 255.0f + 0.5f), 0);
        } else {
            lv_obj_add_flag(name, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Emit a focus-change event when the centered (focused) item changes, so
     * the parent can reflect the selected app name. Skip while the callback is
     * still NULL (e.g. during create, before the parent wires it up). */
    if (v->on_focus_change && focus != v->last_focus) {
        v->last_focus = focus;
        v->on_focus_change(focus, v->focus_user);
    }
}

cos_arclist_view_t *cos_arclist_view_create(lv_obj_t *parent,
                                            const cos_display_profile_t *p,
                                            const char **names, int n,
                                            const char **icon_paths)
{
    cos_arclist_view_t *v = (cos_arclist_view_t *)cos_malloc(sizeof(*v));
    if (!v) return NULL;
    memset(v, 0, sizeof(*v));
    v->profile = p;
    v->count   = (n > COS_ARCLIST_MAX) ? COS_ARCLIST_MAX : n;
    v->card_px = COS_ARCLIST_CARD_DP * p->dp_scale;
    v->last_focus = -1;   /* force an initial focus emit once wired */

    v->root = lv_obj_create(parent);
    lv_obj_remove_style_all(v->root);
    lv_obj_set_size(v->root, (int)p->width, (int)p->height);
    lv_obj_set_pos(v->root, 0, 0);
    lv_obj_clear_flag(v->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(v->root, 0, 0);

    cos_arclist_init(&v->al, p, v->count);
    /* Tighten the arc so the top items clear the status bar on small round
     * screens (less vertical reach) — avoids the arc curling under the bar. */
    v->al.visible_deg = 60.0f;
    /* Use the real card radius so the clamp keeps the *rendered* card inside. */
    cos_arclist_set_item_radius(&v->al, v->card_px * 0.5f);

    for (int i = 0; i < v->count; i++) {
        lv_obj_t *card = lv_obj_create(v->root);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, (int)v->card_px, (int)v->card_px);
        lv_obj_set_style_radius(card, LV_RADIUS_CIRCLE, 0);
        cos_app_icon_solid(card);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, _item_click_cb, LV_EVENT_CLICKED, v);

        const char *nm = (names && names[i]) ? names[i] : NULL;
        lv_obj_t *mono = lv_label_create(card);
        char buf[2] = {0};
        buf[0] = (nm && nm[0]) ? nm[0] : '?';
        lv_label_set_text(mono, buf);
        lv_obj_set_style_text_font(mono, &lv_font_montserrat_30, 0);
        lv_obj_set_style_text_color(mono, lv_color_white(), 0);
        lv_obj_center(mono);

        if (icon_paths && icon_paths[i] && icon_paths[i][0]) {
            lv_obj_t *img = lv_image_create(card);
            lv_image_set_src(img, icon_paths[i]);
            lv_obj_set_size(img, (int)(v->card_px * 0.7f), (int)(v->card_px * 0.7f));
            lv_obj_center(img);
            lv_obj_add_flag(mono, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_t *name = lv_label_create(v->root);
        lv_label_set_text(name, nm ? nm : "");
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(name, (int)(v->card_px + 28));

        v->items[i].card = card;
        v->items[i].label = name;
    }

    lv_obj_add_event_cb(v->root, _root_pressed_cb,  LV_EVENT_PRESSED,  v);
    lv_obj_add_event_cb(v->root, _root_pressing_cb, LV_EVENT_PRESSING, v);
    lv_obj_add_event_cb(v->root, _root_released_cb, LV_EVENT_RELEASED, v);

    _apply(v);
    return v;
}

void cos_arclist_view_step(cos_arclist_view_t *v, float dt_ms)
{
    if (!v) return;
    cos_arclist_step(&v->al, dt_ms / 1000.0f);
    _apply(v);
}

void cos_arclist_view_drag(cos_arclist_view_t *v, float dy_px)
{
    if (!v) return;
    cos_arclist_drag(&v->al, dy_px, 0.016f);
}

void cos_arclist_view_release(cos_arclist_view_t *v)
{
    if (!v) return;
    cos_arclist_release(&v->al);
}

void cos_arclist_view_focus(cos_arclist_view_t *v, int index)
{
    if (!v || index < 0 || index >= v->count) return;
    float target = (float)index * v->al.step_deg;
    v->al.scroller.pos = target;
    v->al.scroll = target;
    cos_arclist_layout(&v->al);
    _apply(v);
}

int cos_arclist_view_focus_index(const cos_arclist_view_t *v)
{
    return v ? cos_arclist_focus_index(&v->al) : -1;
}

int cos_arclist_view_count(const cos_arclist_view_t *v)
{
    return v ? v->count : 0;
}

lv_obj_t *cos_arclist_view_card_at(const cos_arclist_view_t *v, int i)
{
    if (!v || i < 0 || i >= v->count) return NULL;
    return v->items[i].card;
}

void cos_arclist_view_destroy(cos_arclist_view_t *v)
{
    if (!v) return;
    if (v->root) lv_obj_del(v->root);
    cos_free(v);
}

void cos_arclist_view_set_on_select(cos_arclist_view_t *v,
        void (*cb)(int index, void *user), void *user)
{
    if (!v) return;
    v->on_select = cb;
    v->user = user;
}

void cos_arclist_view_set_on_focus_change(cos_arclist_view_t *v,
        void (*cb)(int index, void *user), void *user)
{
    if (!v) return;
    v->on_focus_change = cb;
    v->focus_user = user;
}

/* ----------------------------- events ----------------------------- */

static void _item_click_cb(lv_event_t *e)
{
    cos_arclist_view_t *v = (cos_arclist_view_t *)lv_event_get_user_data(e);
    if (!v) return;
    lv_obj_t *card = lv_event_get_target(e);
    for (int i = 0; i < v->count; i++) {
        if (v->items[i].card == card) {
            cos_arclist_view_focus(v, i);
            if (v->on_select) v->on_select(i, v->user);
            return;
        }
    }
}

static void _root_pressed_cb(lv_event_t *e)
{
    cos_arclist_view_t *v = (cos_arclist_view_t *)lv_event_get_user_data(e);
    if (!v) return;
    lv_point_t p;
    _get_point(&p);
    v->press_y = p.y;
    v->last_y = p.y;
    v->dragging = false;
}

static void _root_pressing_cb(lv_event_t *e)
{
    cos_arclist_view_t *v = (cos_arclist_view_t *)lv_event_get_user_data(e);
    if (!v) return;
    lv_point_t p;
    _get_point(&p);
    int dy_total = p.y - v->press_y;
    /* Drag threshold: ignore sub-pixel jitter so a clean tap still becomes a
     * LV_EVENT_CLICKED. Without this, the scroller got velocity on every
     * micro-move and the release never counted as a click -> apps wouldn't
     * open ("无法打开app页"). */
    if (!v->dragging) {
        if (dy_total < 8 && dy_total > -8) {
            v->last_y = p.y;
            return;
        }
        v->dragging = true;
    }
    int dy = p.y - v->last_y;
    v->last_y = p.y;
    if (dy != 0)
        cos_arclist_view_drag(v, (float)dy);
}

static void _root_released_cb(lv_event_t *e)
{
    cos_arclist_view_t *v = (cos_arclist_view_t *)lv_event_get_user_data(e);
    if (!v) return;
    v->dragging = false;
    cos_arclist_view_release(v);
}
