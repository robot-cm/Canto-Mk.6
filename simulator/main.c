/**
 * @file main.c
 * @brief CantoMk6OS Desktop Simulator entry point
 *
 * Initializes LVGL with SDL2 display driver, then boots CantoMk6OS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "lvgl.h"
#include "drivers/sdl/lv_sdl_window.h"
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_keyboard.h"   /* 键盘热键经 LVGL 键盘 indev 统一分发 */
#include "cos_core.h"
#include "kernel/scheduler/cos_dispatcher.h"   /* cos_dispatch_tick() for faithful deferred-cleanup pumping */
#include "cos_log.h"
#include "framework/app/cos_app_list.h"
#include "framework/app/cos_app.h"
#include "framework/activity/cos_activity.h"
#include "services/storage/cos_storage_paths.h"
#include "services/storage/cos_service_storage.h"
#include "port/file_system/cos_fs_port.h"   /* draw-launch probe: cos_fs_open_read/size/read to verify .edrw */
#include "cJSON.h"              /* album-probe: 模拟 FM 写 app 私有 config */
#include "apps/gallery/cos_gallery.h"
#include "apps/files/cos_files.h"
#include "apps/flash_light/cos_flash_light.h"

#include <SDL2/SDL.h>

#include "cos_shell.h"
#include "cos_shell_framework.h"
#include "ui/widgets/keyboard/cos_keyboard.h"
#include "ui/framework/cos_ui_framework.h"
#include "ui/framework/render/cos_framework_home.h" /* cos_ui_framework.h 不透传 framework_home 类型 */
#include "ui/launcher/cos_launcher.h"   /* cos_launcher_build_home() */
#include "ui/system/cos_round_clip.h"     /* cos_round_clip() */
#include "ui/widgets/basic_widgets/cos_basic_widgets.h" /* cos_draw_buf_create */
#include "spm.h"                         /* spm_app_run / spm_app_stop (in-memory JS) */
#include "devices/time/cos_dev_time.h"                 /* alarm-test: fake clock for ring test */
#include "services/config/cos_service_config.h"        /* alarm-test: persistent state asserts */
#include "ui/widgets/cards_page/cos_cards_page.h" /* cards page + app registration API */
#include "apps/power_off/cos_power_off_page.h"   /* home-gesture-sim-test: 上滑 = 关机页 */
#include "framework/watchface/cos_watchface.h"              /* cos_watchface_instance_t（builtin.h 不自带 typedef） */
#include "framework/watchface/cos_watchface_builtin.h"      /* builtin watchface + sim swipe hook */
#include "ui/widgets/control_center/cos_control_center.h"   /* control center overlay */
#include "ui/wos/cos_wos.h"                                 /* WOS demo app hotkey 'W' */
#include "ui/widgets/swipe_panel/cos_swipe_panel.h"         /* swipe panel state */
#include "ui/widgets/slide_widget/cos_slide_widget.h"       /* slide widget state */
#include "input/crown/cos_crown.h"                          /* 模拟表冠旋转 / 表冠按下 */
#include "input/side_button/cos_side_button.h"              /* 模拟侧键 */
#include "services/pm/cos_service_pm.h"                     /* 自检前置: 拉长熄屏超时/强制唤醒(虚拟时间快进会误触发熄屏) */

#include <math.h>
#include <string.h>

/* Crash localizer: on an access violation / abort, dump the faulting address
 * and a stack backtrace so we can map it with addr2line without a debugger. */
#ifdef _WIN32
static LONG WINAPI _crash_filter(EXCEPTION_POINTERS *ep)
{
    void *stack[40];
    unsigned short n = CaptureStackBackTrace(0, 40, stack, NULL);
    HMODULE mod = NULL;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                      (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &mod);
    uintptr_t base = (uintptr_t)mod;
    fprintf(stderr, "CRASH exception=0x%lx ip=%p base=%p\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            (void *)ep->ExceptionRecord->ExceptionAddress, (void *)base);
    fprintf(stderr, "  ip_rva=%p\n", (void *)((uintptr_t)ep->ExceptionRecord->ExceptionAddress - base));
    for (unsigned short i = 0; i < n; i++)
        fprintf(stderr, "  #%u %p (rva=%p)\n", i, stack[i],
                (void *)((uintptr_t)stack[i] - base));
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif /* _WIN32 */

/* Display dimensions matching XIAO round display */
#define SIM_HOR_RES  240
#define SIM_VER_RES  240

static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* Shell console backend (simulator) ------------------------- */
static void sim_shell_out(const char *line, void *user)
{
    (void)user;
    printf("%s\n", line);
    fflush(stdout);
}

/* Raw echo sink for the interactive shell framework (no added newline). */
static void sim_echo(const char *s, void *user)
{
    (void)user;
    fputs(s, stdout);
    fflush(stdout);
}

/* Completed-line handler: dispatch a finished command line to the Core engine. */
static void sim_line_handler(const char *line, void *user)
{
    (void)user;
    cos_shell_exec(line, sim_shell_out, NULL);
}

/* Self-test mode: run a fixed set of commands and exit. */
static void sim_run_shell_test(void)
{
    /* The SDL display driver overrides LVGL's tick source, so lv_tick_inc()
     * is ignored. Restore the default tick so our pump loop below actually
     * advances timers/animations between commands. */
    lv_tick_set_cb(NULL);

    const char *cmds[] = {
        "help", "version", "mem", "psram", "flash", "sd",
        "rtc", "display", "touch", "wifi",
        "wifi enable",
        "wifi scan",
        "wifi connect HomeMesh_2.4G mypassword",
        "wifi status",
        "wifi disconnect",
        "wifi save",
        "bt enable",
        "bt scan",
        "bt connect AA:BB:CC:11:22:33",
        "bt paired",
        "bt status",
        "bt disable",
        "proxy status",
        "proxy set host 10.0.0.1",
        "proxy set port 1080",
        "proxy set user alice",
        "proxy set pass secret123",
        "proxy enable",
        "proxy status",
        "proxy connect example.com 80",
        "proxy disable",
        "apps",
        "app info com.cantomk6.timer", "log", "reboot",
        /* New Shell-framework / Plugin-Manager / IME commands */
        "plugin scan", "plugin scan --force", "plugin list",
        "ime ni", "ime zhongguo", "ime jianpan", NULL
    };
    printf("=== CantoMk6OS Shell self-test ===\n\n");
    for (int i = 0; cmds[i]; i++)
    {
        printf("> %s\n", cmds[i]);
        cos_shell_exec(cmds[i], sim_shell_out, NULL);
        printf("\n");
        /* Pump LVGL so timers (sleep timer, wos deferred cleanup, anims)
         * actually run between commands, like the real main loop. */
        for (int k = 0; k < 20; k++)
        {
            lv_tick_inc(30);
            lv_timer_handler();
        }
    }
    /* ---- Plugin-chain smoke test (real install -> launch -> run -> release) ----
     * Only runs if a seed .eapk is staged in the FS. Exercises the full chain
     * headlessly: scan .eapk -> parse manifest -> register with the Plugin
     * Manager -> Launcher lists it -> launch via SPM -> JerryScript runs
     * main.js -> back releases all resources. */
    {
        char eapk[COS_FS_PATH_MAX];
        snprintf(eapk, sizeof(eapk), "%s.sys/app/com.cantomk6.timer.eapk", COS_SYS_ROOT_DIR);
        if (cos_storage_is_file(eapk))
        {
            char cmd[320];
            printf("> app install %s\n", eapk);
            snprintf(cmd, sizeof(cmd), "app install %s", eapk);
            cos_shell_exec(cmd, sim_shell_out, NULL);
            printf("\n");

            uint32_t n = cos_app_get_installed();
            for (uint32_t i = 0; i < n; i++)
            {
                const char *aid = cos_app_list_get_id(i);
                if (!aid)
                    continue;
                if (strcmp(aid, "sys.settings") == 0
                    || strcmp(aid, "sys.flash_light") == 0
#if COS_ENABLE_TEST_APP
                    || strcmp(aid, "sys.test") == 0
#endif
                )
                    continue;
                printf("> app start %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app start %s", aid);
                cos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                printf("> app stop %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app stop %s", aid);
                cos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                /* Disable / enable cycle (management surface) */
                printf("> app disable %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app disable %s", aid);
                cos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                printf("> app enable %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app enable %s", aid);
                cos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                /* Uninstall, then re-install so the app is left installed
                 * for interactive Launcher inspection. */
                printf("> app uninstall %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app uninstall %s", aid);
                cos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                printf("> app install %s\n", eapk);
                snprintf(cmd, sizeof(cmd), "app install %s", eapk);
                cos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");
                break;
            }
        }
        else
        {
            printf("[smoke-test] no seed .eapk at %s (skipping plugin-chain test)\n", eapk);
        }
    }

    /* ---- Native media apps smoke test (enter + back, headless) ---- */
    {
        printf("> gallery enter\n");
        cos_gallery_enter();
        printf("> activity back\n");
        cos_activity_back();
        printf("> files enter\n");
        cos_files_enter();
        printf("> activity back\n");
        cos_activity_back();
        printf("\n");
    }

    printf("=== Shell self-test done ===\n");
}

/* Stress mode: hammer activity enter/destroy + plugin start/stop to surface
 * lifecycle leaks / use-after-free / crashes that only appear after many
 * interactions. Headless; exits when done (or aborts on a crash). LVGL timers
 * are pumped with an advancing tick so deferred destroys and the flashlight
 * 200ms timer actually run between cycles. */
static void sim_run_stress_test(void)
{
    const int rounds = 40;
    printf("=== CantoMk6OS Stress test (%d rounds) ===\n", rounds);
    fflush(stdout);
    for (int i = 0; i < rounds; i++)
    {
        /* Flashlight: full enter -> back (card pager + timer + exit button) */
        cos_app_launch_immediately("com.cantomk6.timer");
        cos_activity_back();

        /* Timer plugin via Plugin Manager + SPM/JerryScript lifecycle */
        cos_shell_exec("app start com.cantomk6.timer", sim_shell_out, NULL);
        cos_shell_exec("app stop com.cantomk6.timer", sim_shell_out, NULL);

        /* Native media apps */
        cos_gallery_enter();
        cos_activity_back();
        cos_files_enter();
        cos_activity_back();

        /* Advance LVGL time and pump timers/animations so transitions,
         * deferred activity destroys and the flashlight timer callback run. */
        for (int k = 0; k < 8; k++)
        {
            lv_tick_inc(30);
            lv_timer_handler();
        }

        printf(".");
        fflush(stdout);
    }
    printf("\n=== Stress test done ===\n");
}

/* ---- 串口式 Shell 控制台(交互模式下把 stdin 接进 Shell 框架) ----
 * Windows: conio _kbhit/_getch;Linux: termios raw + 非阻塞 read。
 * 两端都只做"读单字节",行缓冲/历史/回显/prompt 由 Shell 框架承担。 */
static int g_console = 0;

static void sim_reboot(void)
{
    g_running = 0;
}

#ifdef _WIN32
#include <conio.h>
#include <io.h>

static void sim_console_init(void)
{
    g_console = (_isatty(_fileno(stdin)) != 0);
}

/* 无按键返回 -1;扩展键(0x00 / 0xE0 前缀)丢弃,避免框架收到方向键垃圾字节 */
static int sim_console_getch(void)
{
    if (!_kbhit())
        return -1;
    int c = _getch();
    if (c == 0 || c == 0xE0) { _getch(); return -1; }
    return c;
}
#else
#include <termios.h>
#include <fcntl.h>
/* unistd.h 已在文件顶部(非 Windows 分支)包含 */

static struct termios s_saved_tio;
static int s_tio_saved = 0;

static void sim_console_restore(void)
{
    if (s_tio_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &s_saved_tio);
}

static void sim_console_init(void)
{
    g_console = (isatty(STDIN_FILENO) != 0);
    if (!g_console)
        return;
    if (tcgetattr(STDIN_FILENO, &s_saved_tio) != 0)
    {
        g_console = 0;
        return;
    }
    s_tio_saved = 1;
    atexit(sim_console_restore);
    struct termios raw = s_saved_tio;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO); /* 逐字节读取,框架负责回显 */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (fl != -1)
        fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
}

static int sim_console_getch(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n == 1) ? (int)c : -1;
}
#endif /* _WIN32 */

static void sim_console_poll(void)
{
    if (!g_console)
        return;
    int c;
    while ((c = sim_console_getch()) >= 0)
        cos_shell_framework_feed((char)c, sim_echo, sim_line_handler, NULL);
}

/* Seed bundled demo plugins so the interactive Launcher shows them even on a
 * fresh FS. Idempotent: skips any app already installed. Runs on every boot
 * (both interactive and --shell-test modes). */
static void _sim_seed_install_plugins(void)
{
    static const char *const seeds[] = {
        "com.cantomk6.alarm",
        "com.cantomk6.breach",
        "com.cantomk6.calculator",
        "com.cantomk6.calendar",
        "com.cantomk6.globaltime",
        "com.cantomk6.stopwatch",
        "com.cantomk6.texthub",
        "com.cantomk6.timer",
    };
    for (int i = 0; i < (int)(sizeof(seeds) / sizeof(seeds[0])); i++)
    {
        if (cos_app_list_contains(seeds[i]))
            continue;
        char eapk[COS_FS_PATH_MAX];
        snprintf(eapk, sizeof(eapk), "%s.sys/app/%s.eapk", COS_SYS_ROOT_DIR, seeds[i]);
        if (cos_storage_is_file(eapk))
        {
            cos_result_t r = cos_app_install(eapk);
            COS_LOG_I("[Sim] seed-install %s (result=%d)", seeds[i], (int)r);
        }
    }
}

/* Keyboard self-test: drive the on-screen keyboard widget headlessly (no
 * display interaction) to prove both input modes work end-to-end:
 *   EN mode  -> letters are inserted into the bound textarea
 *   ZH mode  -> pinyin accumulates, a candidate is committed into the textarea
 * The widget emits LVGL INSERT/READY events exactly as it does under touch, so
 * this is a faithful exercise of the real code path. */
static void sim_run_keyboard_test(void)
{
    printf("=== CantoMk6OS Keyboard self-test ===\n");
    fflush(stdout);

    lv_obj_t *ta = lv_textarea_create(lv_scr_act());
    lv_textarea_set_text(ta, "");
    lv_obj_t *kb = cos_keyboard_create(lv_scr_act());
    cos_keyboard_set_textarea(kb, ta);

    /* ---- EN mode: type "hello" ---- */
    const char *en[] = { "h", "e", "l", "l", "o", NULL };
    for (int i = 0; en[i]; i++)
        cos_keyboard_send_key(kb, en[i]);
    printf("[EN] textarea: '%s'\n", lv_textarea_get_text(ta));

    /* ---- ZH mode: pinyin "nihao" then commit candidate #1 ---- */
    lv_textarea_set_text(ta, "");
    cos_keyboard_send_key(kb, "MODE");
    printf("[ZH] mode = %d (1 = ZH)\n", (int)cos_keyboard_get_mode(kb));
    const char *zh[] = { "n", "i", "h", "a", "o", NULL };
    for (int i = 0; zh[i]; i++)
        cos_keyboard_send_key(kb, zh[i]);
    cos_keyboard_send_key(kb, "1");
    printf("[ZH] textarea: '%s'\n", lv_textarea_get_text(ta));

    lv_obj_del(kb);
    lv_obj_del(ta);
    printf("=== Keyboard self-test done ===\n");
}

/* UI Framework self-test: exercise the adaptive layout system headlessly
 * across the four required display profiles (req #12) and prove the core
 * subsystems behave. Returns 0 if every check passes, 1 otherwise. */
