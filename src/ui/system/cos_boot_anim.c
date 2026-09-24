/**
 * @file cos_boot_anim.c
 * @brief Boot self-test animation implementation.
 *
 * Stage 0 (0.0s - 2.7s) : full-screen solid color flashes (blue/red/yellow/green)
 * Stage 1 (2.7s - 5.0s) : brand logo (2.3s, resources/images/icon/bootanim.webp)
 * Stage 2 (5.0s - 6.8s) : typewriter "INFILTRATING" (1.8s)
 * Stage 3 (6.8s - 7.0s) : quick fade-out (~0.2s) -> done callback, revealing the main UI.
 *
 * The whole sequence is timer/anim driven so it never blocks the main loop,
 * every stage releases its objects as soon as it finishes, the sequence runs
 * exactly once per boot, and the main UI is only started after completion.
 */

#include "ui/system/cos_boot_anim.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "lvgl.h"
#include "cos_font.h"
#define COS_LOG_TAG "BootAnim"
#include "cos_log.h"

/* Stage 1 brand logo (converted from resources/images/icon/bootanim.webp) */
extern const lv_image_dsc_t cos_icon_bootanim;

/* Macros and Definitions -------------------------------------*/

#define COS_BA_BG       lv_color_hex(0x0A0E17) /* deep space grey         */
#define COS_BA_CYAN     lv_color_hex(0x00F0FF) /* primary neon cyan       */
#define COS_BA_BLUE     lv_color_hex(0x4285F4) /* Google blue   #4285F4   */
#define COS_BA_RED      lv_color_hex(0xEA4335) /* Google red    #EA4335   */
#define COS_BA_YELLOW   lv_color_hex(0xFBBC05) /* Google yellow #FBBC05   */
#define COS_BA_GREEN    lv_color_hex(0x34A853) /* Google green  #34A853   */

#define COS_BA_TEXT     "INFILTRATING"
#define COS_BA_TEXT_LEN 12U

#define COS_BA_SCREEN_PX    240U
#define COS_BA_SCANLINE_NUM 5U

#define COS_BA_FLASH_PERIOD_MS 500U /* not used for equal timing; see s_flash_dur[] */

#define COS_BA_TYPE_PERIOD_MS 150U  /* 12 chars x 150ms = 1.8s */
#define COS_BA_FADE_PERIOD_MS 33U
#define COS_BA_FADE_STEPS     6U    /* ~0.2s -> stage 3 totals 2.0s */

#define COS_BA_LOGO_PERIOD_MS 2300U /* stage 1 brand logo hold (2.3s) */

/* Variables --------------------------------------------------*/

typedef struct
{
    lv_obj_t *overlay;    /* full-screen container on lv_layer_top() */
    lv_obj_t *bg;         /* deep space grey background (+ scanlines) */
    lv_obj_t *flash;      /* stage 0 solid color full-screen object   */
    lv_obj_t *logo;       /* stage 1 brand logo image                 */
    lv_obj_t *glow;       /* stage 2 text glow halo label             */
    lv_obj_t *label;      /* stage 2 "INFILTRATING" label             */

    lv_timer_t *flash_timer;
    lv_timer_t *logo_timer;
    lv_timer_t *type_timer;
    lv_timer_t *fade_timer;

    cos_boot_anim_done_cb_t done_cb;
    uint8_t flash_step;
    uint8_t type_idx;
    uint8_t fade_step;
    bool    running;
} cos_boot_anim_ctx_t;

static cos_boot_anim_ctx_t s_ctx;

/* stage 0 solid color flash sequence (initialized at runtime:
 * lv_color_hex() is a plain function, not a constant expression) */
static lv_color_t s_flash_colors[4];
/* per-color hold durations (ms): blue 0.8s, red 0.9s, yellow 0.6s, green 0.4s */
static const uint32_t s_flash_dur[4] = { 800U, 900U, 600U, 400U };

/* forward declarations (timer callbacks cross-reference each other) */
static void _flash_timer_cb(lv_timer_t *t);
static void _logo_timer_cb(lv_timer_t *t);
static void _type_timer_cb(lv_timer_t *t);
static void _fade_timer_cb(lv_timer_t *t);

/* Function Implementations -----------------------------------*/

static void _stop_timer(lv_timer_t **t)
{
    if (*t)
    {
        lv_timer_delete(*t);
        *t = NULL;
    }
}

