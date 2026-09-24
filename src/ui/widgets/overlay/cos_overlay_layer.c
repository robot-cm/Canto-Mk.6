/**
 * @file cos_overlay_layer.c
 * @brief Four-layer overlay system implementation on lv_layer_top()
 */
#include "cos_overlay_layer.h"

/* Includes ---------------------------------------------------*/
#define COS_LOG_TAG "OverlayLayer"
#include "cos_log.h"
#include "cos_mem.h"

/* Static variables -------------------------------------------*/
static lv_obj_t *_user_top_layer = NULL;
static lv_obj_t *_snapshot_layer = NULL;
static lv_obj_t *_app_layer = NULL;
static lv_obj_t *_header_layer = NULL;
static lv_obj_t *_notification_layer = NULL;
static lv_obj_t *_statusbar_layer = NULL;
static lv_obj_t *_indicator_layer = NULL;
static lv_obj_t *_overlay_layer = NULL;
static bool _initialized = false;

/* Helper: create a full-screen transparent layer container ----*/
static lv_obj_t *_create_layer(void)
{
    lv_obj_t *layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(layer, 0, 0);
    lv_obj_set_style_pad_all(layer, 0, 0);
    lv_obj_set_scrollbar_mode(layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    return layer;
}

/* Function Implementations -----------------------------------*/

void cos_overlay_layer_init(void)
{
    if (_initialized)
        return;

    /* Create from TOP to BOTTOM — each new layer is pushed to the background.
     * Final order (bottom→top):
     *   user_top / snapshot / app / header / notification / statusbar /
     *   indicator / overlay */

    _overlay_layer = _create_layer(); /* layer 7 — topmost */

    _indicator_layer = _create_layer();
    lv_obj_move_background(_indicator_layer); /* layer 6 */

    _statusbar_layer = _create_layer();
    lv_obj_move_background(_statusbar_layer); /* layer 5 */

    _notification_layer = _create_layer();
    lv_obj_move_background(_notification_layer); /* layer 4 */

    _header_layer = _create_layer();
    lv_obj_move_background(_header_layer); /* layer 3 */

    _app_layer = _create_layer();
    lv_obj_move_background(_app_layer); /* layer 2 — current app page */

    _snapshot_layer = _create_layer();
    lv_obj_move_background(_snapshot_layer); /* layer 1 */

    _user_top_layer = _create_layer();
    lv_obj_move_background(_user_top_layer); /* layer 0 — absolute bottom */

    _initialized = true;
    COS_LOG_I("8-layer overlay system initialized on lv_layer_top()");
}

lv_obj_t *cos_overlay_get_user_top_layer(void)
{
    return _user_top_layer;
}

lv_obj_t *cos_overlay_get_snapshot_layer(void)
{
    return _snapshot_layer;
}

lv_obj_t *cos_overlay_get_app_layer(void)
{
    return _app_layer;
}

lv_obj_t *cos_overlay_get_header_layer(void)
{
    return _header_layer;
}

lv_obj_t *cos_overlay_get_notification_layer(void)
{
    return _notification_layer;
}

lv_obj_t *cos_overlay_get_statusbar_layer(void)
{
    return _statusbar_layer;
}

lv_obj_t *cos_overlay_get_indicator_layer(void)
{
    return _indicator_layer;
}

lv_obj_t *cos_overlay_get_overlay_layer(void)
{
    return _overlay_layer;
}