static int sim_run_ui_framework_test(void)
{
    printf("=== CantoMk6OS UI Framework self-test ===\n");
    fflush(stdout);
    int pass = 0, fail = 0;
#define CHECK(cond, msg) do {                                            \
        if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
        else      { fail++; printf("[FAIL] %s\n", msg); }               \
    } while (0)

    /* ---- 1. DisplayProfile across the 4 required shapes ---- */
    for (int id = 0; id < (int)COS_PROFILE_COUNT; id++) {
        cos_display_profile_t p = cos_display_profiles_get((cos_profile_id_t)id);
        if (p.shape == COS_DISPLAY_SHAPE_CIRCLE)
            CHECK(p.safe_radius > 0.0f && p.safe_radius < p.radius, "profile safe_radius (CIRCLE)");
        else
            CHECK(p.safe_w > 0.0f && p.safe_h > 0.0f, "profile safe_rect (SQUARE/RECT)");
        CHECK(fabsf(cos_dp(&p, 48.0f) - 48.0f * p.dp_scale) < 0.001f, "dp scaling");
        float x = (float)p.width + 50.0f, y = (float)p.height + 50.0f;
        bool moved = cos_display_profile_clamp_inside(&p, &x, &y, 10.0f);
        CHECK(moved && cos_display_profile_is_inside(&p, x, y, 10.0f), "clamp inside safe area");
    }

    /* ---- 2. LayoutManager selection + safe-area containment ---- */
    {
        cos_display_profile_t pc = cos_display_profiles_get(COS_PROFILE_240C);
        cos_display_profile_t ps = cos_display_profiles_get(COS_PROFILE_240S);
        cos_layout_manager_t *lc = cos_layout_manager_create(&pc);
        cos_layout_manager_t *ls = cos_layout_manager_create(&ps);
        CHECK(lc && lc->shape == COS_DISPLAY_SHAPE_CIRCLE, "manager selects CIRCLE");
        CHECK(ls && ls->shape == COS_DISPLAY_SHAPE_SQUARE, "manager selects SQUARE");

        cos_widget_t w[8];
        for (int i = 0; i < 8; i++) {
            memset(&w[i], 0, sizeof(w[i]));
            w[i].kind = COS_WIDGET_ICON; w[i].w_dp = 40; w[i].h_dp = 40;
        }
        lc->layout(lc, &pc, w, 8);
        int inside = 0;
        for (int i = 0; i < 8; i++) {
            float cx = w[i].x + w[i].w * 0.5f, cy = w[i].y + w[i].h * 0.5f;
            float r = (w[i].w > w[i].h ? w[i].w : w[i].h) * 0.5f;
            if (cos_display_profile_is_inside(&pc, cx, cy, r)) inside++;
        }
        CHECK(inside == 8, "circle layout all inside safe area");

        cos_widget_t w2[8];
        for (int i = 0; i < 8; i++) {
            memset(&w2[i], 0, sizeof(w2[i]));
            w2[i].kind = COS_WIDGET_ICON; w2[i].w_dp = 50; w2[i].h_dp = 50;
        }
        ls->layout(ls, &ps, w2, 8);
        int inside2 = 0;
        for (int i = 0; i < 8; i++) {
            float cx = w2[i].x + w2[i].w * 0.5f, cy = w2[i].y + w2[i].h * 0.5f;
            float r = (w2[i].w > w2[i].h ? w2[i].w : w2[i].h) * 0.5f;
            if (cos_display_profile_is_inside(&ps, cx, cy, r)) inside2++;
        }
        CHECK(inside2 == 8, "square layout all inside safe rect");
        lc->destroy(lc); ls->destroy(ls);
    }

    /* ---- 3. ArcList geometry on every profile ---- */
    for (int id = 0; id < (int)COS_PROFILE_COUNT; id++) {
        cos_display_profile_t p = cos_display_profiles_get((cos_profile_id_t)id);
        cos_arclist_t al;
        cos_arclist_init(&al, &p, 12);
        CHECK(cos_arclist_focus_index(&al) == 0, "arclist focus index 0 at start");
        CHECK(fabsf(al.items[0].scale - al.focus_scale) < 0.01f, "arclist focus max scale");
        CHECK(fabsf(al.items[0].opacity - 1.0f) < 0.01f, "arclist focus full opacity");
        bool mono = (al.items[0].scale >= al.items[1].scale) &&
                    (al.items[1].scale >= al.items[2].scale);
        CHECK(mono, "arclist scale monotonic with distance");
        int vis_ok = 1;
        for (int i = 0; i < 12; i++) {
            if (!al.items[i].visible) continue;
            float r = 22.0f * p.dp_scale * al.items[i].scale;
            if (!cos_display_profile_is_inside(&p, al.items[i].x, al.items[i].y, r))
                vis_ok = 0;
        }
        CHECK(vis_ok, "arclist visible items inside safe area");
    }

    /* ---- 4. Physics: friction decay stays in bounds ---- */
    {
        cos_scroller_t s; cos_scroller_init(&s, 0.0f, 1000.0f);
        s.pos = 0.0f; s.vel = 1000.0f;
        for (int i = 0; i < 200; i++) cos_scroller_step(&s, 1.0f / 60.0f);
        CHECK(cos_scroller_is_resting(&s), "physics rests after flick");
        CHECK(s.pos >= 0.0f && s.pos <= 1000.0f, "physics stays within bounds");
        CHECK(s.pos > 100.0f && s.pos < 500.0f, "physics friction distance sane");
    }
    /* ---- 4b. Physics: bounce back from beyond the upper bound ---- */
    {
        cos_scroller_t s; cos_scroller_init(&s, 0.0f, 100.0f);
        s.pos = 100.0f; s.vel = 1500.0f;
        float max_over = 0.0f;
        for (int i = 0; i < 200; i++) {
            cos_scroller_step(&s, 1.0f / 60.0f);
            float over = s.pos - 100.0f;
            if (over > max_over) max_over = over;
        }
        CHECK(cos_scroller_is_resting(&s), "physics bounce rests");
        CHECK(s.pos >= 0.0f && s.pos <= 101.0f, "physics bounce returns to bound");
        CHECK(max_over <= 60.0f, "physics bounce overshoot bounded");
    }

    /* ---- 5. Easing curves ---- */
    {
        CHECK(fabsf(cos_ease_linear(0) - 0) < 1e-3f &&
              fabsf(cos_ease_linear(1) - 1) < 1e-3f, "ease_linear endpoints");
        CHECK(fabsf(cos_ease_out_cubic(1) - 1) < 1e-3f, "ease_out_cubic end");
        float prev = -1.0f; bool mono = true;
        for (int i = 0; i <= 10; i++) {
            float v = cos_ease_out_cubic((float)i / 10.0f);
            if (v < prev - 1e-4f) mono = false;
            prev = v;
        }
        CHECK(mono, "ease_out_cubic monotonic");
        float mx = 0.0f;
        for (int i = 0; i <= 10; i++) {
            float v = cos_ease_out_back((float)i / 10.0f);
            if (v > mx) mx = v;
        }
        CHECK(mx > 1.0f && fabsf(cos_ease_out_back(1) - 1) < 1e-3f,
              "ease_out_back overshoot + end");
    }

    /* ---- 6. AppManager + LayoutStorage round-trip ---- */
    {
        cos_app_manager_t m; cos_app_manager_init(&m);
        cos_app_manager_register(&m, "watch", "Watch", 1);
        cos_app_manager_register(&m, "weather", "Weather", 2);
        cos_app_manager_register(&m, "music", "Music", 3);
        cos_app_manager_register(&m, "clock", "Clock", 4);
        CHECK(cos_app_manager_count(&m) == 4, "app manager count");
        cos_app_manager_move(&m, 0, 3);
        CHECK(cos_app_manager_find(&m, "watch") == 3, "app manager move reorders");
        cos_layout_storage_t ls; cos_layout_storage_init(&ls, "Bubble");
        CHECK(cos_layout_storage_save(&ls, &m), "layout storage save");
        cos_app_manager_t m2; cos_app_manager_init(&m2);
        cos_app_manager_register(&m2, "watch", "Watch", 1);
        cos_app_manager_register(&m2, "weather", "Weather", 2);
        cos_app_manager_register(&m2, "music", "Music", 3);
        cos_app_manager_register(&m2, "clock", "Clock", 4);
        CHECK(cos_layout_storage_load(&ls, &m2), "layout storage load");
        bool same = true;
        for (int i = 0; i < 4; i++)
            if (m.order[i] != m2.order[i]) same = false;
        CHECK(same, "layout storage order round-trip");
    }

    /* ---- 7. ThemeManager round-trip (no live LVGL object needed) ---- */
    {
        cos_theme_t t; cos_theme_manager_init(&t);
        cos_theme_manager_set(&t, "Aurora", 200, 0x33ccff, 22, 60, true);
        char buf[256];
        int n = cos_theme_manager_serialize(&t, buf, sizeof(buf));
        cos_theme_t t2; cos_theme_manager_init(&t2);
        CHECK(n > 0 && cos_theme_manager_deserialize(&t2, buf),
              "theme serialize/deserialize");
        CHECK(strcmp(t2.name, "Aurora") == 0 && t2.bg_alpha == 200 &&
              t2.accent == 0x33ccff && t2.radius == 22 && t2.glow == 60 &&
              t2.glass == true, "theme round-trip values");
        cos_theme_manager_apply(&t, NULL, true);   /* NULL-safe */
        cos_liquid_glass_card(NULL);
        cos_liquid_glass_panel(NULL);
        CHECK(true, "theme/glass apply NULL-safe");
    }

    printf("\n[summary] pass=%d fail=%d\n", pass, fail);
    printf("=== UI Framework self-test done ===\n");
    return (fail == 0) ? 0 : 1;
#undef CHECK
}

/* ------------------------------------------------------------------ */
/* UI Framework rendering-layer demo (P8). Builds the adaptive Home on  */
/* each of the 4 profiles, drives a flick, and asserts every rendered   */
/* item (and the status bar) stays inside the safe area with no LVGL    */
/* crash. Headless-safe (SDL dummy driver).                             */
/* ------------------------------------------------------------------ */
static int sim_run_ui_framework_demo(void)
{
    printf("=== CantoMk6OS UI Framework rendering-layer demo ===\n");
    fflush(stdout);
    int pass = 0, fail = 0;
#define CHECK(cond, msg) do {                                            \
        if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
        else      { fail++; printf("[FAIL] %s\n", msg); }               \
    } while (0)

    static const char *DEMO_APPS[7] = {
        "Clock", "Gallery", "Files", "Settings",
        "Flash", "Weather", "Music"
    };
    const int N = 7;

    lv_obj_t *holder = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, SIM_HOR_RES, SIM_VER_RES);
    lv_obj_set_pos(holder, 0, 0);
    lv_obj_set_style_bg_opa(holder, 0, 0);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(holder, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (int id = 0; id < (int)COS_PROFILE_COUNT; id++) {
        cos_display_profile_t p = cos_display_profiles_get((cos_profile_id_t)id);
        const char *pname = cos_display_profiles_name((cos_profile_id_t)id);

        cos_framework_home_t *h =
            cos_framework_home_create(holder, &p, DEMO_APPS, N, NULL);
        CHECK(h != NULL, "home created");
        if (!h) continue;

        CHECK(lv_obj_is_valid(h->root), "home root valid");
        CHECK(lv_obj_is_valid(h->statusbar), "status bar valid");

        /* Status bar (resolved geometry) must fit inside the safe area — the
         * key "no rectangular overflow on round screens" assertion. We use the
         * stored rect, not lv_obj_get_* (those need an LVGL refresh pass that
         * this headless no-timer demo never runs). */
        float sx, sy, sw, sh;
        cos_framework_home_statusbar_rect(h, &sx, &sy, &sw, &sh);
        float scx = sx + sw * 0.5f, scy = sy + sh * 0.5f;
        float sr = sqrtf((sw * 0.5f) * (sw * 0.5f) + (sh * 0.5f) * (sh * 0.5f));
        CHECK(cos_display_profile_is_inside(&p, scx, scy, sr),
              "status bar inside safe area");

        /* Drive a flick + inertia, then verify items stay contained. */
        cos_framework_home_drag(h, -50.0f);
        cos_framework_home_release(h);
        for (int f = 0; f < 90; f++) cos_framework_home_step(h, 16.7f);

        int vis_ok = 1, cnt = 0;
        if (h->is_circle && h->arc) {
            cnt = cos_arclist_view_count(h->arc);
            for (int i = 0; i < cnt; i++) {
                const cos_arclist_item_t *it = &h->arc->al.items[i];
                if (!it->visible) continue;
                float r = (h->arc->card_px * it->scale) * 0.5f;
                if (!cos_display_profile_is_inside(&p, it->x, it->y, r))
                    vis_ok = 0;
            }
        } else if (h->grid) {
            cnt = cos_layout_view_count(h->grid);
            for (int i = 0; i < cnt; i++) {
                cos_widget_t *w = &h->grid->widgets[i];
                float cx = w->x + w->w * 0.5f, cy = w->y + w->h * 0.5f;
                float r = (w->w > w->h ? w->w : w->h) * 0.5f;
                if (!cos_display_profile_is_inside(&p, cx, cy, r))
                    vis_ok = 0;
            }
        }
        CHECK(cnt == N, "app count matches");
        CHECK(vis_ok, "all rendered items inside safe area");
        (void)pname;

        lv_anim_del_all();
        cos_framework_home_destroy(h);
        lv_obj_clean(holder);
    }

    lv_obj_del(holder);
    printf("\n[summary] pass=%d fail=%d\n", pass, fail);
    printf("=== UI Framework rendering-layer demo done ===\n");
    return (fail == 0) ? 0 : 1;
#undef CHECK
}

