/**
 * @file eos_usb_msc.c
 * @brief USB MSC App 主体:USB 检测 / 模式切换 / 界面冻结 / 图标显示 / 退出恢复。
 *
 * 交付批次:②USB 检测与模式切换 ③界面冻结与图标 ④集成与退出。
 *
 * ── 交互状态机(全部在 LVGL ui_task 上下文,经 lv_timer 驱动,不新建任务) ──
 *
 *   INIT ──(SD 存在?)──▶ 失败:ERR(201)  ──▶ DONE(触摸可用,可退出)
 *     │
 *     └─(通过)─▶ drv_begin()(抢占用 USB 口)─失败─▶ ERR(err) ──▶ DONE
 *                     │
 *                     └─成功─▶ WAIT_ENUM ──(4s 内未枚举)─▶ ERR(204) ──▶ DONE
 *                                  │
 *                                  └─(host 枚举完成)─▶ ACTIVE(OK 图标,冻结 UI+触摸)
 *                                                          │
 *                                                          └─(拔线)→ 退出 App
 *
 * ── 关于"冻结 LVGL 主界面任务"的说明 ──
 *   本 App 的代码本身运行在 ui_task(LVGL 事件/定时器回调)内。若在此处
 *   vTaskSuspend(ui_task) 会立刻自我死锁。因此采用等价且安全的"逻辑冻结":
 *     · 用一个不透明全屏页面接管显示(覆盖下层所有内容);
 *     · 关闭所有 POINTER 输入设备(lv_indev_enable(false))→ 触摸全部失效;
 *     · 注册 swipe_back 处理器并消费手势 → 无法滑回桌面;
 *     · board_pm_usb_msc_hold(true) → 期间禁止 Light Sleep,避免打断 USB;
 *     · SD 已由 drv_begin() 从 FATFS 释放 → 其它 App 的 SD 访问返回错误而非写坏卡。
 *   这样既满足"MSC 期间界面冻结、触摸失效、SD 独占",又不会与 LVGL 自身死锁。
 */
#include "eos_usb_msc.h"

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE

#include <stdio.h>

#include "lvgl.h"
#include "eos_core.h"
#include "eos_activity.h"
#include "ui/system/eos_round_clip.h"

#define EOS_LOG_TAG "UsbMscApp"
#include "eos_log.h"

#include "eos_usb_msc_drv.h"
#include "eos_usb_msc_board.h"
#include "eos_font.h"
#include "eos_service_pm.h"   /* eos_pm_wake_up() */

/* 配色(参考其它 App 的深色风格) */
#define UI_BG      0x000000
#define UI_TEXT    0xFFFFFF
#define UI_SUB     0x9AA0A6
#define UI_OK      0x34C759
#define UI_ERR     0xFF3B30
#define UI_BTN_BG  0x1C1C1E

#define ENUM_TIMEOUT_MS 4000u   /* 等待 PC 枚举的最长时间 */
#define TICK_MS         200u    /* 状态机轮询周期 */

/* ── 布局常量(240x240 圆形屏) ─────────────────────────────
 * 主题默认字号为 30px(EOS_FONT_CFG_LARGE_SIZE),若不显式指定字体,
 * 长 errmsg 会溢出圆形屏并与其它控件重叠。这里所有文本显式用小字号,
 * 并固定各元素纵向位置,确保内容落在圆内接区域内、且不与底部按钮重叠。
 * 纵向预算:图标 10..104 → 标题 108..130 → 错误码 130..147 → 正文 149..185
 *           → 按钮 188..222。 */
#define ICON_Y_BIG     10       /* 94x94 大图标(成功/失败页)顶端基线 */
#define ICON_Y_SMALL   30       /* 36x36 小图标(等待页)顶端基线 */
#define TITLE_Y        108      /* 标题(16px,单行) */
#define CODE_Y         130      /* 错误码(13px,单行) */
#define MSG_Y          149      /* 正文(13px,自动换行,最多约 2 行) */
#define TEXT_W         190      /* 正文换行宽度(圆内接安全宽度) */
#define BTN_W          108
#define BTN_H          34
#define BTN_Y_OFF      (-18)    /* 按钮底边距屏幕底部 18px */

