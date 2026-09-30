/**
 * @file eos_lock_page.c
 * @brief Apple Watch style lock screen with numeric keypad
 */

#include "eos_lock_page.h"

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#define EOS_LOG_TAG "LockPage"
#include "eos_log.h"
#include "lvgl.h"
#include "eos_icon.h"
#include "eos_theme.h"
#include "eos_font.h"
#include "eos_lang.h"
#include "eos_config.h"
#include "eos_service_config.h"
#include "eos_mem.h"
#include "eos_sha256.h"
#include "eos_service_lock.h"
#include "eos_service_time.h"
#include "eos_service_haptic.h"
#include "eos_overlay_layer.h"
#include "eos_round_keyboard.h"

/* Macros and Definitions -------------------------------------*/
#define LOCK_PAGE_MAGIC 0x4C4F434BU

/* Variables --------------------------------------------------*/

typedef struct
{
    uint32_t magic;
    lv_obj_t *root;
    lv_obj_t *title_label;
    lv_obj_t *textarea;
    lv_obj_t *keyboard;
    uint8_t failed_attempts;
} _lock_page_ctx_t;

static _lock_page_ctx_t *_ctx = NULL;

/* Forward declarations ---------------------------------------*/
static void _verify_password(_lock_page_ctx_t *ctx);
static void _shake_anim_exec_cb(void *var, int32_t value);
static void _shake_anim_complete_cb(lv_anim_t *anim);
static void _restore_title_async_cb(void *user_data);

/* Function Implementations -----------------------------------*/

/* 键盘"确定"键向绑定的 textarea 发送 LV_EVENT_READY → 触发验证 */
static void _on_textarea_ready(lv_event_t *e)
{
    _lock_page_ctx_t *ctx = (_lock_page_ctx_t *)lv_event_get_user_data(e);
    if (!ctx)
        return;
    _verify_password(ctx);
}

static void _verify_password(_lock_page_ctx_t *ctx)
{
    const char *digits = (ctx->textarea && lv_obj_is_valid(ctx->textarea)) ? lv_textarea_get_text(ctx->textarea) : "";
    if (!digits)
        digits = "";

    /* Hash entered text */
    uint8_t hash[EOS_SHA256_DIGEST_SIZE];
    eos_sha256((const uint8_t *)digits, strlen(digits), hash);

    char entered_hex[EOS_SHA256_HEX_STR_SIZE];
    eos_sha256_to_hex(hash, entered_hex, sizeof(entered_hex));

    /* Get stored hash from config */
    char *stored_hash = eos_config_get_string(EOS_CONFIG_KEY_PASSWORD_HASH_STR, "");

    if (stored_hash && strcmp(entered_hex, stored_hash) == 0)
    {
        eos_free(stored_hash);
        EOS_LOG_I("Password correct, dismissing lock screen");
        eos_lock_screen_dismiss();
        return;
    }
    eos_free(stored_hash);

    /* Wrong password */
    ctx->failed_attempts++;
    EOS_LOG_W("Wrong password, attempt %d", ctx->failed_attempts);

    /* Haptic feedback */
    eos_haptic_buzz();

    /* Change title to error */
    if (ctx->title_label && lv_obj_is_valid(ctx->title_label))
    {
        eos_label_set_text_id(ctx->title_label, STR_ID_LOCK_SCREEN_WRONG_PASSCODE);
        lv_obj_set_style_text_color(ctx->title_label, EOS_COLOR_RED, 0);
    }

    /* Shake animation on textarea */
    if (ctx->textarea && lv_obj_is_valid(ctx->textarea))
    {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, ctx->textarea);
        lv_anim_set_values(&anim, -12, 12);
        lv_anim_set_duration(&anim, 60);
        lv_anim_set_repeat_count(&anim, 3);
        lv_anim_set_playback_time(&anim, 60);
        lv_anim_set_exec_cb(&anim, _shake_anim_exec_cb);
        lv_anim_set_completed_cb(&anim, _shake_anim_complete_cb);
        lv_anim_start(&anim);

        /* Clear input and keep it focused so the keyboard keeps working */
        lv_textarea_set_text(ctx->textarea, "");
        lv_textarea_set_cursor_pos(ctx->textarea, 0);
        lv_obj_add_state(ctx->textarea, LV_STATE_FOCUSED);
    }

    /* Restore title after a short delay */
    lv_async_call(_restore_title_async_cb, ctx->title_label);
}

static void _shake_anim_exec_cb(void *var, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    if (obj && lv_obj_is_valid(obj))
    {
        lv_obj_set_style_translate_x(obj, value, 0);
    }
}

static void _shake_anim_complete_cb(lv_anim_t *anim)
{
    lv_obj_t *obj = (lv_obj_t *)anim->var;
    if (obj && lv_obj_is_valid(obj))
    {
        lv_obj_set_style_translate_x(obj, 0, 0);
    }
}

static void _restore_title_async_cb(void *user_data)
{
    lv_obj_t *title = (lv_obj_t *)user_data;
    if (title && lv_obj_is_valid(title))
    {
        eos_label_set_text_id(title, STR_ID_LOCK_SCREEN_ENTER_PASSCODE);
        lv_obj_set_style_text_color(title, EOS_COLOR_TEXT_GREY, 0);
    }
}