/* ------------------------------------------------------------------ */
/* Launcher migration test: build the REAL Launcher Home (real Plugin  */
/* Manager app list) on every profile and prove the migration is       */
/* shape-agnostic and overflow/occlusion/hit-area correct — asserting  */
/* against the framework's resolved geometry (never lv_obj_get_*).      */
/* ------------------------------------------------------------------ */
static int sim_run_launcher_test(void)
{
    printf("=== CantoMk6OS Launcher migration test ===\n");
    fflush(stdout);
    int pass = 0, fail = 0;
#define CHECK(cond, msg) do {                                            \
        if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
        else      { fail++; printf("[FAIL] %s\n", msg); }               \
    } while (0)

    lv_obj_t *holder = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, SIM_HOR_RES, SIM_VER_RES);
    lv_obj_set_pos(holder, 0, 0);
    lv_obj_set_style_bg_opa(holder, 0, 0);
    lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(holder, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (int id = 0; id < (int)COS_PROFILE_COUNT; id++) {
        cos_display_profile_t p = cos_display_profiles_get((cos_profile_id_t)id);

        cos_framework_home_t *h = NULL;
        int n = cos_launcher_build_home(holder, &p, &h);
        CHECK(h != NULL, "launcher home created");
        if (!h) continue;

        CHECK(lv_obj_is_valid(cos_framework_home_root(h)), "launcher home root valid");

        /* Status bar (resolved geometry) must fit inside the safe area. */
        float sx, sy, sw, sh;
        cos_framework_home_statusbar_rect(h, &sx, &sy, &sw, &sh);
        float scx = sx + sw * 0.5f, scy = sy + sh * 0.5f;
        float sr = sqrtf((sw * 0.5f) * (sw * 0.5f) + (sh * 0.5f) * (sh * 0.5f));
        CHECK(cos_display_profile_is_inside(&p, scx, scy, sr),
              "launcher status bar inside safe area");

        /* On round screens the arc tops out near the bar; the topmost visible
         * item must clear the status bar so app text never hides behind it
         * ("字体重叠" regression guard). */
        if (h->is_circle && h->arc) {
            float min_y = 1e9f;
            for (int i = 0; i < cos_arclist_view_count(h->arc); i++) {
                const cos_arclist_item_t *it = &h->arc->al.items[i];
                if (!it->visible) continue;
                if (it->y < min_y) min_y = it->y;
            }
            CHECK(min_y >= sy + sh * 0.5f, "launcher top arc item clears status bar");
        }

        /* Drive a flick + inertia, then verify items stay contained. */
        cos_framework_home_drag(h, -50.0f);
        cos_framework_home_release(h);
        for (int f = 0; f < 90; f++) cos_framework_home_step(h, 16.7f);

        int vis_ok = 1, cnt = 0;
        if (h->is_circle && h->arc) {
            cnt = cos_arclist_view_count(h->arc);
            for (int i = 0; i < cnt; i++) {
                const cos_arclist_item_t *it = &h->arc->al.items[i];
                if (!it->visible) continue;
                float r = (h->arc->card_px * it->scale) * 0.5f;
                if (!cos_display_profile_is_inside(&p, it->x, it->y, r)) {
                    vis_ok = 0;
                }
            }
        } else if (h->grid) {
            cnt = cos_layout_view_count(h->grid);
            for (int i = 0; i < cnt; i++) {
                cos_widget_t *w = &h->grid->widgets[i];
                float cx = w->x + w->w * 0.5f, cy = w->y + w->h * 0.5f;
                float r = (w->w > w->h ? w->w : w->h) * 0.5f;
                if (!cos_display_profile_is_inside(&p, cx, cy, r)) {
                    vis_ok = 0;
                }
            }
        }
        CHECK(cnt == n, "launcher app count matches");
        CHECK(vis_ok, "launcher all rendered items inside safe area");
        /* The card-tap -> launch callback must be wired (so tapping launches). */
        CHECK((h->is_circle && h->arc && h->arc->on_select) ||
              (!h->is_circle && h->grid && h->grid->on_select),
              "launcher on_select wired to launch");

        /* Exercise the live-rebuild path (install/uninstall subscription): the
         * real Launcher rebuilds its grid on app install/uninstall. */
        cos_launcher_rebuild();
        cos_framework_home_t *h2 = cos_launcher_get_home();
        CHECK(h2 != NULL, "launcher rebuild on install event");
        if (h2)
        {
            CHECK(lv_obj_is_valid(cos_framework_home_root(h2)),
                  "launcher rebuilt root valid");
            int vis2 = 1;
            if (h2->is_circle && h2->arc)
            {
                int c = cos_arclist_view_count(h2->arc);
                for (int i = 0; i < c; i++)
                {
                    const cos_arclist_item_t *it = &h2->arc->al.items[i];
                    if (!it->visible) continue;
                    float r = (h2->arc->card_px * it->scale) * 0.5f;
                    if (!cos_display_profile_is_inside(&p, it->x, it->y, r))
                        vis2 = 0;
                }
            }
            else if (h2->grid)
            {
                int c = cos_layout_view_count(h2->grid);
                for (int i = 0; i < c; i++)
                {
                    cos_widget_t *w = &h2->grid->widgets[i];
                    float cx = w->x + w->w * 0.5f, cy = w->y + w->h * 0.5f;
                    float r = (w->w > w->h ? w->w : w->h) * 0.5f;
                    if (!cos_display_profile_is_inside(&p, cx, cy, r))
                        vis2 = 0;
                }
            }
            CHECK(vis2, "launcher rebuilt items inside safe area");
        }

        lv_anim_del_all();
        cos_framework_home_t *cur = h2 ? h2 : h;
        if (cur) cos_framework_home_destroy(cur);
        lv_obj_clean(holder);
    }

    /* ---- Regression guard: dangling display-profile pointer (#ball-top-left).
     * The real Launcher (_launcher_on_enter) builds the Home from a STACK-LOCAL
     * profile and returns, so the local is freed BEFORE the 16ms step timer
     * re-lays-out the ArcList. If the Home kept only a POINTER to that local,
     * cos_arclist_layout() reads freed stack memory and clumps every card at
     * (0,0) — the "ball in the top-left, only a sliver visible" symptom, and
     * taps hit the wrong (overlapping) card so apps never open. Building inside
     * a nested block (local destroyed on scope exit) and THEN stepping
     * reproduces that exact scenario. After the fix the Home owns the profile
     * BY VALUE, so item centers stay near screen center (120,120), never (0,0). */
    {
        cos_framework_home_t *hr = NULL;
        {
            cos_display_profile_t lp = cos_display_profiles_get(COS_PROFILE_240C);
            int nb = cos_launcher_build_home(holder, &lp, &hr);
            (void)nb;
        } /* lp destroyed here — mirrors _launcher_on_enter returning */

        for (int f = 0; f < 30; f++) cos_framework_home_step(hr, 16.7f);

        float cx = 0.0f, cy = 0.0f;
        cos_framework_home_item_center(hr, 0, &cx, &cy);
        CHECK(cx > 60.0f && cy > 60.0f,
              "regression: ArcList item 0 stays near screen center after profile local out of scope");
        CHECK(cx < 180.0f && cy < 180.0f,
              "regression: ArcList item 0 not clumped at origin (dangling-profile fix)");

        lv_anim_del_all();
        cos_framework_home_destroy(hr);
        lv_obj_clean(holder);
    }

    lv_obj_del(holder);
    printf("\n[summary] pass=%d fail=%d\n", pass, fail);
    printf("=== Launcher migration test done ===\n");
    return (fail == 0) ? 0 : 1;
#undef CHECK
}


/* ------------------------------------------------------------------ */
/* Real-screen snapshot: navigate the LIVE app (watchface -> app list  */
/* -> control center) the same way the user does, snapshot each real  */
/* page, and dump PPMs. This is the only way to actually SEE the      */
/* pixel-level "black dots top / white dots bottom / screen flash"     */
/* bugs the user reports — headless event-loop tests are blind to     */
/* pixels.                                                             */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Forward decls for the tap-injection debug inside screen snapshot     */
/* (the _hg_indev_* globals + read cb are defined later in the file).  */
static int _hg_indev_idx;
static int _hg_indev_start_x;
static int _hg_indev_end_x;
static int _hg_indev_y;
static int _hg_indev_start_y;
static int _hg_indev_end_y;
static int _hg_indev_has_y;
static int _hg_indev_steps;
static void _hg_indev_swipe_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

static int sim_run_screen_snapshot(void)
{
    printf("=== Real-screen snapshot (watchface + app page + control center + timer + alarm) ===\n");
    fflush(stdout);
    /* The SDL display driver overrides LVGL's tick source, so lv_tick_inc()
     * is ignored and event timing (press/click/gesture) never advances in
     * this headless self-test. Restore the default tick like the gesture
     * tests do, so the tap-injection can actually deliver press/click. */
    lv_tick_set_cb(NULL);
    const int w = SIM_HOR_RES, h = SIM_VER_RES;
    static const char *names[] = {"watchface", "app_page", "control_center", "timer_app", "alarm_app"};
    for (int snap = 0; snap < 5; snap++) {
        if (snap == 1) {
            printf("[snap] -> cos_app_list_enter()\n"); fflush(stdout);
            cos_app_list_enter();
        } else if (snap == 2) {
            printf("[snap] -> back to watchface, then open control center\n"); fflush(stdout);
            cos_activity_back_to_watchface();
            cos_control_center_show();
            cos_control_panel_slide_change();
        } else if (snap == 3) {
            printf("[snap] -> launch com.cantomk6.timer (JS eapk app)\n"); fflush(stdout);
            cos_control_center_hide();
            cos_activity_back_to_watchface();
            for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); }
            cos_app_launch_immediately("com.cantomk6.timer");
        } else if (snap == 4) {
            printf("[snap] -> launch com.cantomk6.alarm (JS eapk app)\n"); fflush(stdout);
            cos_activity_back_to_watchface();
            for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); }
            cos_app_launch_immediately("com.cantomk6.alarm");
        }
        /* Let animations settle */
        for (int k = 0; k < 80; k++) { lv_tick_inc(20); lv_timer_handler(); }
        lv_refr_now(lv_display_get_default());
        /* Let animations settle */
        for (int k = 0; k < 80; k++) { lv_tick_inc(20); lv_timer_handler(); }
        lv_refr_now(lv_display_get_default());
        /* Use SDL_RenderReadPixels (real presented pixels, gradients OK)
         * instead of lv_snapshot_take_to_draw_buf which corrupts gradient +
         * transform rendering (color stripe artifacts on the right half). */
        char ppm[64];
        snprintf(ppm, sizeof(ppm), "screen_%s.ppm", names[snap]);
        FILE *fp = fopen(ppm, "wb");
        if (fp) {
            SDL_Renderer *r = (SDL_Renderer *)lv_sdl_window_get_renderer(lv_display_get_default());
            int rpitch = w * 4;
            uint8_t *argb = (uint8_t *)malloc((size_t)rpitch * h);
            if (r && argb && SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, argb, rpitch) == 0) {
                fprintf(fp, "P6\n%d %d\n255\n", w, h);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        uint8_t *p = argb + (size_t)y * rpitch + (size_t)x * 4;
                        uint8_t pix[3] = { p[2], p[1], p[0] };
                        fwrite(pix, 1, 3, fp);
                    }
                printf("[snap] wrote %s (SDL)\n", ppm);
            } else {
                printf("[FAIL] cannot grab %s\n", ppm);
            }
            free(argb);
            fclose(fp);
        } else {
            printf("[FAIL] cannot open %s\n", ppm);
        }
    }
    printf("=== screen snapshot done ===\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Round 4 diagnostic: read the ACTUAL SDL texture the user sees (via  */
/* lv_sdl_window_read_texture) and print per-row luminance so a top/   */
/* bottom split (the "top black, bottom white" report) is numerically  */
/* visible. Runs against the real render path (watchface, then app     */
/* page, then control center). Headless-safe under SDL dummy.          */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Render-check: capture the ACTUAL presented pixels (overlays included)*/
/* via SDL_RenderReadPixels and dump PPM files so we can SEE what the   */
/* user sees, including the chrome/control-center overlays.            */
/* ------------------------------------------------------------------ */
static void sim_grab_ppm(const char *name)
{
    const int w = SIM_HOR_RES, h = SIM_VER_RES;
    SDL_Renderer *r = (SDL_Renderer *)lv_sdl_window_get_renderer(lv_display_get_default());
    int rpitch = w * 4;
    uint8_t *argb = (uint8_t *)malloc((size_t)rpitch * h);
    if (!r || !argb) { printf("[FAIL] %s alloc/renderer\n", name); free(argb); return; }
    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, argb, rpitch) != 0) {
        printf("[FAIL] %s RenderReadPixels: %s\n", name, SDL_GetError()); free(argb); return;
    }
    FILE *f = fopen(name, "wb");
    if (!f) { printf("[FAIL] %s fopen\n", name); free(argb); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t *p = argb + (size_t)y * rpitch + (size_t)x * 4;
            uint8_t pix[3] = { p[2], p[1], p[0] };   /* ARGB8888 -> RGB */
            fwrite(pix, 1, 3, f);
        }
    }
    fclose(f);
    /* per-half average to spot a top/bottom split numerically */
    uint64_t tr = 0, tg = 0, tb = 0, br = 0, bg = 0, bb = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = argb + (size_t)y * rpitch + (size_t)x * 4;
            if (y < h / 2) { tr += p[2]; tg += p[1]; tb += p[0]; }
            else           { br += p[2]; bg += p[1]; bb += p[0]; }
        }
    uint64_t n = (uint64_t)w * (uint64_t)(h / 2);
    printf("[%s] TOP avg rgb=(%llu,%llu,%llu)  BOT avg rgb=(%llu,%llu,%llu)\n", name,
           (unsigned long long)(tr / n), (unsigned long long)(tg / n), (unsigned long long)(tb / n),
           (unsigned long long)(br / n), (unsigned long long)(bg / n), (unsigned long long)(bb / n));
    fflush(stdout);
    free(argb);
}

static int sim_run_render_check(void)
{
    printf("=== Render-check: dump presented pixels (incl. overlays) to PPM ===\n");
    fflush(stdout);

    {
        int rw = 0, rh = 0;
        SDL_Renderer *r = (SDL_Renderer *)lv_sdl_window_get_renderer(lv_display_get_default());
        if (r) SDL_GetRendererOutputSize(r, &rw, &rh);
        printf("[check] renderer output size = %d x %d  (display = %d x %d)\n",
               rw, rh, SIM_HOR_RES, SIM_VER_RES);
        fflush(stdout);
    }

    /* boot a few frames */
    for (int k = 0; k < 60; k++) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(lv_display_get_default());
    lv_timer_handler();

    sim_grab_ppm("chk_watchface_start.ppm");           /* startup: watchface, CC should be CLOSED */

    /* Simulate pressing the 'C' hotkey: cos_control_center_show() + slide_change() */
    printf("[check] -> simulate 'C' key (show + slide_change)\n"); fflush(stdout);
    cos_control_center_show();
    cos_control_panel_slide_change();
    for (int k = 0; k < 150; k++) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(lv_display_get_default());
    lv_timer_handler();
    sim_grab_ppm("chk_watchface_afterC.ppm");          /* CC should now be OPEN */

    /* Press 'C' again -> should CLOSE */
    printf("[check] -> simulate 'C' key again (close)\n"); fflush(stdout);
    cos_control_center_show();
    cos_control_panel_slide_change();
    for (int k = 0; k < 150; k++) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(lv_display_get_default());
    lv_timer_handler();
    sim_grab_ppm("chk_watchface_afterC2.ppm");         /* CC should be CLOSED again */

    printf("[check] -> cos_app_list_enter()\n"); fflush(stdout);
    cos_app_list_enter();
    for (int k = 0; k < 150; k++) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(lv_display_get_default());
    lv_timer_handler();
    sim_grab_ppm("chk_apppage_ccclosed.ppm");          /* app page + CC closed */

    printf("=== render-check done ===\n");
    return 0;
}