/* 图标资源(resources/images/icon 下的 .c 文件,size 94x94 / 36x36) */
extern const lv_image_dsc_t eos_icon_usb_msc;
extern const lv_image_dsc_t eos_icon_usb_ok;
extern const lv_image_dsc_t eos_icon_usb_err;

typedef enum
{
    STAGE_INIT = 0,
    STAGE_WAIT_ENUM,
    STAGE_ACTIVE,
    STAGE_DONE,
} usb_msc_stage_t;

static eos_activity_t *s_act = NULL;
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_img = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_code = NULL;
static lv_obj_t *s_msg = NULL;
static lv_obj_t *s_btn = NULL;
static lv_timer_t *s_timer = NULL;

static usb_msc_stage_t s_stage = STAGE_INIT;
static uint32_t s_deadline = 0;
static bool s_touch_disabled = false;
static bool s_msc_started = false;
static bool s_teardown_done = true;

/* ── 输入冻结 ─────────────────────────────────────────────── */

static void _set_touch_enabled(bool en)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i != NULL; i = lv_indev_get_next(i)) {
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) {
            lv_indev_enable(i, en);
        }
    }
}

/* ── UI 构建 / 状态渲染 ───────────────────────────────────── */

static void _leave(void);   /* 前置声明:见文件末尾退出路径 */

/* 主动断开:先 tud_disconnect() 通知 PC 安全卸载(避免 PC 端脏拔),
 * 再走统一 teardown + 返回桌面。拔线/点按钮/滑回桌面共用此路径。 */
static void _on_disconnect_clicked(lv_event_t *e)
{
    (void)e;
    /* drv_end()(含 tud_disconnect 通知 PC 安全卸载)由 _teardown 统一执行,
     * 此处只需走退出路径,避免重复卸载。 */
    _leave();
}

static void _make_exit_button(void)
{
    if (s_btn != NULL) {
        return;
    }
    lv_obj_t *btn = lv_button_create(s_root);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, BTN_Y_OFF);
    lv_obj_set_style_radius(btn, BTN_H / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_BTN_BG), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Exit");
    /* 按钮文字同样显式设字号,避免继承 30px 默认字号溢出按钮 */
    eos_label_set_font_size(lbl, EOS_FONT_SIZE_EXTRA_SMALL);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_TEXT), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, _on_disconnect_clicked, LV_EVENT_CLICKED, NULL);
    s_btn = btn;
}

static void _render_icon(const lv_image_dsc_t *dsc, uint32_t color_hex)
{
    if (s_img == NULL) {
        s_img = lv_image_create(s_root);
    }
    if (dsc != NULL) {
        lv_image_set_src(s_img, dsc);
        lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        /* 94x94 大图标(成功/失败)与 36x36 小图标(等待)顶端基线不同,
         * 让两者视觉重心落在同一区域 */
        lv_obj_align(s_img, LV_ALIGN_TOP_MID, 0,
                     (dsc == &eos_icon_usb_msc) ? ICON_Y_SMALL : ICON_Y_BIG);
    } else {
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    }
    (void)color_hex;
}

static void _set_text(const char *title, const char *msg, uint32_t color_hex)
{
    if (s_title == NULL) {
        s_title = lv_label_create(s_root);
        lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, TITLE_Y);
        lv_obj_set_width(s_title, 200);
        lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
        /* 标题 16px;默认 30px 在圆屏上会溢出并与正文重叠 */
        eos_label_set_font_size(s_title, EOS_FONT_SIZE_EXTRA_SMALL);
    }
    if (s_msg == NULL) {
        s_msg = lv_label_create(s_root);
        lv_obj_align(s_msg, LV_ALIGN_TOP_MID, 0, MSG_Y);
        lv_obj_set_width(s_msg, TEXT_W);
        lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_msg, lv_color_hex(UI_SUB), 0);
        /* 正文 13px 小字并自动换行,保证长 errmsg 在圆屏内完整可见 */
        eos_label_set_font_size(s_msg, EOS_FONT_SIZE_MICRO);
    }

    lv_label_set_text(s_title, title ? title : "");
    lv_obj_set_style_text_color(s_title, lv_color_hex(color_hex), 0);
    lv_label_set_text(s_msg, msg ? msg : "");
}

