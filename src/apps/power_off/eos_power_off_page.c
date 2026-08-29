/**
 * @file eos_power_off_page.c
 * @brief 关机页:主界面上滑打开,选关机模式后进入硬件深睡
 *
 * 交互(全部英文文案):
 *   - 上滑(主界面)打开:半透明遮罩 + 红色圆形关机按钮 + "Power Off?"
 *   - 点红色关机按钮 → 图标淡去,显示两个圆形按钮:
 *       ● 绿色 SLEEP: 触摸模式 → eos_pm_power_off()
 *           真机: 深睡 + RTC 定时器轮询触摸,长按开机(按住到某次唤醒 ≈ 3s);
 *           模拟器: 全屏黑屏 mask。
 *       ● 黄色 TIMER: 定时模式 → 两按钮淡去,显示 HH:MM:SS + 加减号
 *           调整醒来时间,START 后 → eos_pm_power_off_timed(sec)
 *           真机: 深睡,触摸无效,到点自动开机(唤醒周期自动拉长,几乎不闪);
 *           模拟器: 同黑屏 mask。
 *   - 点遮罩空白 → 关闭页面(防误触)
 *
 * 依据:AGENTS.md 第28节硬件优先级 + 方案A(不改硬件,深睡轮询)。
 */

#include "eos_power_off_page.h"

#include "lvgl.h"
#define EOS_LOG_TAG "PowerOffPage"
#include "eos_log.h"
#include "eos_service_pm.h"
#include "eos_font.h"

/* ---------- 阶段状态 ---------- */
typedef enum
{
    _STAGE_NONE = 0,
    _STAGE_CHOOSE,   /* 绿色/黄色两个圆形按钮 */
    _STAGE_TIMER,    /* HH:MM:SS 定时设置 */
} _stage_t;

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_stage = NULL;   /* 当前阶段容器(可整体淡出) */
static _stage_t  s_next_stage = _STAGE_NONE;

/* 定时设置状态 */
static lv_obj_t *s_fld[3] = {NULL, NULL, NULL};  /* HH / MM / SS */
static int      s_val[3]  = {0, 1, 0};           /* 默认 1 分钟 */
static int      s_sel     = 1;                   /* 默认选中 MM */
static const int s_max[3] = {99, 59, 59};        /* 各字段上限(最大 99:59:59) */

static void _stage_create(_stage_t stage);
static void _stage_update_fields(void);

/* ---------- 阶段淡出动画 ---------- */
static void _stage_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void _stage_ready_cb(lv_anim_t *a)
{
    lv_obj_delete(a->var);   /* 删除旧阶段容器 */
    s_stage = NULL;
    _stage_create(s_next_stage);
}

static void _stage_fade_to(_stage_t next)
{
    s_next_stage = next;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_stage);
    lv_anim_set_exec_cb(&a, _stage_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 250);
    lv_anim_set_ready_cb(&a, _stage_ready_cb);
    lv_anim_start(&a);
}

/* ---------- 事件回调 ---------- */
/* 点红色关机按钮:图标淡去,进入模式选择 */
static void _power_btn_cb(lv_event_t *e)
{
    (void)e;
    EOS_LOG_I("Power-off button pressed -> choose mode");
    _stage_fade_to(_STAGE_CHOOSE);
}

/* 绿色 SLEEP:触摸模式(真机长按开机,深睡不返回) */
static void _green_cb(lv_event_t *e)
{
    (void)e;
    EOS_LOG_I("SLEEP (touch mode, hold to boot) -> eos_pm_power_off()");
    eos_pm_power_off();
}

/* 黄色 TIMER:两按钮淡去,进入定时设置 */
static void _yellow_cb(lv_event_t *e)
{
    (void)e;
    EOS_LOG_I("TIMER (wake timer setup) selected");
    _stage_fade_to(_STAGE_TIMER);
}

/* START:按当前 HH:MM:SS 定时关机(真机深睡不返回) */
static void _start_cb(lv_event_t *e)
{
    (void)e;
    uint32_t total = (uint32_t)s_val[0] * 3600u + (uint32_t)s_val[1] * 60u + (uint32_t)s_val[2];
    if (total == 0u)
        total = 1u;   /* 最小 1 秒,防 0 */
    EOS_LOG_I("Wake timer START: %02d:%02d:%02d -> %u s",
              s_val[0], s_val[1], s_val[2], (unsigned)total);
    eos_pm_power_off_timed(total);
}

/* BACK:返回模式选择 */
static void _back_cb(lv_event_t *e)
{
    (void)e;
    EOS_LOG_D("Wake timer BACK -> choose mode");
    _stage_fade_to(_STAGE_CHOOSE);
}