static int sim_run_pixel_probe(void)
{
    printf("=== Pixel probe: read back the REAL SDL texture (what the user sees) ===\n");
    fflush(stdout);
    const int w = SIM_HOR_RES, h = SIM_VER_RES;
    uint8_t *pix = (uint8_t *)malloc((size_t)w * 2 * h);
    if (!pix) { printf("[FAIL] alloc\n"); return 1; }

    static const char *names[] = {"watchface", "app_page", "control_center"};
    for (int snap = 0; snap < 3; snap++) {
        if (snap == 1) {
            printf("[probe] -> cos_app_list_enter()\n"); fflush(stdout);
            cos_app_list_enter();
        } else if (snap == 2) {
            printf("[probe] -> back to watchface, then open control center\n"); fflush(stdout);
            cos_activity_back_to_watchface();
            cos_control_center_show();
            cos_control_panel_slide_change();
        }
        for (int k = 0; k < 90; k++) { lv_tick_inc(20); lv_timer_handler(); }
        lv_refr_now(lv_display_get_default());
        /* force one more flush so the texture is current */
        lv_timer_handler();

        /* Read the ACTUAL presented pixels. Prefer the uploaded texture; if
         * that is not lockable (e.g. dummy/software renderer), fall back to
         * reading the renderer's present buffer in ARGB8888. */
        int pitch = lv_sdl_window_read_texture(lv_display_get_default(), pix, (uint32_t)w * 2 * h);
        int use_argb = 0;
        if (pitch < 0) {
            SDL_Renderer *r = (SDL_Renderer *)lv_sdl_window_get_renderer(lv_display_get_default());
            int rpitch = w * 4;
            uint8_t *argb = (uint8_t *)malloc((size_t)rpitch * h);
            if (r && argb && SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, argb, rpitch) == 0) {
                /* repack ARGB8888 -> RGB565 in `pix` so the decode below is shared */
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        uint8_t *p = argb + (size_t)y * rpitch + (size_t)x * 4;
                        uint8_t R = p[2] >> 3, G = p[1] >> 2, B = p[0] >> 3;
                        uint16_t v = (uint16_t)((R << 11) | (G << 5) | B);
                        *(uint16_t *)(pix + (size_t)y * (w * 2) + (size_t)x * 2) = v;
                    }
                }
                pitch = w * 2;
                use_argb = 1;
            } else {
                printf("[FAIL] cannot read pixels (texture rc=%d, render-read=%d)\n", pitch,
                       r ? SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, argb, rpitch) : -99);
                free(argb);
                free(pix);
                return 1;
            }
            free(argb);
        }
        printf("[probe %s] readback pitch=%d (expected %d) via_%s\n", names[snap], pitch, w * 2,
               use_argb ? "renderer" : "texture");

        /* Per-row average luminance (decode RGB565). */
        for (int y = 0; y < h; y++) {
            uint32_t sum = 0;
            for (int x = 0; x < w; x++) {
                uint16_t v = *(const uint16_t *)(pix + (size_t)y * pitch + (size_t)x * 2);
                int r = (v >> 11) & 0x1f;
                int g = (v >> 5)  & 0x3f;
                int b =  v        & 0x1f;
                sum += (unsigned)(r * 2 + g + b);  /* rough luminance 0..255 */
            }
            int lum = (int)(sum / w);
            if (y % 20 == 0 || y == h - 1) {
                printf("  row %3d: lum=%3d\n", y, lum);
            }
        }
        printf("[probe %s] done\n", names[snap]);
    }
    free(pix);
    printf("=== pixel probe done ===\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* UI-JS bridge self-test (Route A verification).                      */
/* Runs an in-memory JavaScript program that drives the cos.ui.* Jerry- */
/* Script bridge end-to-end: it builds the adaptive Home on all 4      */
/* display profiles, asserts (via resolved geometry, never            */
/* lv_obj_get_*) that the status bar + every item center stay inside   */
/* the safe area, drives a flick + inertia, and verifies the           */
/* JS->C->JS onSelect round-trip fires synchronously. Any failed       */
/* assertion throws, which makes spm_app_run fail, so the process      */
/* exit code reflects pass/fail for CI. Headless-safe (SDL dummy).     */
/* ------------------------------------------------------------------ */
static int sim_run_ui_js_test(void)
{
    printf("=== CantoMk6OS UI-JS (cos.ui bridge) self-test ===\n");
    fflush(stdout);

    /* Single in-memory program covers all 4 profiles, so only one realm
     * is created and torn down. JS uses single-quoted strings so the C
     * literal below needs no double-quote escaping. */
    static const char *UI_JS_TEST_SRC =
        "(function(){"
        "  function log(s){ try { cos.console.log(s); } catch(e){} }"
        "  var ui = cos.ui;"
        "  var pass = 0, fail = 0, selected = -1;"
        "  function check(c, msg){ if (c){ pass++; log('[OK]   ' + msg); } else { fail++; log('[FAIL] ' + msg); } }"
        "  var apps = ['Clock','Gallery','Files','Settings','Flash','Weather','Music'];"
        "  var view = lv.screen.active();"
        "  for (var id = 0; id < ui.PROFILE_COUNT; id++){"
        "    var p = ui.profileForId(id);"
        "    var home = null;"
        "    try {"
        "      home = ui.home(view, p, apps, function(idx){ selected = idx; });"
        "      check(home != null && home !== undefined, 'profile#' + id + ' home created');"
        "      var sb = ui.homeStatusbarRect(home);"
        "      var sbCx = sb.x + sb.w * 0.5, sbCy = sb.y + sb.h * 0.5;"
        "      var sbR = Math.sqrt((sb.w * 0.5) * (sb.w * 0.5) + (sb.h * 0.5) * (sb.h * 0.5));"
        "      check(ui.profileIsInside(p, sbCx, sbCy, sbR), 'profile#' + id + ' status bar inside safe area');"
        "      var cnt = ui.homeCount(home);"
        "      check(cnt === apps.length, 'profile#' + id + ' app count == ' + apps.length + ' (got ' + cnt + ')');"
        "      var allIn = true;"
        "      for (var i = 0; i < cnt; i++){"
        "        var c = ui.homeItemCenter(home, i);"
        "        if (c === undefined){ allIn = false; break; }"
        "        if (!ui.profileIsInside(p, c.x, c.y, 8.0)){ allIn = false; }"
        "      }"
        "      check(allIn, 'profile#' + id + ' all item centers inside safe area');"
        "      ui.homeDrag(home, -50.0); ui.homeRelease(home);"
        "      for (var f = 0; f < 90; f++){ ui.homeStep(home, 16.7); }"
        "      var allIn2 = true;"
        "      for (var j = 0; j < cnt; j++){"
        "        var c2 = ui.homeItemCenter(home, j);"
        "        if (c2 === undefined){ allIn2 = false; break; }"
        "        if (!ui.profileIsInside(p, c2.x, c2.y, 8.0)){ allIn2 = false; }"
        "      }"
        "      check(allIn2, 'profile#' + id + ' item centers inside safe area after scroll');"
        "      selected = -1; ui.homeSelect(home, 2);"
        "      check(selected === 2, 'profile#' + id + ' onSelect fires JS callback (idx=2)');"
        "      ui.homeDestroy(home);"
        "    } catch(e) {"
        "      fail++; log('[FAIL] profile#' + id + ' exception: ' + e);"
        "    } finally {"
        "      ui.profileDestroy(p);"
        "    }"
        "  }"
        "  log('[summary] pass=' + pass + ' fail=' + fail);"
        "  if (fail > 0){ throw new Error('ui-js-test: ' + fail + ' assertion(s) failed'); }"
        "})();";

    script_pkg_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    pkg.type = SCRIPT_TYPE_APPLICATION;
    pkg.id = "sys.ui-js-test";
    pkg.name = "UI-JS bridge self-test";
    pkg.script_str = UI_JS_TEST_SRC;

    cos_result_t r = spm_app_run(&pkg);
    int exit_code = (r == COS_OK) ? 0 : 1;

    if (r != COS_OK)
    {
        const spm_error_t *err = spm_get_last_error();
        if (err && err->error_info[0])
            printf("[FAIL] ui-js-test script error: %s\n", err->error_info);
    }

    /* Tear down the (possibly still-active) program and free the realm. */
    spm_app_stop();

    printf("\n[summary] %s\n",
           (exit_code == 0) ? "ALL UI-JS ASSERTIONS PASSED" : "UI-JS TEST FAILED");
    printf("=== UI-JS self-test done ===\n");
    return exit_code;
}

/* ----------------------------------------------------------------------------
 * Notes/Draw platform probe (--notes-probe)
 * In-memory JS exercising the new bridges: cos.fs.write (string + Uint8Array),
 * cos.fs.read (binary-safe Uint8Array), cos.ime.open (no-throw + input page).
 * -------------------------------------------------------------------------- */
static const char NOTES_PROBE_SRC[] =
    "function log(m) { cos.console.log(m); }\n"
    "var pass = 0, fail = 0;\n"
    "function check(c, m) { if (c) { pass++; log('[OK] ' + m); } else { fail++; log('[FAIL] ' + m); } }\n"
    "/* JS UTF-8 decoder (no TextDecoder in this JerryScript build) */\n"
    "function utf8Decode(bytes) {\n"
    "  var out = [], i = 0, n = bytes.length;\n"
    "  while (i < n) {\n"
    "    var b = bytes[i++];\n"
    "    if (b < 0x80) out.push(String.fromCharCode(b));\n"
    "    else if (b < 0xE0) out.push(String.fromCharCode(((b & 0x1F) << 6) | (bytes[i++] & 0x3F)));\n"
    "    else if (b < 0xF0) out.push(String.fromCharCode(((b & 0x0F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F)));\n"
    "    else { var cp = ((b & 0x07) << 18) | ((bytes[i++] & 0x3F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F); out.push(String.fromCharCode(cp)); }\n"
    "  }\n"
    "  return out.join('');\n"
    "}\n"
    "/* 1. fs.write string */\n"
    "var ok1 = cos.fs.write('/sdcard/Notes/probe.txt', '你好CantoMk6OS 123');\n"
    "check(ok1 === true, 'fs.write string returns true');\n"
    "/* 2. fs.read -> Uint8Array -> utf8Decode round-trip */\n"
    "var d = cos.fs.read('/sdcard/Notes/probe.txt');\n"
    "check(d && typeof d.length === 'number' && d.length === 20,\n"
    "      'fs.read returns byte array (len ' + (d ? d.length : -1) + ')');\n"
    "var s = utf8Decode(d);\n"
    "check(s === '你好CantoMk6OS 123', 'fs.read utf8 round-trip = [' + s + ']');\n"
    "/* 3. fs.write Uint8Array (binary, NUL + high bytes preserved) */\n"
    "var bin = new Uint8Array([0x45, 0x4C, 0x44, 0x52, 0x57, 0x31, 0x00, 0xFF, 0xFE]);\n"
    "var ok2 = cos.fs.write('/sdcard/Drawings/probe.edrw', bin);\n"
    "check(ok2 === true, 'fs.write Uint8Array returns true');\n"
    "var d2 = cos.fs.read('/sdcard/Drawings/probe.edrw');\n"
    "check(d2 && d2.length === 9 && d2[6] === 0x00 && d2[7] === 0xFF && d2[8] === 0xFE,\n"
    "      'fs binary round-trip 9 bytes (NUL+FF preserved)');\n"
    "/* 4. ime.open no-throw */\n"
    "var imeThrew = false;\n"
    "try { cos.ime.open(function (t) { log('[ime] cb got [' + t + ']'); }); }\n"
    "catch (e) { imeThrew = true; log('[FAIL] ime.open threw: ' + e); }\n"
    "check(!imeThrew, 'ime.open called without throwing');\n"
    "log('[summary] pass=' + pass + ' fail=' + fail);\n";

static int sim_run_notes_probe(void)
{
    printf("=== Notes/Draw platform probe (--notes-probe) ===\n");
    /* ensure target dirs exist */
    cos_storage_mkdir_if_not_exist("/sdcard/Notes");
    cos_storage_mkdir_if_not_exist("/sdcard/Drawings");
    /* remove leftover probe files so size checks are deterministic */
    cos_fs_remove("/sdcard/Notes/probe.txt");
    cos_fs_remove("/sdcard/Drawings/probe.edrw");

    script_pkg_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    pkg.type = SCRIPT_TYPE_APPLICATION;
    pkg.id = "sys.notes-probe";
    pkg.name = "Notes platform probe";
    pkg.script_str = NOTES_PROBE_SRC;

    cos_result_t r = spm_app_run(&pkg);
    int exit_code = (r == COS_OK) ? 0 : 1;
    if (r != COS_OK)
    {
        const spm_error_t *err = spm_get_last_error();
        if (err && err->error_info[0])
            printf("[FAIL] notes-probe script error: %s\n", err->error_info);
    }
    spm_app_stop();

    /* ime.open should have opened the input page: verify an activity switch */
    lv_obj_t *view = cos_activity_get_view(cos_activity_get_current());
    printf("[probe] post-ime activity view=%p\n", (void *)view);

    printf("\n=== notes probe done (exit=%d) ===\n", exit_code);
    return exit_code;
}

/* Return the obj that directly contains a label whose text == needle. */
static lv_obj_t *_find_parent_with_label(lv_obj_t *p, const char *needle)
{
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++)
    {
        lv_obj_t *c = lv_obj_get_child(p, i);
        if (lv_obj_check_type(c, &lv_label_class))
        {
            const char *t = lv_label_get_text(c);
            if (t && strcmp(t, needle) == 0)
                return p;
        }
        lv_obj_t *r = _find_parent_with_label(c, needle);
        if (r) return r;
    }
    return NULL;
}

/* Breach Protocol app probe (--breach-launch): headless boot + structure.
 * Verifies launch succeeds (no JS runtime error), Boot page builds, the
 * "INITIATING..." label is present, and the boot widgets / page containers exist.
 * Boot animations are timer-driven; we pump the loop so they run. */
static int sim_run_breach_launch(void)
{
    printf("=== Breach Protocol app probe (headless) ===\n");
    lv_tick_set_cb(NULL);   // 还原内部 tick 计数器，使 lv_tick_inc 生效（否则 SDL 实时钟忽略增量，计时器不触发）
    cos_result_t la = cos_app_launch_immediately("com.cantomk6.breach");
    printf("[probe] launch ret=%d (COS_OK=%d)\n", (int)la, (int)COS_OK);
    int fail = 0;
    if (la != COS_OK) { printf("[FAIL] launch com.cantomk6.breach failed\n"); fail++; }

    /* pump frames so JS timers (typewriter/progress/countdown) advance and
     * Boot->Hack transition (fadeTo) fires in the REAL engine.
     * 1200 frames * 16ms = 19.2s; Boot ~5s 完成，余量充足。 */
    for (int f = 0; f < 1200; f++)
    {
        lv_tick_inc(16);
        lv_timer_handler();
        cos_dispatch_tick();
    }
    lv_obj_t *view = cos_activity_get_view(cos_activity_get_current());
    if (!view) { printf("[FAIL] breach view is NULL\n"); return 1; }

    /* Boot 静态标签 INITIATING... 必须存在（构建期）；其所在页 = pageBoot */
    lv_obj_t *initLabel = _find_parent_with_label(view, "INITIATING...");
    if (!initLabel) { printf("[FAIL] 'INITIATING...' label missing\n"); fail++; }
    else printf("[probe] Boot 'INITIATING...' present\n");

    /* 关键转场判据（不依赖打字机异步文本 / 不依赖页内标题）：
     * 冻结根因是 Boot 倒计时回调里 fadeTo 未定义 → 引擎 state=3 全拒收 →
     *   pageBoot 永不 HIDDEN 且 pageHack 永不显示。
     * pageBoot 由 INITIATING 标签定位；pageHack = root 下"非 pageBoot 且可见"的页
     * （页内左标题已删，状态栏承载标题；改用排除法定位 pageHack）。 */
    int bootDone = 0, hackShown = 0;
    lv_obj_t *pageBoot = initLabel;   // initLabel 即 pageBoot（INITIATING 的父容器）
    if (pageBoot && lv_obj_has_flag(pageBoot, LV_OBJ_FLAG_HIDDEN)) bootDone = 1;
    lv_obj_t *root = lv_obj_get_child(view, 0);
    if (root) {
        uint32_t nc = lv_obj_get_child_cnt(root);
        for (uint32_t i = 0; i < nc; i++) {
            lv_obj_t *ch = lv_obj_get_child(root, i);
            if (ch == pageBoot) continue;                          // 跳过 pageBoot
            if (lv_obj_has_flag(ch, LV_OBJ_FLAG_HIDDEN)) continue; // 跳过 pageResult / fadeOverlay（均 HIDDEN）
            hackShown = 1;   // 唯一可见的非 boot 页 = pageHack
            break;
        }
    }
    if (!bootDone) { printf("[FAIL] Boot page NOT hidden after pump -> Boot did not finish (freeze risk)\n"); fail++; }
    else printf("[probe] Boot completed (pageBoot hidden)\n");
    if (!hackShown) { printf("[FAIL] Hack page still HIDDEN after pump -> fadeTo did NOT fire (freeze risk)\n"); fail++; }
    else printf("[probe] Boot->Hack transition fired (pageHack visible)\n");

    /* object account：启动期没有崩溃（root 子对象数量合理非零） */
    if (!root) { printf("[FAIL] app root (R.root) missing\n"); fail++; }
    else
    {
        uint32_t rc = lv_obj_get_child_cnt(root);
        printf("[probe] R.root direct children=%u (expect 4: 3 pages + fadeOverlay; grid 已删；页内标题已删)\n", rc);
        if (rc < 4) { printf("[FAIL] too few objects, UI likely crashed\n"); fail++; }
    }

    /* 稳定性：再 pump 250 帧，让 Hack 页 gameTimer 真实 tick + refreshAll 在真实 LVGL 渲染 */
    for (int f = 0; f < 250; f++) { lv_tick_inc(16); lv_timer_handler(); cos_dispatch_tick(); }
    printf("[probe] survived 1450 frames (Boot->Hack real render) without crash\n");

    printf("[summary] breach-launch pass=%d fail=%d\n", (fail == 0) ? 1 : 0, fail);
    return (fail == 0) ? 0 : 1;
}

