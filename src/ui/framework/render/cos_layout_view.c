/**
 * @file cos_layout_view.c
 * @brief LayoutManager grid renderer — glass cards from declarative widgets.
 */
#include "cos_layout_view.h"
#include "cos_liquid_glass.h"
#include "cos_mem.h"
#include <string.h>

#define COS_LAYOUT_CARD_DP 48.0f

static void _item_click_cb(lv_event_t *e);

cos_layout_view_t *cos_layout_view_create(lv_obj_t *parent,
                                          const cos_display_profile_t *p,
                                          const char **names, int n,
                                          const char **icon_paths)
{
    int count = n;
    cos_layout_view_t *v = (cos_layout_view_t *)cos_malloc(sizeof(*v));
    if (!v) return NULL;
    memset(v, 0, sizeof(*v));
    v->profile = p;
    v->count = count;
    v->card_px = COS_LAYOUT_CARD_DP * p->dp_scale;

    v->widgets = (cos_widget_t *)cos_malloc(sizeof(cos_widget_t) * (count > 0 ? count : 1));
    v->items   = (cos_layout_view_item_t *)cos_malloc(
                     sizeof(cos_layout_view_item_t) * (count > 0 ? count : 1));
    if (!v->widgets || !v->items) {
        cos_free(v->widgets); cos_free(v->items); cos_free(v);
        return NULL;
    }

    v->mgr = cos_layout_manager_create(p);
    v->root = lv_obj_create(parent);
    lv_obj_remove_style_all(v->root);
    lv_obj_set_size(v->root, (int)p->width, (int)p->height);
    lv_obj_set_pos(v->root, 0, 0);
    lv_obj_clear_flag(v->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(v->root, 0, 0);

    for (int i = 0; i < count; i++) {
        memset(&v->widgets[i], 0, sizeof(v->widgets[i]));
        v->widgets[i].kind = COS_WIDGET_ICON;
        v->widgets[i].w_dp = COS_LAYOUT_CARD_DP;
        v->widgets[i].h_dp = COS_LAYOUT_CARD_DP;
    }
    if (v->mgr) v->mgr->layout(v->mgr, p, v->widgets, count);

    for (int i = 0; i < count; i++) {
        cos_widget_t *w = &v->widgets[i];
        const char *nm = (names && names[i]) ? names[i] : NULL;

        lv_obj_t *card = lv_obj_create(v->root);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, (int)w->w, (int)w->h);
        lv_obj_set_pos(card, (int)w->x, (int)w->y);
        lv_obj_set_style_radius(card, (int)(w->w * 0.28f), 0);
        cos_app_icon_solid(card);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, _item_click_cb, LV_EVENT_CLICKED, v);

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
            lv_obj_set_size(img, (int)(w->w * 0.7f), (int)(w->h * 0.7f));
            lv_obj_center(img);
            lv_obj_add_flag(mono, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_t *name = lv_label_create(v->root);
        lv_label_set_text(name, nm ? nm : "");
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(name, (int)(w->w + 28));
        lv_obj_set_pos(name, (int)(w->x + w->w * 0.5f - (w->w * 0.5f + 14.0f)),
                             (int)(w->y + w->h + 2.0f));

        v->items[i].card = card;
        v->items[i].label = name;
    }
    return v;
}

void cos_layout_view_destroy(cos_layout_view_t *v)
{
    if (!v) return;
    if (v->root) lv_obj_del(v->root);
    if (v->mgr) v->mgr->destroy(v->mgr);
    cos_free(v->widgets);
    cos_free(v->items);
    cos_free(v);
}

void cos_layout_view_set_on_select(cos_layout_view_t *v,
        void (*cb)(int index, void *user), void *user)
{
    if (!v) return;
    v->on_select = cb;
    v->user = user;
}

static void _item_click_cb(lv_event_t *e)
{
    cos_layout_view_t *v = (cos_layout_view_t *)lv_event_get_user_data(e);
    if (!v) return;
    lv_obj_t *card = lv_event_get_target(e);
    for (int i = 0; i < v->count; i++) {
        if (v->items[i].card == card) {
            if (v->on_select) v->on_select(i, v->user);
            return;
        }
    }
}

int cos_layout_view_count(const cos_layout_view_t *v)
{
    return v ? v->count : 0;
}

lv_obj_t *cos_layout_view_card_at(const cos_layout_view_t *v, int i)
{
    if (!v || i < 0 || i >= v->count) return NULL;
    return v->items[i].card;
}
