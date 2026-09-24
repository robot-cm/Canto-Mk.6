/**
 * @file cos_spotify.c
 * @brief Spotify App 主体:USB 耳机探测 / 失败页 / 文件树 / 播放 UI / 退出恢复。
 *
 * 交付批次:
 *   ① Batch 1:骨架 + USB 模式检测 + 失败 UI(含 idf.py monitor 场景)。
 *   ② Batch 3:SD 文件树 + 解码 + 歌词。
 *   ③ Batch 4:音量弧 / 速度弧 + 完整退出恢复 + 拔线自动退出。
 *
 * ── 交互状态机(全部在 LVGL ui_task 上下文,经 lv_timer 驱动,不新建任务) ──
 *
 *   INIT ──(无 /sdcard/spotify)──▶ ERR(0x01) ──▶ DONE(可滑动退出)
 *     │
 *     └─(目录存在)─▶ PROBE ──(begin() 失败)─▶ ERR(0x02/0x03/0x04/0x06) ──▶ DONE
 *                        │
 *                        └─(UAC 就绪)─▶ TREE/BROWSER(文件树)
 *                                            │
 *                                            └─(点箭头播放)─▶ PLAYING
 *                                                                │
 *                                                (拔线 / 手势退出)─▶ TEARDOWN ──▶ 回 Launcher
 *
 * ── 冻结/退出说明 ──
 *   与 USB MSC 相同,本 App 运行在 ui_task 内,不能 vTaskSuspend(ui_task)。
 *   退出时通过 cos_activity_back() 让框架回收 view;所有 USB/SD/堆资源由
 *   _teardown() 幂等释放,on_destroy 兜底再调一次,保证任意路径干净退出。
 */
#include "cos_spotify.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"
#include "cos_core.h"
#include "cos_activity.h"
#include "cos_mem.h"
#include "cos_storage_paths.h"
#include "cos_service_storage.h"
#include "cos_usb_msc_board.h"   /* board_sd_is_real / board_sd_acquire */
#include "ui/system/cos_round_clip.h"

#define COS_LOG_TAG "Spotify"
#include "cos_log.h"

#include "cos_spotify_board.h"
#include "cos_spotify_uac.h"
#include "cos_spotify_player.h"
#include "cos_font.h"

/* ── 配置 ─────────────────────────────────────────────────── */

#define SP_DIR          "/sdcard/spotify"   /* 音乐根目录            */
#define PROBE_TIMEOUT_MS 4000u              /* 等待 UAC 枚举最长时间 */
/* 播放态需要 50ms 节奏(解码分片 + 歌词滚动);探测/错误页也用同一周期,
 * 开销可忽略。 */
#define TICK_MS          50u                /* 状态机/播放刷新周期   */

/* ── 配色(参考 USB MSC / Texthub 深色风格) ──────────────── */

#define UI_BG      0x000000
#define UI_TEXT    0xFFFFFF
#define UI_SUB     0x9AA0A6
#define UI_OK      0x1ED760   /* Spotify green */
#define UI_ERR     0xFF3B30
#define UI_BTN_BG  0x1C1C1E

/* ── 失败页布局常量(240x240 圆形屏) ─────────────────────── */
/* 纵向预算:图标 10..104 → 标题 108..130 → 错误码 130..147
 *           → 正文 149..185 → 按钮 188..222 */
#define ICON_Y     10
#define TITLE_Y    108
#define CODE_Y     130
#define MSG_Y      149
#define TEXT_W     200
#define BTN_W      108
#define BTN_H      34
#define BTN_Y_OFF  (-18)

/* ── 图标资源(resources/images/icon 下的 .c 文件) ───────── */
extern const lv_image_dsc_t cos_icon_spotify;
extern const lv_image_dsc_t cos_icon_earphone_err;

/* ── 状态机 ───────────────────────────────────────────────── */

typedef enum
{
    STAGE_INIT = 0,
    STAGE_PROBE,
    STAGE_TREE,
    STAGE_PLAYING,
    STAGE_DONE,
} spotify_stage_t;

/* ── App 状态 ─────────────────────────────────────────────── */

static cos_activity_t *s_act = NULL;
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_img = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_code = NULL;
static lv_obj_t *s_msg = NULL;
static lv_obj_t *s_btn = NULL;
static lv_timer_t *s_timer = NULL;
static cos_spotify_player_t *s_player = NULL;