/* Breach 矩阵格定位器：矩阵格由 JS 对象池创建（无名字），用
 * "可见 + CLICKABLE + lv_obj + 尺寸 30x18"（等级 B 默认 4x4，layoutFor 对
 * 3x3..5x5 均产出 30x18）定位第一个矩阵格。取其绝对中心作为真实 indev
 * 点击坐标，从而不依赖 view 是否偏移到 (0,0)。 */
static lv_obj_t *_breach_find_matrix_cell(lv_obj_t *parent)
{
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); i++)
    {
        lv_obj_t *c = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(c, &lv_obj_class) &&
            !lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) &&
            lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE) &&
            lv_obj_get_width(c) == 30 && lv_obj_get_height(c) == 18)
            return c;
        lv_obj_t *r = _breach_find_matrix_cell(c);
        if (r) return r;
    }
    return NULL;
}

/* Breach Protocol Result-page probe (--breach-result): headless.
 *
 * 真机语义：倒计时由「第一次点击矩阵格」启动（main.js `doSelect` 里
 * countdownStarted=true 解除 gameTimer 门控）。故本探针先用真实 LVGL
 * pointer indev（lv_indev_read 的 PRESS→RELEASE 路径）点一次矩阵格，再以
 * 虚拟时间快进让 1s 计时器走完：一击后若全部目标序列已不可能完成 →
 * 立即 endGame(false)（BUFFER FULL）；否则 60s 到点 endGame(true)（TIMED OUT）。
 * 两条路径都满足「solved=0 → isSuccess=false → 结果页显示 BREACH FAILED」，
 * 故断言只锚定与路径无关的事实：结果页可见 + 失败标题 + 终止动画行 + 重试
 * 提示（app 用可点击终端内的 "TAP TO RETRY"，非独立 RETRY 按钮），再 pump
 * 若干帧确认 Glitch/打字机计时器在活动结果页上不崩引擎。 */
static int sim_run_breach_result(void)
{
    printf("=== Breach Protocol Result-page probe (headless) ===\n");
    lv_tick_set_cb(NULL);
    cos_result_t la = cos_app_launch_immediately("com.cantomk6.breach");
    printf("[probe] launch ret=%d (COS_OK=%d)\n", (int)la, (int)COS_OK);
    int fail = 0;
    if (la != COS_OK) { printf("[FAIL] launch com.cantomk6.breach failed\n"); return 1; }

    /* Boot 打字机(50ms*15) + 150ms fade -> Hack 页；快进 2.4s 充足。 */
    for (int f = 0; f < 150; f++) { lv_tick_inc(16); lv_timer_handler(); cos_dispatch_tick(); }

    lv_obj_t *view = cos_activity_get_view(cos_activity_get_current());
    if (!view) { printf("[FAIL] breach view is NULL\n"); return 1; }

    /* 真实点击一次矩阵格，解除倒计时门控。 */
    lv_indev_t *indev = NULL;
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) { indev = i; break; }
    if (!indev) { printf("[FAIL] no pointer indev found\n"); return 1; }

    lv_obj_t *cell = _breach_find_matrix_cell(view);
    if (!cell) { printf("[FAIL] matrix cell not found (layout not applied?)\n"); return 1; }
    lv_area_t ca; lv_obj_get_coords(cell, &ca);
    int cx = (ca.x1 + ca.x2) / 2, cy = (ca.y1 + ca.y2) / 2;
    printf("[probe] matrix cell abs center=(%d,%d)\n", cx, cy);
    /* 选一个真正落在该格上的点：高亮扫描线(scanLine, 10px 高、覆盖整行、且
     * lv_obj 默认可点击)会盖住格子竖直中心并吃掉按下 —— 这是 app 侧行为，
     * 探针不修改它；改为在格内试探几个角落，用 lv_indev_search_obj 取回
     * 第一个命中的就是该格本身的点（避开扫描线带）。 */
    {
        lv_display_t *disp = lv_display_get_default();
        int cand[5][2] = {
            { ca.x1 + 2, ca.y1 + 2 }, { ca.x2 - 2, ca.y1 + 2 },
            { ca.x1 + 2, ca.y2 - 2 }, { ca.x2 - 2, ca.y2 - 2 }, { cx, cy } };
        int hit_ok = 0;
        for (int i = 0; i < 5; i++) {
            lv_point_t pt = { cand[i][0], cand[i][1] };
            if (lv_indev_search_obj(lv_display_get_screen_active(disp), &pt) == cell) {
                cx = cand[i][0]; cy = cand[i][1]; hit_ok = 1; break;
            }
        }
        printf("[probe] tap point=(%d,%d) hits_cell=%d\n", cx, cy, hit_ok);
    }

    /* 单击 = 同点 PRESS→RELEASE（start==end，无滚动/无手势）。 */
    _hg_indev_idx = 0; _hg_indev_has_y = 0;
    _hg_indev_start_x = cx; _hg_indev_end_x = cx; _hg_indev_y = cy;
    _hg_indev_steps = 0;   /* 退化为 1 步：press,press,release,release... */
    lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);
    for (int k = 0; k < 8; k++) { lv_tick_inc(16); lv_indev_read(indev); }
    for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }

    /* 60s 倒计时（1s/tick，16ms/帧）。8000 帧 = 128s 虚拟时间，余量充足。 */
    for (int f = 0; f < 8000; f++) { lv_tick_inc(16); lv_timer_handler(); cos_dispatch_tick(); }

    view = cos_activity_get_view(cos_activity_get_current());
    if (!view) { printf("[FAIL] breach view is NULL after game\n"); return 1; }

    lv_obj_t *failStatus = _find_parent_with_label(view, "BREACH FAILED");
    if (!failStatus) { printf("[FAIL] 'BREACH FAILED' status label missing (endGame did not fire)\n"); fail++; }
    else if (lv_obj_has_flag(failStatus, LV_OBJ_FLAG_HIDDEN)) { printf("[FAIL] Result page (pageResult) still HIDDEN\n"); fail++; }
    else printf("[probe] Result page visible; status 'BREACH FAILED' correct\n");

    lv_obj_t *termRow = _find_parent_with_label(view, "//DAEMON_1 FAILED");
    if (!termRow) { printf("[FAIL] terminal row '//DAEMON_1 FAILED' missing (end animation did not run)\n"); fail++; }
    else printf("[probe] terminal end-animation row present\n");

    lv_obj_t *retryHint = _find_parent_with_label(view, "TAP TO RETRY");
    if (!retryHint) { printf("[FAIL] 'TAP TO RETRY' hint missing\n"); fail++; }
    else printf("[probe] retry affordance present\n");

    /* 稳定性：Glitch(40ms)/打字机计时器在活动结果页上继续跑 */
    for (int f = 0; f < 1500; f++) { lv_tick_inc(16); lv_timer_handler(); cos_dispatch_tick(); }
    printf("[probe] survived result page + glitch timers without crash\n");

    printf("[summary] breach-result pass=%d fail=%d\n", (fail == 0) ? 1 : 0, fail);
    return (fail == 0) ? 0 : 1;
}

/* ----------------------------------------------------------------------------
 * indev-driven swipe reproduction
 * Exercises the REAL LVGL pointer event path (PRESS -> drag -> RELEASE) so we
 * can catch regressions that the direct cos_watchface_builtin_test_swipe() hook
 * (which calls the navigate logic without going through event delivery) hides.
 * A scripted read_cb feeds a right-swipe; the catcher's PRESSED/RELEASED
 * callbacks must then open the control center.
 * -------------------------------------------------------------------------- */
static int _hg_indev_idx = 0;
static int _hg_indev_start_x = 10;
static int _hg_indev_end_x = 235;
static int _hg_indev_y = 120;
static int _hg_indev_start_y = 0;   /* 垂直 swipe：置 _hg_indev_has_y=1 启用 */
static int _hg_indev_end_y = 0;
static int _hg_indev_has_y = 0;
/* Number of PRESSED move samples between start and end.
 *
 * CRITICAL for fidelity: a real SDL mouse/touch drag delivers ~1-4 px per
 * frame. The original harness used 7 coarse steps (~32 px each) for a 225 px
 * swipe, which HID a real bug: LVGL's own scroll engine takes over once the
 * accumulated drag passes `indev->scroll_limit` (LV_INDEV_DEF_SCROLL_LIMIT,
 * default 10 px) and from then on STOPS delivering LV_EVENT_PRESSING to the
 * object. With 32 px steps the gesture handler already saw a large dx on its
 * first sample, so the test passed while the real window failed. Keep the
 * per-step delta small (<= ~4 px) so the scroll-interception window is
 * actually reproduced. */
static int _hg_indev_steps = 7;
static void _hg_indev_swipe_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int idx = _hg_indev_idx++;
    int steps = (_hg_indev_steps > 0) ? _hg_indev_steps : 1;
    /* 垂直 swipe 时 y 也插值；否则 y 固定（旧行为） */
    int y_now = _hg_indev_y;
    if (_hg_indev_has_y)
        y_now = _hg_indev_start_y + (_hg_indev_end_y - _hg_indev_start_y) * idx / steps;
    if (idx == 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = _hg_indev_start_x;
        data->point.y = y_now;
    } else if (idx <= steps) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = _hg_indev_start_x +
                        (_hg_indev_end_x - _hg_indev_start_x) * idx / steps;
        data->point.y = y_now;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = _hg_indev_end_x;
        data->point.y = y_now;
    }
}

/* ---- Alarm app headless-test helpers (file-scope: C forbids static fn
 * inside a function; used only by the --alarm-test branch) ---- */
static lv_obj_t *_alarm_find_label(lv_obj_t *parent, const char *text)
{
    uint32_t cnt = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < cnt; i++)
    {
        lv_obj_t *c = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(c, &lv_label_class))
        {
            const char *t = lv_label_get_text(c);
            if (t && strcmp(t, text) == 0)
                return c;
        }
        lv_obj_t *r = _alarm_find_label(c, text);
        if (r)
            return r;
    }
    return NULL;
}

static cos_datetime_t _alarm_fake_dt;
static cos_datetime_t _alarm_fake_get_datetime(void) { return _alarm_fake_dt; }
static const cos_dev_time_ops_t _alarm_fake_ops = { .get_datetime = _alarm_fake_get_datetime };

/* JS cos.config persists into the APP's OWN data dir (not the system cfg):
 *   COS_APP_DATA_DIR "<app_id>/config.json"
 * The current multi-alarm app stores its list via
 *   cos.config.setStr("alarms", JSON.stringify(alarms))
 * i.e. the app-scoped config.json holds a STRING under the key "alarms"
 * whose value is itself a JSON array:
 *   { "alarms": "[{\"id\":0,\"h\":7,\"m\":30,\"days\":0,\"rep\":-1,\"on\":true,\"lf\":0}]" }
 * The C cos_config_set_number() writes the SYSTEM cfg.json instead, so the
 * alarm test must write the app-scoped file directly through cJSON (which
 * handles the escaping of the inner JSON string). */
#define ALARM_APP_CFG "com.cantomk6.alarm/config.json"
static void _alarm_write_alarms(const char *alarms_json)
{
    char path[COS_FS_PATH_MAX];
    snprintf(path, sizeof(path), COS_APP_DATA_DIR ALARM_APP_CFG);
    cos_storage_mkdir_if_not_exist(COS_APP_DATA_DIR "com.cantomk6.alarm");
    cos_storage_json_set_string(path, "alarms", alarms_json);
}

/* ------------------------------------------------------------------
 * 硬件输入注入：SDL 事件过滤器
 *
 * LVGL 的 SDL 驱动在内部定时器 sdl_event_handler(5ms) 里自行
 * SDL_PollEvent，再把事件分流给 mouse / keyboard indev。因此主循环
 *   (a) 绝不能再 SDL_PollEvent —— 那会把事件从队列里抢走；
 *   (b) 绝不能直接调用 lv_sdl_mouse_handler / lv_sdl_keyboard_handler
 *       —— 那会二次分发同一事件。
 *
 * 这里注册 SDL_SetEventFilter：它在 SDL_PumpEvents/PollEvent 之内、驱动
 * 消费之前执行，返回 0 即把事件从队列中丢弃。于是模拟器的"表冠"与
 * "侧键"映射到真实固件 API，且被本过滤器独占消费，不会漏进触摸/键盘路径：
 *   滚轮         -> 表冠旋转      cos_crown_encoder_report()
 *   鼠标中键     -> 表冠按下      cos_crown_button_report()
 *   鼠标 X1 键   -> 侧键          cos_side_button_report()
 * 三个 report 接口都走 cos_dispatcher_call() 异步投递，因此可以安全地
 * 在 SDL 事件过滤回调里直接调用。未识别的按键放行给 LVGL 键盘 indev。
 * ------------------------------------------------------------------ */