static void _apply_opa_tree(lv_obj_t *parent, int32_t opa)
{
    lv_obj_set_style_opa(parent, opa, 0);
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++)
        lv_obj_set_style_opa(lv_obj_get_child(parent, i), opa, 0);
}

/* Stage 0: solid color flash, each color held for its own duration
 * (blue 0.8s, red 0.9s, yellow 0.6s, green 0.4s) */
static void _flash_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_ctx.flash_step++;
    if (s_ctx.flash_step < 4)
    {
        lv_obj_set_style_bg_color(s_ctx.flash, s_flash_colors[s_ctx.flash_step], 0);
        /* re-arm the timer with this color's own hold duration */
        lv_timer_set_period(s_ctx.flash_timer, s_flash_dur[s_ctx.flash_step]);
        return;
    }

    /* all 4 colors shown: stage 0 done, release its object, start stage 1 */
    _stop_timer(&s_ctx.flash_timer);
    if (s_ctx.flash)
    {
        lv_obj_delete(s_ctx.flash);
        s_ctx.flash = NULL;
    }

    /* stage 1: brand logo, centered on the deep space background (2.3s) */
    s_ctx.logo = lv_img_create(s_ctx.overlay);
    lv_img_set_src(s_ctx.logo, &cos_icon_bootanim);
    lv_obj_center(s_ctx.logo);
    lv_obj_move_foreground(s_ctx.logo);
    s_ctx.logo_timer = lv_timer_create(_logo_timer_cb, COS_BA_LOGO_PERIOD_MS, NULL);
}

/* Stage 1 done: release the logo, start the typewriter stage.
 * Stage 2: typewriter text with neon glow (dual-label halo trick:
 * a slightly upscaled, semi-transparent cyan label behind a white one) */
static void _logo_timer_cb(lv_timer_t *t)
{
    (void)t;
    _stop_timer(&s_ctx.logo_timer);
    if (s_ctx.logo)
    {
        lv_obj_delete(s_ctx.logo);
        s_ctx.logo = NULL;
    }

    s_ctx.glow = lv_label_create(s_ctx.overlay);
    cos_label_set_font_size(s_ctx.glow, COS_FONT_SIZE_LARGE);
    lv_obj_center(s_ctx.glow);
    lv_obj_set_style_text_color(s_ctx.glow, COS_BA_CYAN, 0);
    lv_obj_set_style_text_opa(s_ctx.glow, LV_OPA_60, 0);
    lv_obj_set_style_transform_scale(s_ctx.glow, 282, 0); /* ~110% halo */
    lv_label_set_text(s_ctx.glow, "");

    s_ctx.label = lv_label_create(s_ctx.overlay);
    cos_label_set_font_size(s_ctx.label, COS_FONT_SIZE_LARGE);
    lv_obj_center(s_ctx.label);
    lv_obj_set_style_text_color(s_ctx.label, lv_color_white(), 0);
    lv_label_set_text(s_ctx.label, "");
    s_ctx.type_idx = 0;
    s_ctx.type_timer = lv_timer_create(_type_timer_cb, COS_BA_TYPE_PERIOD_MS, NULL);
}

/* Stage 1: typewriter reveal of "INFILTRATING" (12 x 150ms = 1.8s) */
static void _type_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_ctx.type_idx++;
    if (s_ctx.type_idx <= COS_BA_TEXT_LEN)
    {
        char buf[COS_BA_TEXT_LEN + 1];
        memcpy(buf, COS_BA_TEXT, s_ctx.type_idx);
        buf[s_ctx.type_idx] = '\0';
        lv_label_set_text(s_ctx.label, buf);
        if (s_ctx.glow)
            lv_label_set_text(s_ctx.glow, buf);
        return;
    }

    /* typing done (~6.8s): start the fade-out */
    _stop_timer(&s_ctx.type_timer);
    s_ctx.fade_step = 0;
    s_ctx.fade_timer = lv_timer_create(_fade_timer_cb, COS_BA_FADE_PERIOD_MS, NULL);
}

/* Final: fade out the whole overlay (~0.2s), then release everything and
 * notify the caller so the main UI can finally start */
