/**
 * @file cos_chrome_manager.c
 * @brief System chrome manager implementation
 */

#include "cos_chrome_manager.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#define COS_LOG_TAG "ChromeMgr"
#include "cos_log.h"
#include "cos_mem.h"
#include "cos_control_center.h"
#include "cos_msg_list.h"
#include "cos_flash_light.h"
#include "cos_cards_page.h"
#include "cos_activity.h"
#include "cos_app_list.h"
#include "cos_crown.h"
#include "cos_service_pm.h"
#include "cos_service_lock.h"
#include "cos_stack.h"

/* Macros and Definitions -------------------------------------*/

#define _MAX_OVERLAYS 16

/* Variables --------------------------------------------------*/
static const cos_chrome_overlay_t *_overlays[_MAX_OVERLAYS];
static uint32_t _overlay_count = 0;
static cos_stack_t *_overlay_stack = NULL;

/* Function Implementations -----------------------------------*/

static void _ensure_overlay_on_top(const cos_chrome_overlay_t *overlay)
{
    if (!overlay)
        return;

    lv_obj_t *obj = NULL;

    if (overlay->get_foreground_obj)
    {
        obj = overlay->get_foreground_obj();
    }
    else if (overlay->get_scrollable)
    {
        obj = overlay->get_scrollable();
    }

    if (obj && lv_obj_is_valid(obj))
    {
        lv_obj_move_foreground(obj);
        /* Also raise the overlay's container layer so the entire overlay
         * sits above any direct children of lv_layer_top() */
        lv_obj_t *parent = lv_obj_get_parent(obj);
        if (parent && lv_obj_is_valid(parent))
        {
            lv_obj_move_foreground(parent);
        }
        COS_LOG_D("Brought overlay to front[%s]", overlay->name ? overlay->name : "unknown");
    }
}

static void _activate_crown_for_overlay(const cos_chrome_overlay_t *overlay)
{
    if (!overlay)
    {
        cos_crown_encoder_set_target_view(cos_view_active());
        return;
    }

    if (overlay->get_scrollable)
    {
        lv_obj_t *scrollable = overlay->get_scrollable();
        if (scrollable && lv_obj_is_valid(scrollable))
        {
            cos_crown_encoder_set_target_obj(scrollable);
            COS_LOG_D("Crown activated for overlay[%s]", overlay->name ? overlay->name : "unknown");
            return;
        }
    }

    cos_crown_encoder_set_target_view(cos_view_active());
}

static void _focus_top_overlay(void)
{
    if (!_overlay_stack)
        return;

    const cos_chrome_overlay_t *top = (const cos_chrome_overlay_t *)cos_stack_peek(_overlay_stack);
    if (!top)
        return;

    COS_LOG_I("Focus changed to overlay[%s]", top->name ? top->name : "unknown");

    if (top->on_focus)
    {
        top->on_focus();
    }

    _ensure_overlay_on_top(top);
    _activate_crown_for_overlay(top);
}

void cos_chrome_manager_register_overlay(const cos_chrome_overlay_t *overlay)
{
    if (!overlay || !overlay->pull_back || !overlay->hide)
    {
        COS_LOG_E("Invalid overlay registration");
        return;
    }

    if (_overlay_count >= _MAX_OVERLAYS)
    {
        COS_LOG_E("Max overlays reached (%d)", _MAX_OVERLAYS);
        return;
    }

    _overlays[_overlay_count++] = overlay;
    COS_LOG_I("Overlay registered[%p] (total=%d)", overlay, _overlay_count);
}

bool cos_chrome_manager_any_overlay_open(void)
{
    return cos_stack_get_size(_overlay_stack) > 0;
}

const cos_chrome_overlay_t *cos_chrome_manager_get_top_overlay(void)
{
    return (const cos_chrome_overlay_t *)cos_stack_peek(_overlay_stack);
}

void cos_chrome_manager_pull_back_top(void)
{
    const cos_chrome_overlay_t *top = (const cos_chrome_overlay_t *)cos_stack_peek(_overlay_stack);
    if (top)
    {
        COS_LOG_I("Pulling back top overlay[%p]", top);
        top->pull_back();
    }
}

void cos_chrome_manager_pull_back_all(void)
{
    const cos_chrome_overlay_t *overlay;
    while ((overlay = (const cos_chrome_overlay_t *)cos_stack_pop(_overlay_stack)) != NULL)
    {
        COS_LOG_I("Pulling back overlay[%p]", overlay);
        overlay->pull_back();
    }
}