static int SDLCALL _sim_sdl_event_filter(void *userdata, SDL_Event *event)
{
    (void)userdata;

    switch (event->type) {
        case SDL_MOUSEWHEEL:
            /* 滚轮一格 = 表冠转一格（y>0 为向前滚，远离用户） */
            if (event->wheel.y != 0)
                cos_crown_encoder_report((cos_crown_encoder_diff_t)event->wheel.y);
            if (event->wheel.x != 0)
                cos_crown_encoder_report((cos_crown_encoder_diff_t)event->wheel.x);
            return 0;

        case SDL_MOUSEBUTTONDOWN:
            if (event->button.button == SDL_BUTTON_MIDDLE) {
                printf("[INFO] [Sim] Crown pressed (middle button)\n");
                fflush(stdout);
                cos_crown_button_report(COS_BUTTON_STATE_CLICKED);
                return 0;
            }
            if (event->button.button == SDL_BUTTON_X1) {
                printf("[INFO] [Sim] Side button pressed (X1)\n");
                fflush(stdout);
                cos_side_button_report(COS_BUTTON_STATE_CLICKED);
                return 0;
            }
            return 1;   /* 左键 = 触摸，交给 SDL mouse indev */

        case SDL_MOUSEBUTTONUP:
            /* 按下已被消费，抬起一并丢弃，避免污染触摸路径 */
            if (event->button.button == SDL_BUTTON_MIDDLE ||
                event->button.button == SDL_BUTTON_X1)
                return 0;
            return 1;

        case SDL_KEYDOWN:
            if (event->key.repeat)
                return 0;
            switch (event->key.keysym.sym) {
                case SDLK_l:   /* L — 打开应用列表 */
                    printf("[INFO] [Sim] Hotkey 'L' pressed — opening app list\n");
                    fflush(stdout);
                    cos_app_list_enter();
                    return 0;
                case SDLK_r:   /* R — 返回上一级 */
                    printf("[INFO] [Sim] Hotkey 'R' pressed — going back\n");
                    fflush(stdout);
                    cos_activity_back();
                    return 0;
                case SDLK_c: { /* C — 开/关控制中心 */
                    cos_control_center_t *cc = cos_control_center_get_instance();
                    printf("[INFO] [Sim] Hotkey 'C' pressed — toggling control center (instance=%p)\n",
                           (void *)cc);
                    fflush(stdout);
                    /* 先让面板内容可见：关闭态下它保持隐藏，以免左侧触摸条
                     * 抢走表盘 catcher 的右滑手势。随后再切换开/关。 */
                    cos_control_center_show();
                    cos_control_panel_slide_change();
                    return 0;
                }
                case SDLK_h:   /* H — 直接回到表盘 */
                    printf("[INFO] [Sim] Hotkey 'H' pressed — back to watchface\n");
                    fflush(stdout);
                    cos_activity_back_to_watchface();
                    return 0;
                case SDLK_w: { /* W — 切换 WOS 演示 app */
                    printf("[INFO] [Sim] Hotkey 'W' pressed — toggling WOS demo app\n");
                    fflush(stdout);
                    if (wos_app_manager_get_state() == WOS_APP_STATE_ACTIVE ||
                        wos_app_manager_get_state() == WOS_APP_STATE_LAUNCHING) {
                        wos_app_manager_close();
                    } else {
                        wos_app_manager_open("demo");
                    }
                    return 0;
                }
                case SDLK_ESCAPE:
                    g_running = 0;
                    return 0;
                default:
                    return 1;   /* 其它按键放行给 LVGL 键盘 indev */
            }

        default:
            return 1;
    }
}

/* ------------------------------------------------------------------
 * CLI 自检的启动前置:把系统带到「真机已启动」状态。
 *
 * cos_init() 返回时系统仍在跑开机动画;activity controller 要等动画结束、
 * cos_main_loop() 消费 _pending_root_start 之后才被初始化并启动 root
 * (watchface)。CLI 自检分支紧跟 cos_init() 执行,此刻 controller 尚未就绪,
 * 任何 cos_activity_replace_root() / cos_app_list_enter() 都会以
 * "Activity controller not initialized" 失败——依赖 activity 的手势自检
 * 因此全红,且失败原因与被测逻辑无关。
 *
 * 这里按真机顺序快进:SDL 的窗口驱动会覆盖 LVGL tick 源,故先恢复默认
 * (lv_tick_inc 驱动),再反复 cos_main_loop() 直到 root 就绪。
 * 仅 CLI 自检分支调用(跑完即退出),不影响交互运行的 tick 源。
 *
 * 另:自检以「虚拟时间」快进(每帧 lv_tick_inc(16/20)),几十秒虚拟时间
 * 远超电源管理的熄屏超时(默认 10s;省电模式 5s)。一旦熄屏,PM 会在
 * lv_layer_top() 上盖一层全屏黑色 mask,后续所有真实触摸(PRESS→drag→
 * RELEASE)都被它吃掉,依赖手势的探针会全红,且失败原因与被测逻辑无关。
 * 真机没有这个问题:熄屏是"无触摸 10s"的真实时间。故在自检前把熄屏超时
 * 拉长到自检跑不到的量级,并强制唤醒一次清掉快进期间可能已生成的黑屏 mask。
 * ------------------------------------------------------------------ */
static void _sim_boot_until_root(void)
{
    lv_tick_set_cb(NULL);
    for (int i = 0; i < 600 && cos_activity_get_root() == NULL; i++) {
        lv_tick_inc(20);
        cos_main_loop();
    }
    cos_pm_set_sleep_timeout(1000000u);
    cos_pm_wake_up();
    cos_pm_set_sleep_timeout(1000000u);
}

int main(int argc, char *argv[])
{
    printf("CantoMk6OS Desktop Simulator\n");
    printf("=========================\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(_crash_filter);
#endif

    /* Initialize LVGL */
    lv_init();

    /* Create SDL window as display */
    lv_display_t *disp = lv_sdl_window_create(SIM_HOR_RES, SIM_VER_RES);
    if (!disp) {
        fprintf(stderr, "Failed to create SDL display\n");
        return 1;
    }

    /* Create mouse input device (左键 = 触摸) */
    lv_indev_t *mouse = lv_sdl_mouse_create();
    if (!mouse) {
        fprintf(stderr, "Failed to create SDL mouse\n");
        return 1;
    }

    /* Create keyboard input device (让未识别的按键能打进焦点控件) */
    lv_indev_t *kbd = lv_sdl_keyboard_create();
    if (!kbd) {
        fprintf(stderr, "Failed to create SDL keyboard\n");
        return 1;
    }

    /* 事件过滤器：滚轮=表冠、中键=表冠按下、X1=侧键、字母键=热键。
     * 必须在驱动开始 SDL_PollEvent 之前注册。 */
    SDL_SetEventFilter(_sim_sdl_event_filter, NULL);

    /* 显示面板标题(与真机型号一致) */
    lv_sdl_window_set_title(disp, "CantoMk6OS Simulator (XIAO ESP32-S3 / 240x240 round)");

    printf("LVGL initialized: %dx%d display with SDL2\n", SIM_HOR_RES, SIM_VER_RES);

    /* Initialize CantoMk6OS */
    cos_init();

    /* 桌面模拟器:禁用自动熄屏。
     * 真机默认「无触摸 10s」熄屏;而 SLEEP 态下需双击才亮屏
     * (cos_service_pm.c 的 _indev_pressed_cb 双击判定),在 SDL 窗口里
     * 表现为「放着不动就黑屏、单击不恢复」。这里与 CLI 自检路径
     * _sim_boot_until_root() 末尾做法一致,把熄屏超时拉到自检量级(幂等)。 */
    cos_pm_set_sleep_timeout(1000000u);
    cos_pm_wake_up();

    /* Seed bundled app plugins so the Launcher shows them even on a fresh FS.
     * Idempotent. */
    _sim_seed_install_plugins();

    /* CLI 自检分支统一在「真机已启动」状态下运行:
     * cos_init() 返回时仍在跑开机动画,activity controller 要到动画结束、
     * cos_main_loop() 消费 _pending_root_start 后才初始化。若不快进,所有
     * 依赖 activity 的探针(手势/breach/album/… 的 launch)都会以
     * "Activity controller not initialized" 失败。
     * 交互模式(无参数)不做此快进,以保持 SDL 实时 tick 源。 */
    if (argc > 1) {
        _sim_boot_until_root();
    }

    /* Shell: optional self-test mode (headless, exits after running commands) */
    if (argc > 1 && strcmp(argv[1], "--shell-test") == 0)
    {
        sim_run_shell_test();
        return 0;
    }

    /* Shell: stress mode (headless lifecycle hammering to catch crashes) */
    if (argc > 1 && strcmp(argv[1], "--stress-test") == 0)
    {
        sim_run_stress_test();
        return 0;
    }

    /* Shell: keyboard self-test (headless EN/ZH IME exercise) */
    if (argc > 1 && strcmp(argv[1], "--keyboard-test") == 0)
    {
        sim_run_keyboard_test();
        return 0;
    }

    /* UI Framework self-test (adaptive layout system across 4 profiles) */
    if (argc > 1 && strcmp(argv[1], "--ui-framework-test") == 0)
    {
        int rc = sim_run_ui_framework_test();
        return rc;
    }

    /* UI Framework rendering-layer demo (P8): build adaptive Home on the 4
     * profiles, drive a flick, assert items stay inside the safe area. */
    if (argc > 1 && strcmp(argv[1], "--ui-framework-demo") == 0)
    {
        int rc = sim_run_ui_framework_demo();
        return rc;
    }

    /* Launcher migration test: build the REAL Launcher Home (real Plugin
     * Manager app list) on the 4 profiles, assert shape-agnostic +
     * overflow/occlusion/hit-area correct via resolved geometry. */
    if (argc > 1 && strcmp(argv[1], "--launcher-test") == 0)
    {
        int rc = sim_run_launcher_test();
        return rc;
    }

    /* UI-JS bridge self-test: drive the cos.ui.* JerryScript bridge across
     * all 4 profiles from an in-memory JS program (Route A verification). */
    if (argc > 1 && strcmp(argv[1], "--ui-js-test") == 0)
    {
        int rc = sim_run_ui_js_test();
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "--notes-probe") == 0)
    {
        int rc = sim_run_notes_probe();
        return rc;
    }

    /* Breach Protocol app probe (SDL dummy): boot + structure */
    if (argc > 1 && strcmp(argv[1], "--breach-launch") == 0)
    {
        int rc = sim_run_breach_launch();
        return rc;
    }

    /* Breach Protocol Result-page probe (SDL dummy): natural 60s timeout -> Result overlay */
    if (argc > 1 && strcmp(argv[1], "--breach-result") == 0)
    {
        int rc = sim_run_breach_result();
        return rc;
    }

    /* Real-screen snapshot (watchface + app page + control center) for
     * visual rendering diagnosis the user can see. */
    if (argc > 1 && strcmp(argv[1], "--screen-snap") == 0)
    {
        int rc = sim_run_screen_snapshot();
        return rc;
    }

    /* Round 4 diagnostic: read the ACTUAL SDL texture and print per-row
     * luminance to expose the reported top-black/bottom-white split. */
    if (argc > 1 && strcmp(argv[1], "--pixel-probe") == 0)
    {
        int rc = sim_run_pixel_probe();
        return rc;
    }

    /* Render-check: dump the real presented pixels (incl. overlays) to PPM. */
    if (argc > 1 && strcmp(argv[1], "--render-check") == 0)
    {
        int rc = sim_run_render_check();
        return rc;
    }

    /* Watchface home-gesture catcher regression: the built-in watchface installs
     * a full-screen, CLICKABLE catcher layer that receives LV_EVENT_PRESSED /
     * LV_EVENT_RELEASED (the same press-drag path the swipe panels use) and
     * derives the swipe direction from the press/release point delta. This fork's
     * LV_EVENT_GESTURE is NOT reliably delivered to a full-screen catcher, so the
     * earlier GESTURE_BUBBLE approach silently dropped central-area swipes. The
     * catcher must therefore be CLICKABLE, full-screen, and NOT gesture-bubbling.
     * The actual left/right/up routing is asserted by --home-gesture-sim-test. */
    if (argc > 1 && strcmp(argv[1], "--watchface-gesture-test") == 0)
    {
        cos_watchface_instance_t *w = cos_watchface_builtin_create();
        if (!w || !w->activity) {
            printf("[FAIL] watchface instance/activity not created\n"); return 1;
        }
        /* Manually trigger _builtin_on_enter by replacing the root activity. */
        cos_activity_replace_root(w->activity);
        /* Pump one tick so any on_enter timers are registered. */
        lv_tick_inc(20);
        lv_timer_handler();

        int pass = 0, fail = 0;
#define WF_CHECK(cond, msg) do {                                            \
            if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
            else      { fail++; printf("[FAIL] %s\n", msg); }               \
        } while (0)
        lv_obj_t *view = cos_activity_get_view(w->activity);
        WF_CHECK(view != NULL, "watchface view present after on_enter");
        if (view) {
            /* The catcher is created with lv_pct(100); its pixel size is only
             * resolved after a layout pass. Force one so the width/height we
             * assert below are real (otherwise a freshly created pct object
             * reads 0/stale and the check flakes). */
            lv_obj_update_layout(view);
            /* The home-gesture catcher is the only full-screen, clickable
             * child of the view. (In this fork LV_OBJ_FLAG_GESTURE_BUBBLE is
             * set on EVERY child, so it cannot discriminate the catcher.)
             * The view itself is not clickable, so without this catcher LVGL
             * never produces a gesture target and every swipe is dropped.
             * Match against the VIEW size (profile-agnostic) rather than a
             * hardcoded 240, so the check holds on any display profile. */
            int n_children = lv_obj_get_child_cnt(view);
            WF_CHECK(n_children >= 3, "watchface view has at least 3 label children");
            bool catcher_ok = false;
            lv_coord_t vw = lv_obj_get_width(view), vh = lv_obj_get_height(view);
            for (int i = 0; i < n_children; i++) {
                lv_obj_t *c = lv_obj_get_child(view, i);
                if (!c) continue;
                if (lv_obj_has_flag(c, LV_OBJ_FLAG_CLICKABLE) &&
                    lv_obj_get_width(c) >= (vw * 9) / 10 &&
                    lv_obj_get_height(c) >= (vh * 9) / 10) {
                    catcher_ok = true;
                    break;
                }
            }
            WF_CHECK(catcher_ok,
                     "home-gesture catcher present (full-screen, clickable, PRESS/RELEASE) -> left/right/up work");
        }
        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef WF_CHECK
        return (fail == 0) ? 0 : 1;
    }

    /* Home swipe-navigation regression: the home-gesture catcher now uses
     * PRESS/RELEASE (not the unreliable LV_EVENT_GESTURE). Drive the
     * navigation logic with synthetic drag vectors and assert each direction
     * routes to the right destination. LEFT has no edge swipe panel, so this
     * is the only thing that opens the app list — it MUST work from anywhere. */
    if (argc > 1 && strcmp(argv[1], "--home-gesture-sim-test") == 0)
    {
        cos_watchface_instance_t *w = cos_watchface_builtin_create();
        if (!w || !w->activity) { printf("[FAIL] watchface instance/activity not created\n"); return 1; }
        cos_activity_replace_root(w->activity);
        /* The SDL display driver overrides LVGL's tick source, so lv_tick_inc()
         * is ignored and animations never advance in this headless self-test.
         * Restore the default (lv_tick_inc-driven) tick so the swipe-panel open
         * animations can settle to OPEN and we can assert the final state. */
        lv_tick_set_cb(NULL);
        lv_tick_inc(20); lv_timer_handler();

        int pass = 0, fail = 0;
#define HG_CHECK(c, m) do {                                            \
            if (c) { pass++; printf("[OK]   %s\n", m); }               \
            else      { fail++; printf("[FAIL] %s\n", m); }            \
        } while (0)