static void _fade_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_ctx.fade_step++;
    int32_t opa = LV_OPA_COVER - s_ctx.fade_step * LV_OPA_COVER / COS_BA_FADE_STEPS;
    if (opa < 0)
        opa = 0;
    if (s_ctx.label)
        lv_obj_set_style_opa(s_ctx.label, opa, 0);
    if (s_ctx.glow)
        lv_obj_set_style_opa(s_ctx.glow, opa, 0);
    if (s_ctx.bg)
        _apply_opa_tree(s_ctx.bg, opa);

    if (s_ctx.fade_step >= COS_BA_FADE_STEPS)
    {
        /* t = 7.0s: done - free every remaining object */
        _stop_timer(&s_ctx.fade_timer);
        if (s_ctx.overlay)
        {
            lv_obj_delete(s_ctx.overlay);
            s_ctx.overlay = NULL;
        }
        s_ctx.bg = NULL;
        s_ctx.glow = NULL;
        s_ctx.label = NULL;
        s_ctx.running = false;
        COS_LOG_I("boot animation finished");

        cos_boot_anim_done_cb_t cb = s_ctx.done_cb;
        s_ctx.done_cb = NULL;
        if (cb)
            cb(); /* caller decides how to hand off (keep it lightweight) */
    }
}

/* Subtle static scanline texture on the background (no motion: per spec the
 * scanning FX are frozen during the typewriter stage) */
static void _create_scanlines(lv_obj_t *bg)
{
    for (uint8_t i = 0; i < COS_BA_SCANLINE_NUM; i++)
    {
        lv_obj_t *line = lv_obj_create(bg);
        lv_obj_remove_style_all(line);
        lv_obj_set_style_bg_color(line, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(line, 12, 0); /* extremely subtle */
        lv_obj_set_size(line, lv_pct(100), 1);
        lv_obj_set_pos(line, 0, i * (COS_BA_SCREEN_PX / COS_BA_SCANLINE_NUM));
    }
}

void cos_boot_anim_start(cos_boot_anim_done_cb_t done_cb)
{
    if (s_ctx.running)
        return; /* plays exactly once */

    lv_obj_t *top = lv_layer_top();
    if (!top)
        return;

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.running = true;
    s_ctx.done_cb = done_cb;

    s_flash_colors[0] = COS_BA_BLUE;
    s_flash_colors[1] = COS_BA_RED;
    s_flash_colors[2] = COS_BA_YELLOW;
    s_flash_colors[3] = COS_BA_GREEN;

    /* debug:打印各阶段期望色与字节序换算值,对照显示与 flush[] 日志排查。
     * 屏幕应显示 want RGB888 对应的颜色;若显示的是 swapped 值则字节序未修正。 */
    static const uint32_t s_flash_rgb888[4] = { 0x4285F4, 0xEA4335, 0xFBBC05, 0x34A853 };
    for (uint8_t i = 0; i < 4; i++)
    {
        uint16_t u16 = lv_color_to_u16(s_flash_colors[i]);
        COS_LOG_I("flash[%u] want RGB888=0x%06X u16=0x%04X swapped=0x%04X",
                  (unsigned)i, (unsigned)s_flash_rgb888[i], (unsigned)u16,
                  (unsigned)((uint16_t)((u16 >> 8) | (u16 << 8))));
    }

    /* full-screen container on the topmost layer; blocks touches so the
     * boot splash underneath cannot be interacted with during the sequence */
    s_ctx.overlay = lv_obj_create(top);
    lv_obj_remove_style_all(s_ctx.overlay);
    lv_obj_set_size(s_ctx.overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_ctx.overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ctx.overlay, 0, 0);
    lv_obj_add_flag(s_ctx.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_ctx.overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* deep space grey background with static scanlines (fully opaque, so the
     * screen underneath is never visible until the final fade-out) */
    s_ctx.bg = lv_obj_create(s_ctx.overlay);
    lv_obj_remove_style_all(s_ctx.bg);
    lv_obj_set_size(s_ctx.bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_ctx.bg, COS_BA_BG, 0);
    lv_obj_set_style_bg_opa(s_ctx.bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ctx.bg, 0, 0);
    _create_scanlines(s_ctx.bg);

    /* stage 0: solid color flash object, starts blue immediately */
    s_ctx.flash = lv_obj_create(s_ctx.overlay);
    lv_obj_remove_style_all(s_ctx.flash);
    lv_obj_set_size(s_ctx.flash, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_ctx.flash, s_flash_colors[0], 0);
    lv_obj_set_style_bg_opa(s_ctx.flash, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_ctx.flash, 0, 0);
    lv_obj_move_foreground(s_ctx.flash);

    s_ctx.flash_step = 0;
    s_ctx.flash_timer = lv_timer_create(_flash_timer_cb, s_flash_dur[0], NULL);

    COS_LOG_I("boot animation started (7.0s)");
}

bool cos_boot_anim_running(void)
{
    return s_ctx.running;
}