static void _set_error_code(eos_usb_msc_err_t code)
{
    if (s_code == NULL) {
        s_code = lv_label_create(s_root);
        /* 放在标题正下方(而非贴底),避免与正文/按钮重叠 */
        lv_obj_align(s_code, LV_ALIGN_TOP_MID, 0, CODE_Y);
        lv_obj_set_style_text_color(s_code, lv_color_hex(UI_SUB), 0);
        eos_label_set_font_size(s_code, EOS_FONT_SIZE_MICRO);
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "Error code: %d", (int)code);
    lv_label_set_text(s_code, buf);
    lv_obj_clear_flag(s_code, LV_OBJ_FLAG_HIDDEN);
}

static void _show_ok(void)
{
    _render_icon(&eos_icon_usb_ok, UI_OK);
    _set_text("USB Drive Mode", "Sharing SD card with the PC.\nEject or swipe back to exit.", UI_OK);
    if (s_code) {
        lv_obj_add_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    }
    /* ACTIVE 态保留可见的 Disconnect 按钮,并恢复触摸:
     * 用户需要能主动强制断开(拔线/点按钮/滑回桌面皆可退出)。 */
    _make_exit_button();
    if (s_btn) {
        lv_label_set_text(lv_obj_get_child(s_btn, 0), "Disconnect");
        lv_obj_clear_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_touch_disabled) {
        _set_touch_enabled(true);
        s_touch_disabled = false;
    }
}