#define HG_PUMP() do { for (int _i = 0; _i < 60; _i++) { lv_tick_inc(20); lv_timer_handler(); } } while (0)

        /* RIGHT swipe -> control center opens */
        cos_watchface_builtin_test_swipe(200, 0);
        HG_PUMP();
        HG_CHECK(cos_control_center_is_open(), "right-swipe opens control center");
        cos_control_center_hide();
        lv_tick_inc(20); lv_timer_handler();

        /* UP swipe -> power-off page opens (内置表盘上滑 = 关机页;
         * 见 cos_watchface_builtin.c `_builtin_swipe_navigate`: up → 关机页,
         * down → 通知, left → App 列表, right → 控制中心)。 */
        cos_watchface_builtin_test_swipe(0, -200);
        HG_PUMP();
        HG_CHECK(cos_power_off_page_is_open(), "up-swipe opens power-off page");
        cos_power_off_page_close();
        lv_tick_inc(20); lv_timer_handler();

        /* LEFT swipe -> app list activity entered */
        cos_watchface_builtin_test_swipe(-200, 0);
        HG_PUMP();
        HG_CHECK(cos_activity_get_current() &&
                 cos_activity_get_type(cos_activity_get_current()) == COS_ACTIVITY_TYPE_APP_LIST,
                 "left-swipe opens app list");

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef HG_CHECK
#undef HG_PUMP
        return (fail == 0) ? 0 : 1;
    }

    /* indev-driven home swipe reproduction: feed a REAL right-swipe through the
     * LVGL pointer indev (PRESS -> drag -> RELEASE) and assert the control
     * center actually opens. This catches event-delivery regressions that the
     * direct cos_watchface_builtin_test_swipe() hook cannot surface. */
    if (argc > 1 && strcmp(argv[1], "--home-gesture-indev-test") == 0)
    {
        cos_watchface_instance_t *w = cos_watchface_builtin_create();
        if (!w || !w->activity) { printf("[FAIL] watchface instance/activity not created\n"); return 1; }
        cos_activity_replace_root(w->activity);
        lv_tick_set_cb(NULL);
        /* Let the watch face lay out (catcher full-screen) before swiping. */
        for (int _k = 0; _k < 12; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }

        lv_indev_t *indev = NULL;
        for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
            if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) { indev = i; break; }
        if (!indev) { printf("[FAIL] no pointer indev found\n"); return 1; }

        cos_control_center_t *cc = cos_control_center_get_instance();
        if (!cc || !cc->swipe_panel) { printf("[FAIL] control center instance/panel missing\n"); return 1; }

        int pass = 0, fail = 0;
#define INDV_CHECK(c, m) do {                                            \
            if (c) { pass++; printf("[OK]   %s\n", m); }                \
            else      { fail++; printf("[FAIL] %s\n", m); }             \
        } while (0)

        /* Helper: close the panel if open, then settle. */
#define INDV_CLOSE() do {                                                \
            cos_slide_widget_state_t _s = cos_slide_widget_get_state(cc->swipe_panel->sw); \
            if (_s == COS_SLIDE_WIDGET_STATE_OPEN) {                    \
                cos_swipe_panel_pull_back(cc->swipe_panel);             \
                for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); } \
            }                                                            \
        } while (0)

        /* Helper: drive one real swipe through lv_indev_read (PRESS -> drag ->
         * RELEASE), then settle animations. cos_dispatch_tick() runs the
         * deferred activity-cleanup queue (the real main loop does this) so the
         * app-list -> watchface destroy path is actually exercised. */
/* `px` = per-frame travel in pixels. Real mice deliver 1-4 px per frame; use
         * 4 px so LVGL's 10 px scroll threshold is crossed mid-gesture exactly as
         * it is in the real window. */
#define INDV_SWIPE_N(sx, ex, px) do {                                    \
            int _dist = ((ex) > (sx)) ? ((ex) - (sx)) : ((sx) - (ex));   \
            _hg_indev_steps = (_dist / (px) > 0) ? (_dist / (px)) : 1;   \
            _hg_indev_idx = 0; _hg_indev_start_x = (sx); _hg_indev_end_x = (ex); \
            lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);        \
            int _reads = _hg_indev_steps + 6;                            \
            for (int _k = 0; _k < _reads; _k++) { lv_tick_inc(16); lv_indev_read(indev); } \
            for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); } \
        } while (0)
#define INDV_SWIPE(sx, ex) INDV_SWIPE_N(sx, ex, 4)
/* 垂直 swipe（上下滑退出验证）：x 固定 120，y 插值 4px/帧 */
#define INDV_SWIPE_V(sy, ey) do {                                        \
            int _dist = ((ey) > (sy)) ? ((ey) - (sy)) : ((sy) - (ey));   \
            _hg_indev_steps = (_dist / 4 > 0) ? (_dist / 4) : 1;         \
            _hg_indev_idx = 0; _hg_indev_has_y = 1;                      \
            _hg_indev_start_y = (sy); _hg_indev_end_y = (ey);            \
            _hg_indev_start_x = 120; _hg_indev_end_x = 120; _hg_indev_y = 120; \
            lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);        \
            int _reads = _hg_indev_steps + 6;                            \
            for (int _k = 0; _k < _reads; _k++) { lv_tick_inc(16); lv_indev_read(indev); } \
            for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); } \
            _hg_indev_has_y = 0;                                         \
        } while (0)

        /* Case A: right-swipe from the LEFT edge (the natural control-bar pull). */
        INDV_CLOSE();
        /* Left-edge swipe: start at x=60 so the press lands on the home
         * catcher (the closed control center hides its 50px touch strip). */
        INDV_SWIPE(60, 235);
        INDV_CHECK(cos_slide_widget_get_state(cc->swipe_panel->sw) == COS_SLIDE_WIDGET_STATE_OPEN,
                   "left-edge right-swipe opens control center (REAL event path)");

        /* Case B: right-swipe from the CENTER (catcher handles it). */
        INDV_CLOSE();
        INDV_SWIPE(130, 235);
        INDV_CHECK(cos_slide_widget_get_state(cc->swipe_panel->sw) == COS_SLIDE_WIDGET_STATE_OPEN,
                   "center right-swipe opens control center (REAL event path)");

        /* Case C: left-swipe from the CENTER (user reports this WORKS in the
         * real window -> opens the app list). Used to confirm the indev harness
         * actually delivers events to the catcher at all. */
        INDV_CLOSE();  /* Case B left the control center OPEN; close it first
                        * or this press lands on the CC panel, not the catcher. */
        INDV_SWIPE(130, 20);
        INDV_CHECK(cos_activity_get_current() &&
                   cos_activity_get_type(cos_activity_get_current()) == COS_ACTIVITY_TYPE_APP_LIST,
                   "center left-swipe opens app list (REAL event path)");

        /* Case D: RIGHT-swipe on the APP page (now open from Case C) must pop
         * back to the watchface. Exercises the REAL exit path of the current
         * app-list page (built-in bubble_grid) — 退出手势 = 右滑
         * (cos_activity.c `_indev_swipe_back_cb` 仅接受 LV_DIR_RIGHT)。 */
        INDV_SWIPE(20, 235);
        INDV_CHECK(cos_activity_get_current() &&
                   cos_activity_get_type(cos_activity_get_current()) == COS_ACTIVITY_TYPE_WATCHFACE,
                   "app-page RIGHT-swipe exits back to watchface (REAL event path)");

        /* Case E: a SLOW swipe must NOT be hijacked by the long-press handler.
         * LVGL fires LV_EVENT_LONG_PRESSED on elapsed time alone (400 ms) and
         * never cancels it when the finger moves, so a deliberate 1-2 px/frame
         * swipe used to open the watchface list mid-gesture and swallow the rest
         * of the gesture. That is why the control center "randomly" refused to
         * open and why a slow left-swipe landed on a page that would not exit.
         * 2 px/frame over 225 px = ~112 samples = ~1.8 s of press: far past the
         * long-press threshold. */
        INDV_CLOSE();
        INDV_SWIPE_N(10, 235, 2);
        INDV_CHECK(cos_activity_get_current() &&
                   cos_activity_get_type(cos_activity_get_current()) == COS_ACTIVITY_TYPE_WATCHFACE,
                   "SLOW swipe does not open the watchface list (long-press needs to be stationary)");
        INDV_CHECK(cos_slide_widget_get_state(cc->swipe_panel->sw) == COS_SLIDE_WIDGET_STATE_OPEN,
                   "SLOW right-swipe still opens control center");

        /* Case F: a stationary long press MUST still open the watchface list
         * (the fix must not break press-and-hold to change watchface). */
        INDV_CLOSE();
        /* Hold still for ~1.2 s. Use a huge step count with a 1 px span so every
         * sample lands on the same integer coordinate and stays in the PRESSED
         * branch — note lv_timer_handler() ALSO drives the indev read timer, so
         * the sample index advances faster than this loop and must not be allowed
         * to run past `steps` (that would emit a spurious RELEASED). */
        _hg_indev_steps = 100000;
        _hg_indev_idx = 0; _hg_indev_start_x = 120; _hg_indev_end_x = 121;
        lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);
        for (int _k = 0; _k < 60; _k++) {
            lv_tick_inc(20);
            lv_indev_read(indev);         /* must be explicit: lv_timer_handler() does not drive this read_cb */
            lv_timer_handler();
        }
        _hg_indev_idx = 100001;           /* force RELEASED */
        lv_tick_inc(20); lv_indev_read(indev);
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        INDV_CHECK(cos_activity_get_current() &&
                   cos_activity_get_type(cos_activity_get_current()) == COS_ACTIVITY_TYPE_WATCHFACE_LIST,
                   "stationary long-press still opens the watchface list");
        cos_activity_back_to_watchface();
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }

        /* Case G: the 'C' hotkey path. Exercises exactly what the SDL key handler
         * calls, so the keyboard shortcut is proven to work rather than merely
         * proven to compile. It must open the panel, and toggle it shut again. */
        INDV_CLOSE();
        cos_control_center_show();
        cos_control_panel_slide_change();
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        INDV_CHECK(cos_slide_widget_get_state(cc->swipe_panel->sw) == COS_SLIDE_WIDGET_STATE_OPEN,
                   "hotkey 'C' path opens control center");
        cos_control_center_show();
        cos_control_panel_slide_change();
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        INDV_CHECK(cos_slide_widget_get_state(cc->swipe_panel->sw) != COS_SLIDE_WIDGET_STATE_OPEN,
                   "hotkey 'C' path closes control center again");

        /* Case H: WOS framework — open the demo app, then LEFT-swipe must
         * close it (App Manager swipe-back). Validates the new framework's
         * gesture routing AND the deferred cleanup (state returns to IDLE). */
        INDV_CLOSE();
        wos_app_manager_open("demo");
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        INDV_CHECK(wos_app_manager_get_state() == WOS_APP_STATE_ACTIVE,
                   "wos open('demo') reaches ACTIVE");
        INDV_SWIPE_V(200, 60);   /* UP-swipe across the app page (退出手势=上下滑) */
        INDV_CHECK(wos_app_manager_get_state() == WOS_APP_STATE_IDLE,
                   "wos UP-swipe closes the app (deferred cleanup -> IDLE)");

        /* Case I: REAL JerryScript eapk app (com.cantomk6.timer) launched via
         * the activity framework. RIGHT-swipe anywhere on the page must back
         * out to the watchface. This exercises the _indev_swipe_back_cb path
         * (退出手势 = 右滑)。*/
        INDV_CLOSE();
        cos_activity_back_to_watchface();
        for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        bool jsapp_ok = (cos_app_launch_immediately("com.cantomk6.timer") == COS_OK);
        INDV_CHECK(jsapp_ok, "launch 'com.cantomk6.timer' (eapk)");
        if (jsapp_ok) {
            /* Let the activity enter + spm_app_run + JS main.js execute + layout. */
            for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
            cos_activity_t *cur_a = cos_activity_get_current();
            INDV_CHECK(cur_a && cos_activity_get_type(cur_a) == COS_ACTIVITY_TYPE_APP,
                       "JS eapk app is the current activity");
            /* 左滑不再退出（退出手势 = 右滑） */
            INDV_SWIPE(120, 20);
            cos_activity_t *after_a = cos_activity_get_current();
            INDV_CHECK(after_a && cos_activity_get_type(after_a) == COS_ACTIVITY_TYPE_APP,
                       "left-swipe inside JS eapk app does NOT exit (exit gesture = right)");
            /* Drive a RIGHT-swipe from the CENTER of the screen — lands on
             * app content (a JS-created lv.obj), reproducing the real-window
             * gesture path. */
            INDV_SWIPE(20, 235);
            after_a = cos_activity_get_current();
            INDV_CHECK(after_a && cos_activity_get_type(after_a) == COS_ACTIVITY_TYPE_WATCHFACE,
                       "RIGHT-swipe inside JS eapk app pops back to watchface (indev-level)");
        }

        /* Case J (temp): relaunch the timer eapk, tap its content. Same
         * press→release harness as the swipes above. */
        cos_activity_back_to_watchface();
        for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        cos_app_launch_immediately("com.cantomk6.timer");
        for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        _hg_indev_steps = 0; _hg_indev_idx = 0;   /* 单帧 press + 立即 release（真窗口等价） */
        _hg_indev_start_x = 120; _hg_indev_end_x = 120; _hg_indev_y = 120;
        lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);
        for (int _k = 0; _k < 6; _k++) { lv_tick_inc(16); lv_indev_read(indev); }
        for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        printf("[tap] done state=%d pressed=%d\n", (int)lv_indev_get_state(indev),
               (int)lv_indev_get_press_moved(indev));
        INDV_CHECK(true, "tap executed");

