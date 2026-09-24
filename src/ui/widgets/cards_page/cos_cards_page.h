/**
 * @file cos_cards_page.h
 * @brief Small-cards (smart stack) page revealed by up-swipe from the bottom
 *        edge — Redmi-Watch style. Hosts a vertical stack of lightweight
 *        cards (activity, heart rate, battery, ...).
 *
 *        The page exposes a small, app-facing registration API so that SD-card
 *        plugins (and system services) can contribute their own cards without
 *        touching the internals. Every card — built-in or app-provided — goes
 *        through the SAME registry, so the stack is ordered by `priority` and
 *        rebuilt on demand.
 */

#ifndef COS_CARDS_PAGE_H
#define COS_CARDS_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_chrome_manager.h"
#include "cos_swipe_panel.h"

/* Public macros ----------------------------------------------*/

/**
 * @brief Maximum number of cards the page can hold (built-in + app cards).
 */
#define COS_CARDS_PAGE_MAX_CARDS 8

/* Card geometry — exposed so an app's build callback can size and lay out
 * its custom content to match the standard card footprint. */
#define COS_CARDS_PAGE_CARD_W      200
#define COS_CARDS_PAGE_CARD_H      70
#define COS_CARDS_PAGE_CARD_RADIUS 16

/* Public typedefs --------------------------------------------*/
typedef struct cos_cards_page_t
{
    cos_swipe_panel_t *swipe_panel; /**< Drag object pointer */
    lv_obj_t *header;              /**< Time + date header strip */
    lv_obj_t *header_time;         /**< HH:MM label */
    lv_obj_t *header_date;         /**< M/D 周X label */
    lv_obj_t *cards;               /**< Vertical container of cards */
    uint8_t card_count;            /**< Number of registered cards currently shown */
} cos_cards_page_t;

/* ---- App-facing card registration API ---- */

/** Opaque handle to a registered card (returned by register, used to unregister). */
typedef struct cos_card_t *cos_card_handle_t;

/**
 * @brief Populate a card's body.
 *
 * `card` is a pre-built flex-ROW container of size
 * COS_CARDS_PAGE_CARD_W x COS_CARDS_PAGE_CARD_H with the accent background
 * already applied (radius, padding, center alignment set). The builder only
 * needs to add its own children (labels, arcs, charts, images, ...).
 *
 * @param card       The card container to fill.
 * @param user_data  The `user_data` from the descriptor.
 */
typedef void (*cos_card_build_cb)(lv_obj_t *card, void *user_data);

/** Called when the card is tapped. */
typedef void (*cos_card_click_cb)(cos_card_handle_t card, void *user_data);

/**
 * @brief Card descriptor. Passed by value to cos_cards_page_register_card();
 *        the page copies the fields, so the caller's storage need not persist.
 */
typedef struct
{
    const char       *id;        /**< Unique stable id — used for de-dup and unregister. Must be non-NULL. */
    const char       *title;     /**< Optional short title; used only when `build == NULL`. */
    lv_color_t        accent;    /**< Card background color. */
    uint8_t           priority;  /**< 0 = top of the stack; larger = lower. */
    cos_card_build_cb build;     /**< Populate content; NULL => a default title-only card is built. */
    cos_card_click_cb click;     /**< Optional tap handler. */
    void             *user_data; /**< Passed back to `build` and `click`. */
} cos_card_desc_t;

/**
 * @brief Register a card.
 * @return Opaque handle, or NULL if the page is full or the descriptor is
 *         invalid. The card is added to the stack immediately and will be
 *         visible the next time the cards page opens.
 * @note  Safe to call before cos_cards_page_init() — the card is queued and
 *         built when the page is created. Registering the same `id` again
 *         replaces the previous card (count unchanged).
 */
cos_card_handle_t cos_cards_page_register_card(const cos_card_desc_t *desc);

/**
 * @brief Remove a previously registered card.
 */
void cos_cards_page_unregister_card(cos_card_handle_t card);

/**
 * @brief Rebuild the card stack from the current registry (e.g. after a
 *        batch of register/unregister calls, or when card data changed and
 *        the builder should re-run).
 */
void cos_cards_page_refresh(void);

/* ---- Legacy / convenience (delegates to the registry) ---- */

/**
 * @brief Append a simple title-only card. Convenience wrapper around
 *        cos_cards_page_register_card() with no builder. Returns the created
 *        card's lv_obj_t* (or NULL on failure); primarily for shell/debug use.
 */
lv_obj_t *cos_cards_page_add_card(lv_color_t accent, const char *title);

/* ---- Lifecycle / chrome (unchanged) ---- */

/**
 * @brief Initialize cards page. Creates the swipe panel + seeds the built-in
 *        cards on the overlay layer. Safe to call once at boot.
 */
void cos_cards_page_init(void);

/**
 * @brief Show the cards-page touch strip (bottom 50px). The watchface calls
 *        this in on_enter / on_resume.
 */
void cos_cards_page_show(void);

/**
 * @brief Hide the cards-page touch strip. Called in on_pause.
 */
void cos_cards_page_hide(void);

/**
 * @brief Get the cards-page instance (NULL until cos_cards_page_init).
 */
cos_cards_page_t *cos_cards_page_get_instance(void);

/**
 * @brief Slide up to reveal the cards page (animate from base to target).
 *        Programmatic open from the watchface (e.g. up-swipe starting away
 *        from the bottom strip) or a button.
 */
void cos_cards_page_slide_up(void);

/**
 * @brief Chrome-manager overlay descriptor. Registered in cos_cards_page_init.
 */
const cos_chrome_overlay_t *cos_cards_page_get_overlay_descriptor(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_CARDS_PAGE_H */