static void _show_error(eos_usb_msc_err_t code)
{
    _render_icon(&eos_icon_usb_err, UI_ERR);
    _set_text("USB Drive Failed", eos_usb_msc_strerror(code), UI_ERR);
    _set_error_code(code);
    _make_exit_button();
    if (s_btn) {
        lv_obj_clear_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _show_waiting(void)
{
    _render_icon(&eos_icon_usb_msc, UI_TEXT);
    _set_text("Waiting for PC", "Negotiating with the PC...", UI_TEXT);
    if (s_code) {
        lv_obj_add_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_btn) {
        lv_obj_add_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ── 退出 / 清理(幂等) ───────────────────────────────────── */

static void _teardown(void)
{
    if (s_teardown_done) {
        return;
    }
    s_teardown_done = true;

    /* 1) 卸载 USB + 重新挂载 SD(批量④退出恢复步骤 1-3) */
    if (s_msc_started) {
        eos_usb_msc_err_t e = eos_usb_msc_drv_end();
        if (e != EOS_USB_MSC_OK && s_root != NULL) {
            /* 重挂载失败也只提示,不 panic */
            _show_error(e);
        }
        s_msc_started = false;
    }

    /* 2) 恢复触摸(步骤 5) */
    if (s_touch_disabled) {
        _set_touch_enabled(true);
        s_touch_disabled = false;
    }

    /* 3) 解除电源保持(步骤 6:恢复正常调度/休眠) */
    board_pm_usb_msc_hold(false);

    /* 4) 停止状态机 */
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    s_stage = STAGE_DONE;
}

static void _leave(void)
{
    _teardown();
    /* 主动/拔线退出后确保亮屏:MSC 期间可能已熄屏(DISPOFF),
     * 若不唤醒,回到桌面仍是黑屏,用户误以为卡死需重启。 */
    eos_pm_wake_up();
    if (s_act != NULL) {
        eos_activity_back();
    }
}

/* ── 状态机 tick ──────────────────────────────────────────── */

static void _tick(lv_timer_t *t)
{
    (void)t;

    switch (s_stage) {
        case STAGE_INIT: {
            /* 步骤 1:SD 是否为真可移动卡 */
            if (!eos_usb_msc_drv_sd_available()) {
                _show_error(EOS_USB_MSC_ERR_NO_SD);
                s_stage = STAGE_DONE;
                break;
            }

            /* 步骤 2:方案 B —— 主动抢占 USB 口,不再因"调试控制台(monitor)在线"而拒绝。
             * 即便 monitor 正占用 USB-Serial-JTAG,drv_begin() 也会把内部 USB PHY
             * 路由给 USB-OTG,USB 设备身份随后由 MSC 接管;调试串口在此期间静默,
             * 退出 App 时由 drv_end() 自动把 PHY 还给 USB-Serial-JTAG。 */

            /* 步骤 3:释放 SD + 启动 TinyUSB MSC(会接管 USB 口) */
            _show_waiting();
            eos_usb_msc_err_t e = eos_usb_msc_drv_begin();
            if (e != EOS_USB_MSC_OK) {
                _show_error(e);
                s_stage = STAGE_DONE;
                break;
            }
            s_msc_started = true;
            s_deadline = lv_tick_get() + ENUM_TIMEOUT_MS;
            s_stage = STAGE_WAIT_ENUM;
            break;
        }

        case STAGE_WAIT_ENUM: {
            if (eos_usb_msc_drv_mounted()) {
                /* 成功:保持触摸可用(用户需能点 Disconnect / 滑回桌面主动退出),
                 * 仅禁止 Light Sleep 以免打断 USB。 */
                board_pm_usb_msc_hold(true);
                _show_ok();
                s_stage = STAGE_ACTIVE;
                EOS_LOG_I("USB MSC: host mounted, UI active");
                break;
            }
            if ((int32_t)(lv_tick_get() - s_deadline) >= 0) {
                /* 超时未枚举:多为纯充电 / 非数据线 */
                s_msc_started = false;
                eos_usb_msc_drv_end();
                _show_error(EOS_USB_MSC_ERR_NOT_ENUM);
                s_stage = STAGE_DONE;
            }
            break;
        }

        case STAGE_ACTIVE: {
            /* 拔线 → PC 卸载 → 自动退出并恢复 */
            if (!eos_usb_msc_drv_mounted()) {
                EOS_LOG_I("USB MSC: cable removed, restoring");
                _leave();
            }
            break;
        }

        case STAGE_DONE:
        default:
            break;
    }
}

/* ── 生命周期 ─────────────────────────────────────────────── */

static bool _swipe_back(eos_activity_t *self, lv_dir_t dir)
{
    (void)self;
    (void)dir;
    /* 任意阶段滑回桌面都视为主动断开并退出(拔线/点按钮/滑动三者一致)。
     * 返回 true 表示已自行处理,不要再走默认 back。 */
    _on_disconnect_clicked(NULL);
    return true;
}

static void _build_ui(eos_activity_t *act)
{
    s_root = eos_activity_get_view(act);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    eos_round_clip(s_root);

    _show_waiting();
}

static void _on_enter(eos_activity_t *act)
{
    s_act = act;

    /* 每次进入重置全部状态(静态变量在 App 复用间必须复位) */
    s_img = NULL;
    s_title = NULL;
    s_code = NULL;
    s_msg = NULL;
    s_btn = NULL;
    s_timer = NULL;
    s_stage = STAGE_INIT;
    s_deadline = 0;
    s_touch_disabled = false;
    s_msc_started = false;
    s_teardown_done = false;

    eos_activity_set_app_header_visible(act, false);
    eos_activity_set_swipe_back_handler(act, _swipe_back);

    _build_ui(act);

    /* 200ms 轮询状态机(不新建任务) */
    s_timer = lv_timer_create(_tick, TICK_MS, NULL);
}

static void _on_destroy(eos_activity_t *act)
{
    (void)act;
    /* 兜底:无论从哪条路径离开,都确保 USB 卸载 / SD 重挂载 / 触摸恢复 */
    _teardown();
    s_root = NULL;
    s_img = NULL;
    s_title = NULL;
    s_code = NULL;
    s_msg = NULL;
    s_btn = NULL;
    s_act = NULL;
}

static const eos_activity_lifecycle_t s_lifecycle = {
    .on_enter = _on_enter,
    .on_destroy = _on_destroy,
    .on_pause = NULL,
    .on_resume = NULL,
    .on_swipe_back = NULL,
};

void eos_usb_msc_enter(void)
{
    EOS_LOG_I("USB MSC: enter");

    eos_activity_t *act = eos_activity_create(&s_lifecycle);
    if (act == NULL) {
        EOS_LOG_E("USB MSC: activity create failed");
        return;
    }
    eos_activity_set_type(act, EOS_ACTIVITY_TYPE_APP);
    eos_activity_enter(act);
}

#endif /* CONFIG_USB_MSC_APP_ENABLE */