#undef INDV_CLOSE
#undef INDV_CHECK
#undef INDV_SWIPE
#undef INDV_SWIPE_N
        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
        return (fail == 0) ? 0 : 1;
    }

    /* Cards-page regression: the small-cards overlay must be created with
     * 3+ seeded cards (activity, heart, battery) and be registered with the
     * chrome manager. The watchface must call cos_cards_page_show() so the
     * up-swipe touch strip is enabled at runtime. */
    if (argc > 1 && strcmp(argv[1], "--cards-page-test") == 0)
    {
        /* Includes below the test function definition; pull them in here. */
#include "framework/chrome/cos_chrome_manager.h"
        /* The simulator already booted through cos_core, which calls
         * cos_cards_page_init() and registers the overlay. Just verify. */
        int pass = 0, fail = 0;
#define CP_CHECK(cond, msg) do {                                            \
            if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
            else      { fail++; printf("[FAIL] %s\n", msg); }               \
        } while (0)

        cos_cards_page_t *cp = cos_cards_page_get_instance();
        CP_CHECK(cp != NULL, "cards page instance created during boot");
        if (cp)
        {
            CP_CHECK(cp->swipe_panel != NULL, "cards page has a swipe panel");
            CP_CHECK(cp->cards != NULL, "cards page has a card container");
            CP_CHECK(cp->card_count == 0, "cards page boots with NO seed cards (deleted; re-added via apps)");
            CP_CHECK(cp->header != NULL, "cards page has a header");
            CP_CHECK(cp->header_time != NULL && cp->header_date != NULL, "header has time + date labels");
            /* Verify the cards container has children matching card_count. */
            if (cp->cards)
            {
                uint32_t n = lv_obj_get_child_cnt(cp->cards);
                /* header + 0 cards = 1 child expected now (seeds removed) */
                CP_CHECK((int)n >= (int)cp->card_count,
                         "card container has child count >= card_count");
            }
        }
        /* Verify the cards page is registered with the chrome manager.
         * We can't directly query the registered list (private), but we
         * CAN trigger an open/close cycle and observe that the overlay
         * descriptor is non-NULL. */
        const cos_chrome_overlay_t *od = cos_cards_page_get_overlay_descriptor();
        CP_CHECK(od != NULL && od->name && strcmp(od->name, "cards_page") == 0,
                 "cards page chrome overlay descriptor registered");
        /* Verify the touch strip is enabled after a show() call. */
        cos_cards_page_show();
        if (cp && cp->swipe_panel && cp->swipe_panel->sw)
        {
            lv_obj_t *to = cos_slide_widget_get_touch_obj(cp->swipe_panel->sw);
            CP_CHECK(to && !lv_obj_has_flag(to, LV_OBJ_FLAG_HIDDEN),
                     "cards page touch strip visible after show()");
        }
        cos_cards_page_hide();
        if (cp && cp->swipe_panel && cp->swipe_panel->sw)
        {
            lv_obj_t *to = cos_slide_widget_get_touch_obj(cp->swipe_panel->sw);
            CP_CHECK(to && lv_obj_has_flag(to, LV_OBJ_FLAG_HIDDEN),
                     "cards page touch strip hidden after hide()");
        }

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef CP_CHECK
        return (fail == 0) ? 0 : 1;
    }

    /* Cards-page app-facing registration API: apps register/unregister
     * cards through cos_cards_page_register_card(). Verify a registered card
     * appears in the container, unregister removes it, and re-registering the
     * same id does not grow the count (de-dup). */
    if (argc > 1 && strcmp(argv[1], "--cards-api-test") == 0)
    {
        int pass = 0, fail = 0;
#define API_CHECK(cond, msg) do {                                            \
            if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
            else      { fail++; printf("[FAIL] %s\n", msg); }               \
        } while (0)

        cos_cards_page_t *cp = cos_cards_page_get_instance();
        API_CHECK(cp != NULL, "cards page instance present");
        int base = cp ? (int)cp->card_count : 0;
        uint32_t base_children = (cp && cp->cards) ? lv_obj_get_child_cnt(cp->cards) : 0;

        cos_card_handle_t h = cos_cards_page_register_card(&(cos_card_desc_t){
            .id = "app_test", .title = "AppCard", .accent = lv_color_hex(0x3355AA),
            .priority = 5, .build = NULL, .click = NULL, .user_data = NULL });
        API_CHECK(h != NULL, "register_card returns a non-NULL handle");
        API_CHECK(cp && (int)cp->card_count == base + 1, "card_count incremented after register");
        if (cp && cp->cards)
        {
            uint32_t n = lv_obj_get_child_cnt(cp->cards);
            API_CHECK(n == base_children + 1, "container child count grew by 1 after register");
        }

        cos_cards_page_unregister_card(h);
        API_CHECK(cp && (int)cp->card_count == base, "card_count restored after unregister");
        if (cp && cp->cards)
        {
            uint32_t n = lv_obj_get_child_cnt(cp->cards);
            API_CHECK(n == base_children, "container child count restored after unregister");
        }

        /* De-dup: re-registering the same id replaces, count unchanged. */
        cos_card_handle_t h1 = cos_cards_page_register_card(&(cos_card_desc_t){
            .id = "app_test", .title = "AppCard2", .accent = lv_color_hex(0x5533AA),
            .priority = 5, .build = NULL, .click = NULL, .user_data = NULL });
        API_CHECK(h1 != NULL, "re-register same id succeeds");
        API_CHECK(cp && (int)cp->card_count == base + 1, "re-register same id does not grow count");
        cos_cards_page_unregister_card(h1);
        API_CHECK(cp && (int)cp->card_count == base, "card_count restored after re-register+unregister");

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef API_CHECK
        return (fail == 0) ? 0 : 1;
    }

    /* Alarm app (JS eapk, com.cantomk6.alarm) headless state-machine test.
     *
     * The app is an English MULTI-alarm manager:
     *   HOME  : scrollable list (rows "HH:MM" + "ON"/"OFF" toggle + days/repeat),
     *           empty state "No alarms" / "Tap + to add", "+" opens the wizard.
     *   EDIT  : 4-step wizard TIME -> WEEKDAYS -> REPEAT -> NOTE with a
     *           CANCEL/NEXT/BACK/SAVE nav row.
     *   RING  : full-screen overlay with an "ALARM" title, a big "HH:MM" label
     *           and OFF / SNOOZE buttons.
     * Data lives in the app-scoped config.json under the STRING key "alarms"
     * (a JSON array); _alarm_write_alarms() writes exactly that.
     *
     * The probe launches with a written config, asserts the UI via label text /
     * visibility, drives a few real taps through the pointer indev, and forces
     * the ring deterministically with a swapped-in FAKE clock. */
    if (argc > 1 && strcmp(argv[1], "--alarm-test") == 0)
    {
        int pass = 0, fail = 0;
#define ALM_CHECK(c, m) do {                                            \
            if (c) { pass++; printf("[OK]   %s\n", m); }               \
            else      { fail++; printf("[FAIL] %s\n", m); }            \
        } while (0)

        /* helpers used: _alarm_find_label / _alarm_write_alarms /
         * _alarm_fake_dt / _alarm_fake_ops (all file-scope above) */
        cos_dev_time_t *_td = cos_dev_time_get_instance();
        const cos_dev_time_ops_t *_saved_ops = _td->ops;

        /* --- settle the LVGL tick so timers/animations actually advance --- */
        lv_tick_set_cb(NULL);
        for (int _k = 0; _k < 12; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }

        /* find the pointer indev for tap injection */
        lv_indev_t *indev = NULL;
        for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
            if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) { indev = i; break; }
        ALM_CHECK(indev != NULL, "pointer indev present");
        if (!indev)
        {
            printf("\n[summary] pass=%d fail=%d\n", pass, fail);
            return 1;
        }

        /* tap = single-frame press + immediate release (Case-J proven pattern) */
#define ALM_TAP(px, py) do {                                            \
            _hg_indev_steps = 0; _hg_indev_idx = 0;                     \
            _hg_indev_start_x = (px); _hg_indev_end_x = (px); _hg_indev_y = (py); \
            lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);       \
            for (int _k = 0; _k < 6; _k++) { lv_tick_inc(16); lv_indev_read(indev); } \
            for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); } \
        } while (0)

        /* pump N milliseconds of virtual time, running timers + dispatcher */
#define ALM_PUMP(ms) do {                                               \
            int _n = (ms) / 20;                                         \
            for (int _k = 0; _k < _n; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); } \
        } while (0)

        /* restart helper: pop back to watchface, settle deferred cleanup, relaunch */
#define ALM_RELAUNCH() do {                                             \
            cos_activity_back_to_watchface();                           \
            for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); } \
            ALM_CHECK(cos_app_launch_immediately("com.cantomk6.alarm") == COS_OK, "relaunch com.cantomk6.alarm"); \
            for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); } \
            view = cos_activity_get_view(cos_activity_get_current());  \
            ALM_CHECK(view != NULL, "relaunched activity view present"); \
        } while (0)

        /* The time service (cos_time_get) re-anchors to the RTC/fake device at
         * most once per 1000 ms and otherwise interpolates from its last anchor.
         * After swapping in the fake clock we must therefore pump >1 s so the
         * next cos_time_get() call (app checkDue runs every 1000 ms) re-anchors
         * to the fake datetime — otherwise the ring fires late or against a
         * stale anchor. ALM_SET_FAKE forces that re-anchoring. */
#define ALM_SET_FAKE(hh, mm) do {                                       \
            _alarm_fake_dt.hour = (hh); _alarm_fake_dt.min = (mm);      \
            _alarm_fake_dt.sec = 0; _alarm_fake_dt.ms = 0;              \
            ALM_PUMP(2200);   /* >= 2 re-anchor windows */              \
        } while (0)

        lv_obj_t *view = NULL;
        printf("=== Alarm app headless test ===\n");

        /* [1] empty state: no alarms -> "No alarms" + "Tap + to add" visible */
        _alarm_write_alarms("[]");
        ALM_CHECK(cos_app_launch_immediately("com.cantomk6.alarm") == COS_OK, "launch com.cantomk6.alarm");
        for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); cos_dispatch_tick(); }
        view = cos_activity_get_view(cos_activity_get_current());
        ALM_CHECK(view != NULL, "activity view present");
        {
            lv_obj_t *ttl = _alarm_find_label(view, "No alarms");
            lv_obj_t *sub = _alarm_find_label(view, "Tap + to add");
            ALM_CHECK(ttl && !lv_obj_has_flag(ttl, LV_OBJ_FLAG_HIDDEN), "empty state: 'No alarms' visible");
            ALM_CHECK(sub && !lv_obj_has_flag(sub, LV_OBJ_FLAG_HIDDEN), "empty state: 'Tap + to add' visible");
        }

        /* [2] one alarm (07:30, ON, daily) -> list row time + ON toggle */
        _alarm_write_alarms("[{\"id\":0,\"h\":7,\"m\":30,\"days\":0,\"rep\":-1,\"on\":true,\"lf\":0}]");
        ALM_RELAUNCH();
        {
            lv_obj_t *t = _alarm_find_label(view, "07:30");
            lv_obj_t *on = _alarm_find_label(view, "ON");
            lv_obj_t *e = _alarm_find_label(view, "No alarms");
            ALM_CHECK(t && !lv_obj_has_flag(t, LV_OBJ_FLAG_HIDDEN), "list row shows alarm time 07:30");
            ALM_CHECK(on != NULL, "list row toggle shows ON");
            ALM_CHECK(e && lv_obj_has_flag(e, LV_OBJ_FLAG_HIDDEN), "'No alarms' hidden when a list entry exists");
        }

        /* [2b] real tap on the ON/OFF toggle (row0 center ~ (153,60)) -> OFF */
        ALM_TAP(153, 60);
        ALM_CHECK(_alarm_find_label(view, "OFF") != NULL, "tap toggle -> OFF label");
        /* persistence: relaunch reloads the saved OFF state */
        ALM_RELAUNCH();
        ALM_CHECK(_alarm_find_label(view, "OFF") != NULL, "OFF state persisted across relaunch");

        /* [3] add wizard: "+" opens TIME; NEXT advances steps; SAVE returns home */
        ALM_TAP(154, 201);   /* addBtn center (128+26, 30+156+15) */
        lv_obj_t *stepTtl = _alarm_find_label(view, "TIME");
        lv_obj_t *editC = stepTtl ? lv_obj_get_parent(stepTtl) : NULL;
        ALM_CHECK(stepTtl && editC && !lv_obj_has_flag(editC, LV_OBJ_FLAG_HIDDEN),
                  "add wizard opens on TIME step");
        ALM_TAP(165, 184);   /* navNext center (130+35, 30+140+14) */
        ALM_CHECK(stepTtl && strcmp(lv_label_get_text(stepTtl), "WEEKDAYS") == 0, "NEXT -> WEEKDAYS step");
        ALM_TAP(165, 184);
        ALM_CHECK(stepTtl && strcmp(lv_label_get_text(stepTtl), "REPEAT") == 0, "NEXT -> REPEAT step");
        ALM_TAP(165, 184);
        ALM_CHECK(stepTtl && strcmp(lv_label_get_text(stepTtl), "NOTE") == 0, "NEXT -> NOTE step");
        ALM_TAP(165, 184);   /* SAVE */
        ALM_CHECK(stepTtl && lv_obj_has_flag(lv_obj_get_parent(stepTtl), LV_OBJ_FLAG_HIDDEN),
                  "SAVE returns to home list (edit container hidden)");

        /* [4] ring overlay with fake clock 12:34:00 */
        _alarm_fake_dt.year = 2026; _alarm_fake_dt.month = 8; _alarm_fake_dt.day = 17;
        _alarm_fake_dt.day_of_week = 1;
        _td->ops = &_alarm_fake_ops;      /* swap in fake clock */
        ALM_SET_FAKE(12, 34);             /* force the time service to re-anchor */
        _alarm_write_alarms("[{\"id\":0,\"h\":12,\"m\":34,\"days\":0,\"rep\":-1,\"on\":true,\"lf\":0}]");
        ALM_RELAUNCH();
        ALM_PUMP(4000);                   /* let checkDue (every 1 s) fire the ring */
        {
            lv_obj_t *ttl = _alarm_find_label(view, "ALARM");
            lv_obj_t *ov = ttl ? lv_obj_get_parent(ttl) : NULL;
            ALM_CHECK(ov != NULL, "ring overlay: ALARM title label present");
            ALM_CHECK(ov && !lv_obj_has_flag(ov, LV_OBJ_FLAG_HIDDEN),
                      "ring overlay visible (ringC not HIDDEN)");
            /* search inside the ring container so the list-row "HH:MM" cannot
             * be mistaken for the ring time label */
            ALM_CHECK(ov && _alarm_find_label(ov, "12:34") != NULL,
                      "ring overlay: time label shows 12:34");
            ALM_CHECK(ov && _alarm_find_label(ov, "OFF") != NULL, "ring overlay: OFF button present");
            ALM_CHECK(ov && _alarm_find_label(ov, "SNOOZE") != NULL, "ring overlay: SNOOZE button present");

            /* CRITICAL: move the fake clock past the match window BEFORE tapping
             * OFF, otherwise the 100 ms due-check could re-trigger the ring. */
            _alarm_fake_dt.min = 35;
            ALM_PUMP(300);
            ALM_TAP(75, 205);   /* offBtn center (44+31, 192+13) */
            ALM_CHECK(ov && lv_obj_has_flag(ov, LV_OBJ_FLAG_HIDDEN),
                      "ring overlay hidden after tapping OFF");
        }

        /* [5] no ring when the time does not match (clock 12:20 vs alarm 12:35) */
        ALM_SET_FAKE(12, 20);   /* re-anchor to 12:20 BEFORE writing the alarm */
        _alarm_write_alarms("[{\"id\":0,\"h\":12,\"m\":35,\"days\":0,\"rep\":-1,\"on\":true,\"lf\":0}]");
        ALM_RELAUNCH();
        ALM_PUMP(4000);
        {
            lv_obj_t *ttl = _alarm_find_label(view, "ALARM");
            lv_obj_t *ov = ttl ? lv_obj_get_parent(ttl) : NULL;
            ALM_CHECK(ov == NULL || lv_obj_has_flag(ov, LV_OBJ_FLAG_HIDDEN),
                      "no ring when fake clock (12:20) != alarm (12:35)");
        }

        _td->ops = _saved_ops;   /* restore real clock */

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef ALM_TAP
#undef ALM_PUMP
#undef ALM_SET_FAKE
#undef ALM_RELAUNCH
#undef ALM_CHECK
        return (fail == 0) ? 0 : 1;
    }

    /* Shell: wire a serial-like console when stdin is a real TTY */
    sim_console_init();
    if (g_console)
    {
        cos_shell_set_reboot_cb(sim_reboot);
        cos_shell_framework_init();
        cos_shell_framework_prompt(sim_echo, NULL);
        printf("[INFO] [Sim] Shell console ready. Type commands (e.g. 'help').\n");
        fflush(stdout);
    }

    printf("CantoMk6OS initialized, entering main loop...\n");
    printf("---------------------------------------------\n");
    printf(" Hardware input emulation (click the sim window first):\n");
    printf("   left mouse drag  touch\n");
    printf("   mouse wheel      crown rotate\n");
    printf("   middle button    crown press\n");
    printf("   X1 (back) button side button\n");
    printf(" Hotkeys:\n");
    printf("   C   toggle Control Center\n");
    printf("   L   open App list\n");
    printf("   H   back to Watchface\n");
    printf("   R   back one step\n");
    printf("   W   toggle WOS demo app\n");
    printf("   Esc quit\n");
    printf("---------------------------------------------\n\n");

    /* Main loop. SDL 事件由 LVGL 的 SDL 驱动内部消费，本循环只负责：
     *   1) 轮询串口 shell（stdin 非 TTY 时为空操作）
     *   2) cos_main_loop()：分发 tick + lv_timer_handler()
     *   3) 按返回值休眠，避免忙等 */
    while (g_running) {
        /* Poll the shell console (no-op when stdin is not a TTY) */
        sim_console_poll();

        /* Drive CantoMk6OS + LVGL */
        uint32_t sleep_ms = cos_main_loop();

        /* Sleep to avoid busy-waiting */
        if (sleep_ms > 0 && sleep_ms < 100) {
#ifdef _WIN32
            Sleep(sleep_ms);
#else
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = sleep_ms * 1000000L;
            nanosleep(&ts, NULL);
#endif
        }
    }

    printf("\nSimulator terminated.\n");
    return 0;
}
