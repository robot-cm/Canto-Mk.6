/**
 * @file cos_side_button.c
 * @brief Side button
 */

#include "cos_side_button.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "cos_dispatcher.h"
#include "input/cos_input.h"
#include "cos_control_center.h"
#include "cos_service_pm.h"
#include "cos_service_lock.h"
#include "cos_chrome_manager.h"
/* Macros and Definitions -------------------------------------*/

/* Variables --------------------------------------------------*/

/* Function Implementations -----------------------------------*/

static void _side_button_async_cb(void *user_data)
{
    if (cos_pm_get_state() == COS_PM_SLEEP)
    {
        cos_pm_wake_up();
        return;
    }
    cos_pm_reset_timer();

    /* Security: block all side button actions while lock screen is active */
    if (cos_lock_screen_is_active())
    {
        return;
    }

    /* Absorb side key events when any overlay is open (e.g. permission panel),
     * but allow the side button to toggle the control center which it controls */
    if (cos_chrome_manager_any_overlay_open())
    {
        const cos_chrome_overlay_t *top = cos_chrome_manager_get_top_overlay();
        if (top != cos_control_center_get_overlay_descriptor())
        {
            return;
        }
    }

    cos_button_state_t state = (cos_button_state_t)(intptr_t)user_data;
    switch (state)
    {
        case COS_BUTTON_STATE_CLICKED:
            cos_control_panel_slide_change();
        default:
            break;
    }
}

void cos_side_button_report(cos_button_state_t state)
{
    cos_dispatcher_call(_side_button_async_cb, (void *)(intptr_t)state);
}