static spotify_stage_t s_stage = STAGE_INIT;
static uint32_t s_deadline = 0;
static bool s_uac_started = false;
static bool s_teardown_done = true;
static bool s_pm_held = false;
static bool s_sd_touched = false;   /* 本 App 是否访问过 SD(决定退出时是否校验) */

/* ── 输入冻结(与 USB MSC 一致) ──────────────────────────── */

static void _set_touch_enabled(bool en)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i != NULL; i = lv_indev_get_next(i))
    {
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_enable(i, en);
        }
    }
}

/* ── UI 构建 / 状态渲染 ───────────────────────────────────── */

static void _make_exit_button(void)
{
    if (s_btn != NULL)
    {
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
    cos_label_set_font_size(lbl, COS_FONT_SIZE_EXTRA_SMALL);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_TEXT), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cos_activity_back_cb, LV_EVENT_CLICKED, NULL);
    s_btn = btn;
}

static void _render_icon(const lv_image_dsc_t *dsc)
{
    if (s_img == NULL)
    {
        s_img = lv_image_create(s_root);
    }
    if (dsc != NULL)
    {
        lv_image_set_src(s_img, dsc);
        lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_img, LV_ALIGN_TOP_MID, 0, ICON_Y);
    }
    else
    {
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _set_text(const char *title, const char *msg, uint32_t color_hex)
{
    if (s_title == NULL)
    {
        s_title = lv_label_create(s_root);
        lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, TITLE_Y);
        lv_obj_set_width(s_title, TEXT_W);
        lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
        cos_label_set_font_size(s_title, COS_FONT_SIZE_EXTRA_SMALL);
    }
    if (s_msg == NULL)
    {
        s_msg = lv_label_create(s_root);
        lv_obj_align(s_msg, LV_ALIGN_TOP_MID, 0, MSG_Y);
        lv_obj_set_width(s_msg, TEXT_W);
        lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_msg, lv_color_hex(UI_SUB), 0);
        cos_label_set_font_size(s_msg, COS_FONT_SIZE_MICRO);
    }

    lv_label_set_text(s_title, title ? title : "");
    lv_obj_set_style_text_color(s_title, lv_color_hex(color_hex), 0);
    lv_label_set_text(s_msg, msg ? msg : "");
}

static void _set_error_code(cos_spotify_err_t code)
{
    if (s_code == NULL)
    {
        s_code = lv_label_create(s_root);
        lv_obj_align(s_code, LV_ALIGN_TOP_MID, 0, CODE_Y);
        lv_obj_set_style_text_color(s_code, lv_color_hex(UI_SUB), 0);
        cos_label_set_font_size(s_code, COS_FONT_SIZE_MICRO);
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "Error code: 0x%02X", (unsigned)code);
    lv_label_set_text(s_code, buf);
    lv_obj_clear_flag(s_code, LV_OBJ_FLAG_HIDDEN);
}