/* 字段点击:切换选中高亮 */
static void _field_cb(lv_event_t *e)
{
    s_sel = (int)(intptr_t)lv_event_get_user_data(e);
    _stage_update_fields();
}

/* 加减号调整选中字段 */
static void _adjust_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);   /* +1 / -1 */
    s_val[s_sel] += delta;
    if (s_val[s_sel] < 0)
        s_val[s_sel] = s_max[s_sel];
    if (s_val[s_sel] > s_max[s_sel])
        s_val[s_sel] = 0;
    _stage_update_fields();
}

/* 更新字段显示与高亮 */
static void _stage_update_fields(void)
{
    for (int i = 0; i < 3; i++)
    {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%02d", s_val[i]);
        lv_label_set_text(s_fld[i], buf);
        lv_obj_set_style_text_color(s_fld[i],
                                    (i == s_sel) ? lv_color_hex(0xF9A825) : lv_color_white(), 0);
    }
}

/* ---------- 阶段创建 ---------- */
static lv_obj_t *_stage_new(void)
{
    s_stage = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_stage);
    lv_obj_set_size(s_stage, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_CLICKABLE);
    return s_stage;
}

static void _stage_create(_stage_t stage)
{
    if (stage == _STAGE_CHOOSE)
    {
        lv_obj_t *st = _stage_new();

        /* 绿色 SLEEP 圆钮 */
        lv_obj_t *g = lv_btn_create(st);
        lv_obj_set_size(g, 76, 76);
        lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g, lv_color_hex(0x43A047), 0);
        lv_obj_set_style_bg_color(g, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
        lv_obj_align(g, LV_ALIGN_CENTER, -44, -16);
        lv_obj_add_event_cb(g, _green_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *gl = lv_label_create(g);
        lv_label_set_text(gl, "SLEEP");
        lv_obj_set_style_text_color(gl, lv_color_white(), 0);
        eos_label_set_font_size(gl, EOS_FONT_SIZE_EXTRA_SMALL);
        lv_obj_center(gl);

        /* 黄色 TIMER 圆钮 */
        lv_obj_t *y = lv_btn_create(st);
        lv_obj_set_size(y, 76, 76);
        lv_obj_set_style_radius(y, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(y, lv_color_hex(0xF9A825), 0);
        lv_obj_set_style_bg_color(y, lv_color_hex(0xF57F17), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(y, LV_OPA_COVER, 0);
        lv_obj_align(y, LV_ALIGN_CENTER, 44, -16);
        lv_obj_add_event_cb(y, _yellow_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *yl = lv_label_create(y);
        lv_label_set_text(yl, "TIMER");
        lv_obj_set_style_text_color(yl, lv_color_white(), 0);
        eos_label_set_font_size(yl, EOS_FONT_SIZE_EXTRA_SMALL);
        lv_obj_center(yl);

        /* 按钮说明 */
        lv_obj_t *gsub = lv_label_create(st);
        lv_label_set_text(gsub, "HOLD TO WAKE");
        lv_obj_align(gsub, LV_ALIGN_CENTER, -44, 42);
        lv_obj_set_style_text_color(gsub, lv_color_hex(0xB0B0B0), 0);
        eos_label_set_font_size(gsub, EOS_FONT_SIZE_MICRO);

        lv_obj_t *ysub = lv_label_create(st);
        lv_label_set_text(ysub, "SET WAKE TIMER");
        lv_obj_align(ysub, LV_ALIGN_CENTER, 44, 42);
        lv_obj_set_style_text_color(ysub, lv_color_hex(0xB0B0B0), 0);
        eos_label_set_font_size(ysub, EOS_FONT_SIZE_MICRO);

        EOS_LOG_D("Power-off page: mode choose shown");
    }
    else if (stage == _STAGE_TIMER)
    {
        lv_obj_t *st = _stage_new();

        /* 标题 */
        lv_obj_t *title = lv_label_create(st);
        lv_label_set_text(title, "WAKE TIMER");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        eos_label_set_font_size(title, EOS_FONT_SIZE_MEDIUM);

        /* BACK(左上) */
        lv_obj_t *back = lv_btn_create(st);
        lv_obj_set_size(back, 88, 36);
        lv_obj_set_style_radius(back, 18, 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x37474F), 0);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x263238), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 8, 8);
        lv_obj_add_event_cb(back, _back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *backl = lv_label_create(back);
        lv_label_set_text(backl, "BACK");
        lv_obj_set_style_text_color(backl, lv_color_white(), 0);
        eos_label_set_font_size(backl, EOS_FONT_SIZE_EXTRA_SMALL);
        lv_obj_center(backl);

        /* HH:MM:SS 字段(点字段切换选中,高亮黄色) */
        const int off[3] = {-48, 0, 48};
        for (int i = 0; i < 3; i++)
        {
            s_fld[i] = lv_label_create(st);
            lv_obj_add_flag(s_fld[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(s_fld[i], _field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_align(s_fld[i], LV_ALIGN_CENTER, off[i], -18);
            eos_label_set_font_size(s_fld[i], EOS_FONT_SIZE_LARGE);
        }
        for (int i = 0; i < 2; i++)
        {
            lv_obj_t *sep = lv_label_create(st);
            lv_label_set_text(sep, ":");
            lv_obj_align(sep, LV_ALIGN_CENTER, (off[i] + off[i + 1]) / 2, -20);
            lv_obj_set_style_text_color(sep, lv_color_hex(0x888888), 0);
            eos_label_set_font_size(sep, EOS_FONT_SIZE_LARGE);
        }

        /* 加减号按钮(ASCII,任何字体都有) */
        for (int i = 0; i < 2; i++)
        {
            lv_obj_t *b = lv_btn_create(st);
            lv_obj_set_size(b, 44, 44);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(0x546E7A), 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(0x37474F), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_align(b, LV_ALIGN_CENTER, (i == 0) ? -26 : 26, 28);
            lv_obj_add_event_cb(b, _adjust_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)((i == 0) ? -1 : 1));
            lv_obj_t *bl = lv_label_create(b);
            lv_label_set_text(bl, (i == 0) ? "-" : "+");
            lv_obj_set_style_text_color(bl, lv_color_white(), 0);
            eos_label_set_font_size(bl, EOS_FONT_SIZE_MEDIUM);
            lv_obj_center(bl);
        }

        /* START */
        lv_obj_t *start = lv_btn_create(st);
        lv_obj_set_size(start, 160, 44);
        lv_obj_set_style_radius(start, 22, 0);
        lv_obj_set_style_bg_color(start, lv_color_hex(0x43A047), 0);
        lv_obj_set_style_bg_color(start, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(start, LV_OPA_COVER, 0);
        lv_obj_align(start, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_obj_add_event_cb(start, _start_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *startl = lv_label_create(start);
        lv_label_set_text(startl, "START");
        lv_obj_set_style_text_color(startl, lv_color_white(), 0);
        eos_label_set_font_size(startl, EOS_FONT_SIZE_MEDIUM);
        lv_obj_center(startl);

        _stage_update_fields();
        EOS_LOG_D("Power-off page: wake timer setup shown");
    }
}

/* 点遮罩空白:关闭关机页(防误触) */
static void _mask_cb(lv_event_t *e)
{
    (void)e;
    EOS_LOG_D("Power-off page dismissed by tap on blank area");
    eos_power_off_page_close();
}

void eos_power_off_page_open(void)
{
    if (s_page)
    {
        lv_obj_move_foreground(s_page);
        return;
    }

    lv_obj_t *layer = lv_layer_top();
    s_page = lv_obj_create(layer);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_page, _mask_cb, LV_EVENT_CLICKED, NULL);

    /* 阶段 1 也放进 s_stage,保证"关机图标淡去"动画一致 */
    lv_obj_t *st = _stage_new();
    s_next_stage = _STAGE_NONE;

    /* 红色圆形关机按钮 */
    lv_obj_t *btn = lv_btn_create(st);
    lv_obj_set_size(btn, 96, 96);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xB71C1C), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(btn, 24, 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -16);
    lv_obj_add_event_cb(btn, _power_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_center(icon);

    lv_obj_t *hint = lv_label_create(st);
    lv_label_set_text(hint, "Power Off?");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 56);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xFFFFFF), 0);
    eos_label_set_font_size(hint, EOS_FONT_SIZE_MEDIUM);

    lv_obj_t *sub = lv_label_create(st);
    lv_label_set_text(sub, "Tap to choose mode");
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 82);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xB0B0B0), 0);
    eos_label_set_font_size(sub, EOS_FONT_SIZE_EXTRA_SMALL);

    EOS_LOG_I("Power-off page opened");
}

void eos_power_off_page_close(void)
{
    if (!s_page)
        return;
    lv_obj_delete(s_page);
    s_page  = NULL;
    s_stage = NULL;
    s_next_stage = _STAGE_NONE;
    EOS_LOG_D("Power-off page closed");
}

bool eos_power_off_page_is_open(void)
{
    return s_page != NULL;
}