void cos_chrome_manager_push_overlay(const cos_chrome_overlay_t *overlay)
{
    if (!overlay)
        return;

    const cos_chrome_overlay_t *top = (const cos_chrome_overlay_t *)cos_stack_peek(_overlay_stack);
    if (top == overlay)
    {
        COS_LOG_W("Overlay[%p] is already on top of stack", overlay);
        return;
    }

    cos_chrome_manager_remove_overlay(overlay);

    COS_LOG_I("Pushing overlay[%p] to stack", overlay);
    cos_stack_push(_overlay_stack, (void *)overlay);
    _focus_top_overlay();
}

void cos_chrome_manager_remove_overlay(const cos_chrome_overlay_t *overlay)
{
    if (!overlay || !_overlay_stack)
        return;

    size_t size = cos_stack_get_size(_overlay_stack);
    if (size == 0)
        return;

    const cos_chrome_overlay_t *top_before = (const cos_chrome_overlay_t *)cos_stack_peek(_overlay_stack);
    bool removed = false;

    const cos_chrome_overlay_t **items = cos_malloc_zeroed(size * sizeof(*items));
    COS_CHECK_PTR_RETURN(items);

    for (size_t i = 0; i < size; i++)
    {
        items[i] = (const cos_chrome_overlay_t *)cos_stack_pop(_overlay_stack);
    }

    for (size_t i = size; i > 0; i--)
    {
        const cos_chrome_overlay_t *item = items[i - 1];
        if (item == overlay)
        {
            removed = true;
            COS_LOG_I("Removing overlay[%p] from stack", overlay);
            continue;
        }

        cos_stack_push(_overlay_stack, (void *)item);
    }

    cos_free(items);

    if (!removed)
        return;

    if (top_before == overlay)
    {
        _focus_top_overlay();
    }
}

void cos_chrome_manager_handle_crown_click(void)
{
    /* Wake up first if sleeping — always allow crown to wake, even on lock screen */
    if (cos_pm_get_state() == COS_PM_SLEEP)
    {
        cos_pm_wake_up();
        return;
    }

    /* Don't allow crown click to bypass lock screen */
    if (cos_lock_screen_is_active())
    {
        return;
    }

    cos_pm_reset_timer();

    if (cos_chrome_manager_any_overlay_open())
    {
        cos_chrome_manager_pull_back_top();
        return;
    }

    cos_activity_t *current = cos_activity_get_current();
    cos_activity_t *visible = cos_activity_get_visible();
    cos_activity_t *watchface = cos_activity_get_watchface();
    bool from_watchface = (current == watchface);

    COS_LOG_I(
        "Crown click: current=%p[type=%d] visible=%p[type=%d] watchface=%p[type=%d] from_watchface=%d transition=%d",
        (void *)current,
        current ? cos_activity_get_type(current) : -1,
        (void *)visible,
        visible ? cos_activity_get_type(visible) : -1,
        (void *)watchface,
        watchface ? cos_activity_get_type(watchface) : -1,
        from_watchface,
        cos_activity_is_transition_in_progress());

    if (from_watchface)
    {
        cos_app_list_enter();
    }
    else
    {
        cos_activity_back();
    }
}

void cos_chrome_manager_handle_activity_switch(void)
{
    cos_chrome_manager_pull_back_all();

    for (uint32_t i = 0; i < _overlay_count; i++)
    {
        _overlays[i]->hide();
    }
}

void cos_chrome_manager_init(void)
{
    _overlay_count = 0;

    if (_overlay_stack)
        cos_stack_destroy(_overlay_stack);
    _overlay_stack = cos_stack_create(4);

    cos_chrome_manager_register_overlay(cos_control_center_get_overlay_descriptor());
    cos_chrome_manager_register_overlay(cos_msg_list_get_overlay_descriptor());
    cos_chrome_manager_register_overlay(cos_flash_light_get_overlay_descriptor());
    cos_chrome_manager_register_overlay(cos_cards_page_get_overlay_descriptor());
    COS_LOG_I("Chrome manager initialized");
}

/* Helper functions for overlays to notify opening */
void cos_chrome_manager_notify_overlay_opened(const cos_chrome_overlay_t *overlay)
{
    if (!overlay)
    {
        COS_LOG_W("Overlay notify opened with null pointer");
        return;
    }

    cos_chrome_manager_push_overlay(overlay);
}

void cos_chrome_manager_notify_overlay_closed(const cos_chrome_overlay_t *overlay)
{
    if (!overlay)
    {
        COS_LOG_W("Overlay notify closed with null pointer");
        return;
    }

    cos_chrome_manager_remove_overlay(overlay);
}