static void _show_waiting(void)
{
    _render_icon(&cos_icon_spotify);
    _set_text("Spotify", "Detecting USB-C earphone...", UI_TEXT);
    if (s_code)
    {
        lv_obj_add_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_btn)
    {
        lv_obj_add_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _show_error(cos_spotify_err_t code)
{
    _render_icon(&cos_icon_earphone_err);
    _set_text("Playback Unavailable", cos_spotify_strerror(code), UI_ERR);
    _set_error_code(code);
    _make_exit_button();
    if (s_btn)
    {
        lv_obj_clear_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 隐藏失败页遗留元素,为文件树/播放界面腾出屏幕。 */
static void _hide_error_widgets(void)
{
    if (s_img)
    {
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_title)
    {
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_code)
    {
        lv_obj_add_flag(s_code, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_msg)
    {
        lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_btn)
    {
        lv_obj_add_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 前向声明:退出 App(定义在下方) */
static void _leave(void);

/* 播放界面回调:用户在播放态点了返回 → 回文件树(此处无需额外动作,
 * player 内部已切换页面)。 */
static void _player_on_back(void *user)
{
    (void)user;
    COS_LOG_I("Spotify: back to browser");
}

/* 播放界面回调:请求退出 App。 */
static void _player_on_exit(void *user)
{
    (void)user;
    _leave();
}

/* 创建文件树 + 播放界面(Batch 3 起进入此状态)。 */
static bool _show_player(void)
{
    _hide_error_widgets();

    s_player = cos_spotify_player_create(s_root, _player_on_back, _player_on_exit, NULL);
    if (s_player == NULL)
    {
        COS_LOG_E("Spotify: player create failed");
        _show_error(COS_SPOTIFY_ERR_NO_DIR);
        return false;
    }
    return true;
}

/* ── 退出 / 清理(幂等,严格按任务书 §3.4 顺序) ───────────── */

static void _teardown(void)
{
    if (s_teardown_done)
    {
        return;
    }
    s_teardown_done = true;

    /* 步骤 1-2:停解码器 + 清空 PCM 环形缓冲。
     * player_destroy 内部调用 cos_spotify_audio_close(),它负责:
     *   · 停止解码循环(pump 不再被调用)
     *   · 丢弃环形缓冲中的未发送数据(_ring_reset)
     *   · 关闭文件、销毁 MP3 解码器、释放歌词
     * 必须在 uac_end() 之前执行,避免等时端点回调访问已释放资源。 */
    if (s_player != NULL)
    {
        cos_spotify_player_destroy(s_player);
        s_player = NULL;
    }

    /* 步骤 3-6:停 UAC 流 → 去初始化 TinyUSB → 归还 PHY → 恢复 USJ。
     * cos_spotify_uac_end() 内部严格按此顺序执行,幂等。 */
    if (s_uac_started)
    {
        cos_spotify_uac_end();
        s_uac_started = false;
    }

    /* 步骤 7:恢复 SD 卡可用性(最多 3 次,间隔 50ms)。
     * 本 App 运行期间未卸载 SD(见分析报告 §Q5:UAC 主机模式与 SD 不冲突,
     * 跨 App 互斥由本 App 的主持权自然形成),因此这里做的是
     * "确认挂载仍然有效;若已被外部破坏则重新挂载"。
     * 失败只记录,绝不阻塞退出。 */
    if (s_sd_touched)
    {
        bool ok = false;
        for (int attempt = 0; attempt < 3; attempt++)
        {
            ok = cos_storage_is_dir(SP_DIR) || board_sd_is_real();
            if (ok)
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            board_sd_acquire();   /* 重新挂载(移植层幂等) */
        }
        if (!ok)
        {
            COS_LOG_E("Spotify: SD remount failed (%d)", COS_SPOTIFY_ERR_REMOUNT);
        }
        else
        {
            COS_LOG_I("Spotify: SD ok after teardown");
        }
        s_sd_touched = false;
    }

    /* 步骤 8:LVGL 对象由 cos_activity 回收 view 时统一销毁;
     *         此处只需确保播放器释放了非 LVGL 资源(已在步骤 1 完成)。 */

    /* 恢复触摸 */
    _set_touch_enabled(true);

    /* 解除电源保持 */
    if (s_pm_held)
    {
        board_spotify_pm_hold(false);
        s_pm_held = false;
    }

    /* 步骤 9:停止状态机并释放本 App 堆内存。
     * 播放器(含文件树/歌词/解码器)已在步骤 1 释放;
     * 此处仅剩 LVGL 定时器。 */
    if (s_timer != NULL)
    {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    s_stage = STAGE_DONE;

    /* 步骤 10:返回 App 启动器 —— 由 _leave() 中的 cos_activity_back() 完成 */
}

static void _leave(void)
{
    _teardown();
    if (s_act != NULL)
    {
        cos_activity_back();
    }
}

/* ── 状态机 tick ──────────────────────────────────────────── */

static void _tick(lv_timer_t *t)
{
    (void)t;

    switch (s_stage)
    {
        case STAGE_INIT:
        {
            /* 步骤 1:/sdcard/spotify 是否存在 */
            if (!cos_storage_is_dir(SP_DIR))
            {
                COS_LOG_W("Spotify: %s not found", SP_DIR);
                _show_error(COS_SPOTIFY_ERR_NO_DIR);
                s_stage = STAGE_DONE;
                break;
            }

            /* 步骤 2:进入探测(方案 B:即使 monitor 在线也先尝试抢占 PHY) */
            s_sd_touched = true;   /* 已读取过 SD,退出时需校验挂载 */
            _show_waiting();
            s_deadline = lv_tick_get() + PROBE_TIMEOUT_MS;
            s_stage = STAGE_PROBE;

            /* Batch 2:begin() 内部完成 usb_new_phy(HOST) + TinyUSB Host 起栈
             * + 等待 UAC 设备枚举(4s 超时);成功即已枚举到可用的等时 OUT 端点。 */
            cos_spotify_err_t err = COS_SPOTIFY_ERR_NO_EARPHONE;
            if (!cos_spotify_uac_begin(&err))
            {
                _show_error(err);
                s_stage = STAGE_DONE;
                break;
            }
            s_uac_started = true;
            s_pm_held = true;
            board_spotify_pm_hold(true);
            COS_LOG_I("Spotify: UAC ready, entering browser");
            if (!_show_player())
            {
                s_stage = STAGE_DONE;
                break;
            }
            s_stage = STAGE_TREE;
            break;
        }

        case STAGE_PROBE:
        {
            if (cos_spotify_uac_ready())
            {
                s_uac_started = true;
                s_pm_held = true;
                board_spotify_pm_hold(true);
                if (!_show_player())
                {
                    s_stage = STAGE_DONE;
                    break;
                }
                s_stage = STAGE_TREE;
                break;
            }
            if ((int32_t)(lv_tick_get() - s_deadline) >= 0)
            {
                _show_error(COS_SPOTIFY_ERR_NO_EARPHONE);
                s_stage = STAGE_DONE;
            }
            break;
        }

        case STAGE_TREE:
        case STAGE_PLAYING:
        {
            /* 拔线检测:UAC 设备消失 → 自动退出并恢复 */
            if (s_uac_started && !cos_spotify_uac_connected())
            {
                COS_LOG_I("Spotify: earphone removed, restoring");
                _leave();
                break;
            }
            /* 播放刷新:解码分片 → PCM 环形缓冲 → UAC 推送 → 歌词/进度。
             * 全部在 ui_task 内完成(不新增任务)。 */
            if (s_player != NULL)
            {
                cos_spotify_player_tick(s_player);
                s_stage = cos_spotify_player_is_playing(s_player) ? STAGE_PLAYING
                                                                 : STAGE_TREE;
            }
            break;
        }

        case STAGE_DONE:
        default:
            break;
    }
}

/* ── 生命周期 ─────────────────────────────────────────────── */

static bool _swipe_back(cos_activity_t *self, lv_dir_t dir)
{
    (void)self;

    /* 播放态:第一次滑动先回到文件树,第二次才退出 App
     * (更符合直觉,也避免误触打断播放)。
     * 失败页 / 文件树:直接放行,由框架执行返回。 */
    if (dir == LV_DIR_LEFT && s_stage == STAGE_PLAYING && s_player != NULL)
    {
        /* 请求播放器切回文件树页(不停止播放),消费本次手势 */
        if (cos_spotify_player_show_browser(s_player))
        {
            s_stage = STAGE_TREE;
            return true;   /* 消费:不退 App */
        }
    }

    /* 放行:框架将执行 activity 返回;
     * 真正的资源释放由 _on_destroy() → _teardown() 兜底完成。 */
    _teardown();
    return false;
}

static void _build_ui(cos_activity_t *act)
{
    s_root = cos_activity_get_view(act);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    cos_round_clip(s_root);

    _show_waiting();
}

static void _on_enter(cos_activity_t *act)
{
    s_act = act;

    /* 每次进入重置全部状态(静态变量在 App 复用间必须复位) */
    s_img = NULL;
    s_title = NULL;
    s_code = NULL;
    s_msg = NULL;
    s_btn = NULL;
    s_timer = NULL;
    s_player = NULL;
    s_stage = STAGE_INIT;
    s_deadline = 0;
    s_uac_started = false;
    s_teardown_done = false;
    s_pm_held = false;
    s_sd_touched = false;
    s_deadline = 0;

    cos_activity_set_app_header_visible(act, false);
    cos_activity_set_swipe_back_handler(act, _swipe_back);

    _build_ui(act);

    /* 200ms 轮询状态机(不新建任务) */
    s_timer = lv_timer_create(_tick, TICK_MS, NULL);
}

static void _on_destroy(cos_activity_t *act)
{
    (void)act;
    /* 兜底:无论从哪条路径离开,都确保 USB/SD/触摸/电源恢复 */
    _teardown();
    s_root = NULL;
    s_img = NULL;
    s_title = NULL;
    s_code = NULL;
    s_msg = NULL;
    s_btn = NULL;
    s_player = NULL;
    s_act = NULL;
}

static const cos_activity_lifecycle_t s_lifecycle = {
    .on_enter = _on_enter,
    .on_destroy = _on_destroy,
    .on_pause = NULL,
    .on_resume = NULL,
    .on_swipe_back = NULL,
};

void cos_spotify_enter(void)
{
    COS_LOG_I("Spotify: enter");

    cos_activity_t *act = cos_activity_create(&s_lifecycle);
    if (act == NULL)
    {
        COS_LOG_E("Spotify: activity create failed");
        return;
    }
    cos_activity_set_type(act, COS_ACTIVITY_TYPE_APP);
    cos_activity_enter(act);
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