/* 密码输入框样式，与 WiFi 输入页面的 textarea 保持一致 */
static void _style_password_textarea(lv_obj_t *textarea)
{
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(textarea, lv_color_black(), 0);
    lv_obj_set_style_border_width(textarea, 0, 0);
    lv_obj_set_style_pad_all(textarea, 0, 0);
    lv_obj_set_style_text_color(textarea, EOS_COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 1, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(textarea, EOS_COLOR_BLUE, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(textarea, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_duration(textarea, 400, LV_PART_CURSOR | LV_STATE_FOCUSED);
}

static void _create_lock_ui(_lock_page_ctx_t *ctx, lv_obj_t *parent)
{
    /* Root container filling parent — absolute top layer, security barrier */
    ctx->root = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->root);
    lv_obj_set_size(ctx->root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ctx->root, 0, 0);
    lv_obj_set_style_border_width(ctx->root, 0, 0);
    lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    /* Disable scroll & scrollbar */
    lv_obj_remove_flag(ctx->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ctx->root, LV_SCROLLBAR_MODE_OFF);

    /* Main flex container — bottom 120px reserved for the keyboard */
    lv_obj_t *container = lv_obj_create(ctx->root);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_bottom(container, 120, 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Time label */
    lv_obj_t *time_label = lv_label_create(container);
    eos_datetime_t now = eos_time_get();
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", now.hour, now.min);
    lv_label_set_text(time_label, time_str);
    lv_obj_set_style_text_color(time_label, EOS_COLOR_WHITE, 0);
    eos_label_set_font_size(time_label, EOS_FONT_SIZE_LARGE);

    /* Title label */
    ctx->title_label = lv_label_create(container);
    eos_label_set_text_id(ctx->title_label, STR_ID_LOCK_SCREEN_ENTER_PASSCODE);
    lv_obj_set_style_text_color(ctx->title_label, EOS_COLOR_TEXT_GREY, 0);
    eos_label_set_font_size(ctx->title_label, EOS_FONT_SIZE_MEDIUM);

    /* Password textarea */
    ctx->textarea = lv_textarea_create(container);
    lv_obj_set_width(ctx->textarea, 200);
    lv_obj_set_height(ctx->textarea, 66);
    lv_textarea_set_max_length(ctx->textarea, 255);
    lv_textarea_set_one_line(ctx->textarea, false);
    _style_password_textarea(ctx->textarea);

    /* 键盘"确定"键 → READY 事件 → 验证密码 */
    lv_obj_add_event_cb(ctx->textarea, _on_textarea_ready, LV_EVENT_READY, ctx);

    /* Half-circle keyboard (same as WiFi password input) on the root */
    ctx->keyboard = eos_round_keyboard_create(ctx->root);
    eos_round_keyboard_set_textarea(ctx->keyboard, ctx->textarea);

    /* Focus the textarea and put the cursor at the end so input is ready */
    if (ctx->textarea && lv_obj_is_valid(ctx->textarea))
    {
        lv_textarea_set_cursor_pos(ctx->textarea, 0);
        lv_obj_add_state(ctx->textarea, LV_STATE_FOCUSED);
    }
}

static void _destroy_lock_ui(_lock_page_ctx_t *ctx)
{
    if (!ctx)
        return;

    /* Reset translate if animating */
    if (ctx->textarea && lv_obj_is_valid(ctx->textarea))
    {
        lv_obj_set_style_translate_x(ctx->textarea, 0, 0);
    }

    /* Keyboard is a child of root and frees its own context on delete */
    if (ctx->root && lv_obj_is_valid(ctx->root))
    {
        lv_obj_delete(ctx->root);
        ctx->root = NULL;
    }

    eos_free(ctx);
}

/* ---- Public API ---- */

void eos_lock_page_show(void)
{
    if (_ctx)
    {
        EOS_LOG_W("Lock page already showing");
        return;
    }

    _ctx = (_lock_page_ctx_t *)eos_malloc_zeroed(sizeof(_lock_page_ctx_t));
    if (!_ctx)
    {
        EOS_LOG_E("Failed to allocate lock page context");
        return;
    }

    _ctx->magic = LOCK_PAGE_MAGIC;

    /* Build UI on overlay layer — naturally above header_layer on lv_layer_top */
    _create_lock_ui(_ctx, eos_overlay_get_overlay_layer());

    /* Ensure top z-order within overlay layer */
    if (_ctx->root)
    {
        lv_obj_move_foreground(_ctx->root);
    }

    EOS_LOG_I("Lock screen shown (security barrier)");
}

void eos_lock_page_hide(void)
{
    if (!_ctx)
    {
        return;
    }

    _destroy_lock_ui(_ctx);
    _ctx = NULL;

    /* Header visibility is managed by the activity system — no manual restore needed.
     * The overlay_layer naturally covered the header_layer while the lock was active. */

    EOS_LOG_I("Lock screen hidden");
}

bool eos_lock_page_is_visible(void)
{
    return _ctx != NULL && _ctx->root != NULL && lv_obj_is_valid(_ctx->root);
}
