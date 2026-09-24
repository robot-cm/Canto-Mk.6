/**
 * @file eos_keyboard.h
 * @brief System on-screen keyboard with English (QWERTY) and Chinese (pinyin
 *        IME) input modes.
 *
 * The widget renders a compact soft keyboard suitable for a 240x240 round
 * display. Two modes:
 *   - EOS_KB_MODE_EN: QWERTY letters + space / back / enter / mode-toggle.
 *     Letters and digits are inserted directly into the bound textarea.
 *   - EOS_KB_MODE_ZH: the same letter keys accumulate a pinyin string; the
 *     candidate bar (number buttons) shows hanzi from eos_pinyin_lookup();
 *     pressing a number commits that candidate. Backspace edits the pinyin
 *     buffer (or the textarea when the buffer is empty).
 *
 * Output is delivered to a bound lv_textarea using LVGL's standard
 * LV_EVENT_INSERT / LV_EVENT_READY events, so it works with any textarea.
 */

#ifndef EOS_KEYBOARD_H
#define EOS_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Public typedefs --------------------------------------------*/

typedef enum
{
    EOS_KB_MODE_EN = 0, /**< English QWERTY */
    EOS_KB_MODE_ZH,     /**< Chinese pinyin IME */
} eos_keyboard_mode_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Create a keyboard widget.
 * @param parent Parent object (usually the current activity view).
 * @return lv_obj_t* The keyboard container.
 */
lv_obj_t *eos_keyboard_create(lv_obj_t *parent);

/**
 * @brief Bind a textarea that receives the keyboard's input.
 */
void eos_keyboard_set_textarea(lv_obj_t *kb, lv_obj_t *ta);

/**
 * @brief Switch input mode (English / Chinese).
 */
void eos_keyboard_set_mode(lv_obj_t *kb, eos_keyboard_mode_t mode);

/**
 * @brief Get the current input mode.
 */
eos_keyboard_mode_t eos_keyboard_get_mode(lv_obj_t *kb);

/**
 * @brief Programmatically "press" a key. Key strings:
 *        "a".."z" letters, " " space, "BACK" backspace, "ENTER" enter,
 *        "MODE" toggle EN/ZH, "1".."9" select candidate (ZH) or digit (EN).
 *        Primarily for headless testing; the on-screen buttons call it too.
 */
void eos_keyboard_send_key(lv_obj_t *kb, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* EOS_KEYBOARD_H */
