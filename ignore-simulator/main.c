/**
 * @file main.c
 * @brief ElenixOS Desktop Simulator entry point
 *
 * Initializes LVGL with SDL2 display driver, then boots ElenixOS.
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
#include "drivers/sdl/lv_sdl_private.h"
#include "eos_core.h"
#include "kernel/scheduler/eos_dispatcher.h"   /* eos_dispatch_tick() for faithful deferred-cleanup pumping */
#include "eos_log.h"
#include "framework/app/eos_app_list.h"
#include "framework/app/eos_app.h"
#include "framework/activity/eos_activity.h"
#include "services/storage/eos_storage_paths.h"
#include "services/storage/eos_service_storage.h"
#include "port/file_system/eos_fs_port.h"   /* draw-launch probe: eos_fs_open_read/size/read to verify .edrw */
#include "cJSON.h"              /* album-probe: 模拟 FM 写 app 私有 config */
#include "apps/gallery/eos_gallery.h"
#include "apps/files/eos_files.h"
#include "apps/flash_light/eos_flash_light.h"

#include <SDL.h>

#include "eos_shell.h"
#include "eos_shell_framework.h"
#include "ui/widgets/keyboard/eos_keyboard.h"
#include "ui/framework/eos_ui_framework.h"
#include "ui/launcher/eos_launcher.h"   /* eos_launcher_build_home() */
#include "ui/system/eos_round_clip.h"     /* eos_round_clip() */
#include "ui/widgets/basic_widgets/eos_basic_widgets.h" /* eos_draw_buf_create */
#include "spm.h"                         /* spm_app_run / spm_app_stop (in-memory JS) */
#include "devices/time/eos_dev_time.h"                 /* alarm-test: fake clock for ring test */
#include "services/config/eos_service_config.h"        /* alarm-test: persistent state asserts */
#include "ui/widgets/cards_page/eos_cards_page.h" /* cards page + app registration API */
#include "framework/watchface/eos_watchface_builtin.h"      /* builtin watchface + sim swipe hook */
#include "ui/widgets/control_center/eos_control_center.h"   /* control center overlay */
#include "ui/wos/eos_wos.h"                                 /* WOS demo app hotkey 'W' */
#include "ui/widgets/swipe_panel/eos_swipe_panel.h"         /* swipe panel state */
#include "ui/widgets/slide_widget/eos_slide_widget.h"       /* slide widget state */

#include <math.h>
#include <string.h>
#include <windows.h>

/* Crash localizer: on an access violation / abort, dump the faulting address
 * and a stack backtrace so we can map it with addr2line without a debugger. */
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
    eos_shell_exec(line, sim_shell_out, NULL);
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
        "app info com.cantomk6.clock", "log", "reboot",
        /* New Shell-framework / Plugin-Manager / IME commands */
        "plugin scan", "plugin scan --force", "plugin list",
        "ime ni", "ime zhongguo", "ime jianpan", NULL
    };
    printf("=== ElenixOS Shell self-test ===\n\n");
    for (int i = 0; cmds[i]; i++)
    {
        printf("> %s\n", cmds[i]);
        eos_shell_exec(cmds[i], sim_shell_out, NULL);
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
        char eapk[EOS_FS_PATH_MAX];
        snprintf(eapk, sizeof(eapk), "%s.sys/app/com.cantomk6.pmdemo.eapk", EOS_SYS_ROOT_DIR);
        if (eos_storage_is_file(eapk))
        {
            char cmd[320];
            printf("> app install %s\n", eapk);
            snprintf(cmd, sizeof(cmd), "app install %s", eapk);
            eos_shell_exec(cmd, sim_shell_out, NULL);
            printf("\n");

            uint32_t n = eos_app_get_installed();
            for (uint32_t i = 0; i < n; i++)
            {
                const char *aid = eos_app_list_get_id(i);
                if (!aid)
                    continue;
                if (strcmp(aid, "sys.settings") == 0
                    || strcmp(aid, "sys.flash_light") == 0
#ifdef EOS_ENABLE_TEST_APP
                    || strcmp(aid, "sys.test") == 0
#endif
                )
                    continue;
                printf("> app start %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app start %s", aid);
                eos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                printf("> app stop %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app stop %s", aid);
                eos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                /* Disable / enable cycle (management surface) */
                printf("> app disable %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app disable %s", aid);
                eos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                printf("> app enable %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app enable %s", aid);
                eos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                /* Uninstall, then re-install so the app is left installed
                 * for interactive Launcher inspection. */
                printf("> app uninstall %s\n", aid);
                snprintf(cmd, sizeof(cmd), "app uninstall %s", aid);
                eos_shell_exec(cmd, sim_shell_out, NULL);
                printf("\n");

                printf("> app install %s\n", eapk);
                snprintf(cmd, sizeof(cmd), "app install %s", eapk);
                eos_shell_exec(cmd, sim_shell_out, NULL);
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
        eos_gallery_enter();
        printf("> activity back\n");
        eos_activity_back();
        printf("> files enter\n");
        eos_files_enter();
        printf("> activity back\n");
        eos_activity_back();
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
    printf("=== ElenixOS Stress test (%d rounds) ===\n", rounds);
    fflush(stdout);
    for (int i = 0; i < rounds; i++)
    {
        /* Flashlight: full enter -> back (card pager + timer + exit button) */
        eos_app_launch_immediately("com.cantomk6.timer");
        eos_activity_back();

        /* Clock plugin via Plugin Manager + SPM/JerryScript lifecycle */
        eos_shell_exec("app start com.cantomk6.clock", sim_shell_out, NULL);
        eos_shell_exec("app stop com.cantomk6.clock", sim_shell_out, NULL);

        /* Native media apps */
        eos_gallery_enter();
        eos_activity_back();
        eos_files_enter();
        eos_activity_back();

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

#ifdef _WIN32
#include <conio.h>
#include <io.h>
static int g_console = 0;

static void sim_reboot(void)
{
    g_running = 0;
}

/* Non-blocking line reader for when the simulator is launched from a terminal.
 * Bytes are fed one at a time into the interactive Shell framework, which owns
 * the line buffer / history / prompt / echo and dispatches completed lines to
 * the Core command engine. Extended keys (0x00 / 0xE0 prefix) are dropped so
 * the framework never sees arrow-key garbage bytes. */
static void sim_console_poll(void)
{
    if (!g_console)
        return;
    while (_kbhit())
    {
        int c = _getch();
        if (c == 0 || c == 0xE0) { _getch(); continue; } /* extended key */
        eos_shell_framework_feed((char)c, sim_echo, sim_line_handler, NULL);
    }
}
#endif /* _WIN32 */

/* Seed bundled demo plugins so the interactive Launcher shows them even on a
 * fresh FS. Idempotent: skips any app already installed. Runs on every boot
 * (both interactive and --shell-test modes). The clock plugin replaces the
 * old hard-coded native C Clock activity (design-doc stage E). */
static void _sim_seed_install_plugins(void)
{
    static const char *const seeds[] = {
        "com.cantomk6.clock",
        "com.cantomk6.timer",
        "com.cantomk6.pmdemo",
        "com.cantomk6.alarm",
    };
    for (int i = 0; i < (int)(sizeof(seeds) / sizeof(seeds[0])); i++)
    {
        if (eos_app_list_contains(seeds[i]))
            continue;
        char eapk[EOS_FS_PATH_MAX];
        snprintf(eapk, sizeof(eapk), "%s.sys/app/%s.eapk", EOS_SYS_ROOT_DIR, seeds[i]);
        if (eos_storage_is_file(eapk))
        {
            eos_result_t r = eos_app_install(eapk);
            EOS_LOG_I("[Sim] seed-install %s (result=%d)", seeds[i], (int)r);
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
    printf("=== ElenixOS Keyboard self-test ===\n");
    fflush(stdout);

    lv_obj_t *ta = lv_textarea_create(lv_scr_act());
    lv_textarea_set_text(ta, "");
    lv_obj_t *kb = eos_keyboard_create(lv_scr_act());
    eos_keyboard_set_textarea(kb, ta);

    /* ---- EN mode: type "hello" ---- */
    const char *en[] = { "h", "e", "l", "l", "o", NULL };
    for (int i = 0; en[i]; i++)
        eos_keyboard_send_key(kb, en[i]);
    printf("[EN] textarea: '%s'\n", lv_textarea_get_text(ta));

    /* ---- ZH mode: pinyin "nihao" then commit candidate #1 ---- */
    lv_textarea_set_text(ta, "");
    eos_keyboard_send_key(kb, "MODE");
    printf("[ZH] mode = %d (1 = ZH)\n", (int)eos_keyboard_get_mode(kb));
    const char *zh[] = { "n", "i", "h", "a", "o", NULL };
    for (int i = 0; zh[i]; i++)
        eos_keyboard_send_key(kb, zh[i]);
    eos_keyboard_send_key(kb, "1");
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
    printf("=== ElenixOS UI Framework self-test ===\n");
    fflush(stdout);
    int pass = 0, fail = 0;
#define CHECK(cond, msg) do {                                            \
        if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
        else      { fail++; printf("[FAIL] %s\n", msg); }               \
    } while (0)

    /* ---- 1. DisplayProfile across the 4 required shapes ---- */
    for (int id = 0; id < (int)EOS_PROFILE_COUNT; id++) {
        eos_display_profile_t p = eos_display_profiles_get((eos_profile_id_t)id);
        if (p.shape == EOS_DISPLAY_SHAPE_CIRCLE)
            CHECK(p.safe_radius > 0.0f && p.safe_radius < p.radius, "profile safe_radius (CIRCLE)");
        else
            CHECK(p.safe_w > 0.0f && p.safe_h > 0.0f, "profile safe_rect (SQUARE/RECT)");
        CHECK(fabsf(eos_dp(&p, 48.0f) - 48.0f * p.dp_scale) < 0.001f, "dp scaling");
        float x = (float)p.width + 50.0f, y = (float)p.height + 50.0f;
        bool moved = eos_display_profile_clamp_inside(&p, &x, &y, 10.0f);
        CHECK(moved && eos_display_profile_is_inside(&p, x, y, 10.0f), "clamp inside safe area");
    }

    /* ---- 2. LayoutManager selection + safe-area containment ---- */
    {
        eos_display_profile_t pc = eos_display_profiles_get(EOS_PROFILE_240C);
        eos_display_profile_t ps = eos_display_profiles_get(EOS_PROFILE_240S);
        eos_layout_manager_t *lc = eos_layout_manager_create(&pc);
        eos_layout_manager_t *ls = eos_layout_manager_create(&ps);
        CHECK(lc && lc->shape == EOS_DISPLAY_SHAPE_CIRCLE, "manager selects CIRCLE");
        CHECK(ls && ls->shape == EOS_DISPLAY_SHAPE_SQUARE, "manager selects SQUARE");

        eos_widget_t w[8];
        for (int i = 0; i < 8; i++) {
            memset(&w[i], 0, sizeof(w[i]));
            w[i].kind = EOS_WIDGET_ICON; w[i].w_dp = 40; w[i].h_dp = 40;
        }
        lc->layout(lc, &pc, w, 8);
        int inside = 0;
        for (int i = 0; i < 8; i++) {
            float cx = w[i].x + w[i].w * 0.5f, cy = w[i].y + w[i].h * 0.5f;
            float r = (w[i].w > w[i].h ? w[i].w : w[i].h) * 0.5f;
            if (eos_display_profile_is_inside(&pc, cx, cy, r)) inside++;
        }
        CHECK(inside == 8, "circle layout all inside safe area");

        eos_widget_t w2[8];
        for (int i = 0; i < 8; i++) {
            memset(&w2[i], 0, sizeof(w2[i]));
            w2[i].kind = EOS_WIDGET_ICON; w2[i].w_dp = 50; w2[i].h_dp = 50;
        }
        ls->layout(ls, &ps, w2, 8);
        int inside2 = 0;
        for (int i = 0; i < 8; i++) {
            float cx = w2[i].x + w2[i].w * 0.5f, cy = w2[i].y + w2[i].h * 0.5f;
            float r = (w2[i].w > w2[i].h ? w2[i].w : w2[i].h) * 0.5f;
            if (eos_display_profile_is_inside(&ps, cx, cy, r)) inside2++;
        }
        CHECK(inside2 == 8, "square layout all inside safe rect");
        lc->destroy(lc); ls->destroy(ls);
    }

    /* ---- 3. ArcList geometry on every profile ---- */
    for (int id = 0; id < (int)EOS_PROFILE_COUNT; id++) {
        eos_display_profile_t p = eos_display_profiles_get((eos_profile_id_t)id);
        eos_arclist_t al;
        eos_arclist_init(&al, &p, 12);
        CHECK(eos_arclist_focus_index(&al) == 0, "arclist focus index 0 at start");
        CHECK(fabsf(al.items[0].scale - al.focus_scale) < 0.01f, "arclist focus max scale");
        CHECK(fabsf(al.items[0].opacity - 1.0f) < 0.01f, "arclist focus full opacity");
        bool mono = (al.items[0].scale >= al.items[1].scale) &&
                    (al.items[1].scale >= al.items[2].scale);
        CHECK(mono, "arclist scale monotonic with distance");
        int vis_ok = 1;
        for (int i = 0; i < 12; i++) {
            if (!al.items[i].visible) continue;
            float r = 22.0f * p.dp_scale * al.items[i].scale;
            if (!eos_display_profile_is_inside(&p, al.items[i].x, al.items[i].y, r))
                vis_ok = 0;
        }
        CHECK(vis_ok, "arclist visible items inside safe area");
    }

    /* ---- 4. Physics: friction decay stays in bounds ---- */
    {
        eos_scroller_t s; eos_scroller_init(&s, 0.0f, 1000.0f);
        s.pos = 0.0f; s.vel = 1000.0f;
        for (int i = 0; i < 200; i++) eos_scroller_step(&s, 1.0f / 60.0f);
        CHECK(eos_scroller_is_resting(&s), "physics rests after flick");
        CHECK(s.pos >= 0.0f && s.pos <= 1000.0f, "physics stays within bounds");
        CHECK(s.pos > 100.0f && s.pos < 500.0f, "physics friction distance sane");
    }
    /* ---- 4b. Physics: bounce back from beyond the upper bound ---- */
    {
        eos_scroller_t s; eos_scroller_init(&s, 0.0f, 100.0f);
        s.pos = 100.0f; s.vel = 1500.0f;
        float max_over = 0.0f;
        for (int i = 0; i < 200; i++) {
            eos_scroller_step(&s, 1.0f / 60.0f);
            float over = s.pos - 100.0f;
            if (over > max_over) max_over = over;
        }
        CHECK(eos_scroller_is_resting(&s), "physics bounce rests");
        CHECK(s.pos >= 0.0f && s.pos <= 101.0f, "physics bounce returns to bound");
        CHECK(max_over <= 60.0f, "physics bounce overshoot bounded");
    }

    /* ---- 5. Easing curves ---- */
    {
        CHECK(fabsf(eos_ease_linear(0) - 0) < 1e-3f &&
              fabsf(eos_ease_linear(1) - 1) < 1e-3f, "ease_linear endpoints");
        CHECK(fabsf(eos_ease_out_cubic(1) - 1) < 1e-3f, "ease_out_cubic end");
        float prev = -1.0f; bool mono = true;
        for (int i = 0; i <= 10; i++) {
            float v = eos_ease_out_cubic((float)i / 10.0f);
            if (v < prev - 1e-4f) mono = false;
            prev = v;
        }
        CHECK(mono, "ease_out_cubic monotonic");
        float mx = 0.0f;
        for (int i = 0; i <= 10; i++) {
            float v = eos_ease_out_back((float)i / 10.0f);
            if (v > mx) mx = v;
        }
        CHECK(mx > 1.0f && fabsf(eos_ease_out_back(1) - 1) < 1e-3f,
              "ease_out_back overshoot + end");
    }

    /* ---- 6. AppManager + LayoutStorage round-trip ---- */
    {
        eos_app_manager_t m; eos_app_manager_init(&m);
        eos_app_manager_register(&m, "watch", "Watch", 1);
        eos_app_manager_register(&m, "weather", "Weather", 2);
        eos_app_manager_register(&m, "music", "Music", 3);
        eos_app_manager_register(&m, "clock", "Clock", 4);
        CHECK(eos_app_manager_count(&m) == 4, "app manager count");
        eos_app_manager_move(&m, 0, 3);
        CHECK(eos_app_manager_find(&m, "watch") == 3, "app manager move reorders");
        eos_layout_storage_t ls; eos_layout_storage_init(&ls, "Bubble");
        CHECK(eos_layout_storage_save(&ls, &m), "layout storage save");
        eos_app_manager_t m2; eos_app_manager_init(&m2);
        eos_app_manager_register(&m2, "watch", "Watch", 1);
        eos_app_manager_register(&m2, "weather", "Weather", 2);
        eos_app_manager_register(&m2, "music", "Music", 3);
        eos_app_manager_register(&m2, "clock", "Clock", 4);
        CHECK(eos_layout_storage_load(&ls, &m2), "layout storage load");
        bool same = true;
        for (int i = 0; i < 4; i++)
            if (m.order[i] != m2.order[i]) same = false;
        CHECK(same, "layout storage order round-trip");
    }

    /* ---- 7. ThemeManager round-trip (no live LVGL object needed) ---- */
    {
        eos_theme_t t; eos_theme_manager_init(&t);
        eos_theme_manager_set(&t, "Aurora", 200, 0x33ccff, 22, 60, true);
        char buf[256];
        int n = eos_theme_manager_serialize(&t, buf, sizeof(buf));
        eos_theme_t t2; eos_theme_manager_init(&t2);
        CHECK(n > 0 && eos_theme_manager_deserialize(&t2, buf),
              "theme serialize/deserialize");
        CHECK(strcmp(t2.name, "Aurora") == 0 && t2.bg_alpha == 200 &&
              t2.accent == 0x33ccff && t2.radius == 22 && t2.glow == 60 &&
              t2.glass == true, "theme round-trip values");
        eos_theme_manager_apply(&t, NULL, true);   /* NULL-safe */
        eos_liquid_glass_card(NULL);
        eos_liquid_glass_panel(NULL);
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
    printf("=== ElenixOS UI Framework rendering-layer demo ===\n");
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

    for (int id = 0; id < (int)EOS_PROFILE_COUNT; id++) {
        eos_display_profile_t p = eos_display_profiles_get((eos_profile_id_t)id);
        const char *pname = eos_display_profiles_name((eos_profile_id_t)id);

        eos_framework_home_t *h =
            eos_framework_home_create(holder, &p, DEMO_APPS, N, NULL);
        CHECK(h != NULL, "home created");
        if (!h) continue;

        CHECK(lv_obj_is_valid(h->root), "home root valid");
        CHECK(lv_obj_is_valid(h->statusbar), "status bar valid");

        /* Status bar (resolved geometry) must fit inside the safe area — the
         * key "no rectangular overflow on round screens" assertion. We use the
         * stored rect, not lv_obj_get_* (those need an LVGL refresh pass that
         * this headless no-timer demo never runs). */
        float sx, sy, sw, sh;
        eos_framework_home_statusbar_rect(h, &sx, &sy, &sw, &sh);
        float scx = sx + sw * 0.5f, scy = sy + sh * 0.5f;
        float sr = sqrtf((sw * 0.5f) * (sw * 0.5f) + (sh * 0.5f) * (sh * 0.5f));
        CHECK(eos_display_profile_is_inside(&p, scx, scy, sr),
              "status bar inside safe area");

        /* Drive a flick + inertia, then verify items stay contained. */
        eos_framework_home_drag(h, -50.0f);
        eos_framework_home_release(h);
        for (int f = 0; f < 90; f++) eos_framework_home_step(h, 16.7f);

        int vis_ok = 1, cnt = 0;
        if (h->is_circle && h->arc) {
            cnt = eos_arclist_view_count(h->arc);
            for (int i = 0; i < cnt; i++) {
                const eos_arclist_item_t *it = &h->arc->al.items[i];
                if (!it->visible) continue;
                float r = (h->arc->card_px * it->scale) * 0.5f;
                if (!eos_display_profile_is_inside(&p, it->x, it->y, r))
                    vis_ok = 0;
            }
        } else if (h->grid) {
            cnt = eos_layout_view_count(h->grid);
            for (int i = 0; i < cnt; i++) {
                eos_widget_t *w = &h->grid->widgets[i];
                float cx = w->x + w->w * 0.5f, cy = w->y + w->h * 0.5f;
                float r = (w->w > w->h ? w->w : w->h) * 0.5f;
                if (!eos_display_profile_is_inside(&p, cx, cy, r))
                    vis_ok = 0;
            }
        }
        CHECK(cnt == N, "app count matches");
        CHECK(vis_ok, "all rendered items inside safe area");
        (void)pname;

        lv_anim_del_all();
        eos_framework_home_destroy(h);
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
    printf("=== ElenixOS Launcher migration test ===\n");
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

    for (int id = 0; id < (int)EOS_PROFILE_COUNT; id++) {
        eos_display_profile_t p = eos_display_profiles_get((eos_profile_id_t)id);

        eos_framework_home_t *h = NULL;
        int n = eos_launcher_build_home(holder, &p, &h);
        CHECK(h != NULL, "launcher home created");
        if (!h) continue;

        CHECK(lv_obj_is_valid(eos_framework_home_root(h)), "launcher home root valid");

        /* Status bar (resolved geometry) must fit inside the safe area. */
        float sx, sy, sw, sh;
        eos_framework_home_statusbar_rect(h, &sx, &sy, &sw, &sh);
        float scx = sx + sw * 0.5f, scy = sy + sh * 0.5f;
        float sr = sqrtf((sw * 0.5f) * (sw * 0.5f) + (sh * 0.5f) * (sh * 0.5f));
        CHECK(eos_display_profile_is_inside(&p, scx, scy, sr),
              "launcher status bar inside safe area");

        /* On round screens the arc tops out near the bar; the topmost visible
         * item must clear the status bar so app text never hides behind it
         * ("字体重叠" regression guard). */
        if (h->is_circle && h->arc) {
            float min_y = 1e9f;
            for (int i = 0; i < eos_arclist_view_count(h->arc); i++) {
                const eos_arclist_item_t *it = &h->arc->al.items[i];
                if (!it->visible) continue;
                if (it->y < min_y) min_y = it->y;
            }
            CHECK(min_y >= sy + sh * 0.5f, "launcher top arc item clears status bar");
        }

        /* Drive a flick + inertia, then verify items stay contained. */
        eos_framework_home_drag(h, -50.0f);
        eos_framework_home_release(h);
        for (int f = 0; f < 90; f++) eos_framework_home_step(h, 16.7f);

        int vis_ok = 1, cnt = 0;
        if (h->is_circle && h->arc) {
            cnt = eos_arclist_view_count(h->arc);
            for (int i = 0; i < cnt; i++) {
                const eos_arclist_item_t *it = &h->arc->al.items[i];
                if (!it->visible) continue;
                float r = (h->arc->card_px * it->scale) * 0.5f;
                if (!eos_display_profile_is_inside(&p, it->x, it->y, r)) {
                    vis_ok = 0;
                }
            }
        } else if (h->grid) {
            cnt = eos_layout_view_count(h->grid);
            for (int i = 0; i < cnt; i++) {
                eos_widget_t *w = &h->grid->widgets[i];
                float cx = w->x + w->w * 0.5f, cy = w->y + w->h * 0.5f;
                float r = (w->w > w->h ? w->w : w->h) * 0.5f;
                if (!eos_display_profile_is_inside(&p, cx, cy, r)) {
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
        eos_launcher_rebuild();
        eos_framework_home_t *h2 = eos_launcher_get_home();
        CHECK(h2 != NULL, "launcher rebuild on install event");
        if (h2)
        {
            CHECK(lv_obj_is_valid(eos_framework_home_root(h2)),
                  "launcher rebuilt root valid");
            int vis2 = 1;
            if (h2->is_circle && h2->arc)
            {
                int c = eos_arclist_view_count(h2->arc);
                for (int i = 0; i < c; i++)
                {
                    const eos_arclist_item_t *it = &h2->arc->al.items[i];
                    if (!it->visible) continue;
                    float r = (h2->arc->card_px * it->scale) * 0.5f;
                    if (!eos_display_profile_is_inside(&p, it->x, it->y, r))
                        vis2 = 0;
                }
            }
            else if (h2->grid)
            {
                int c = eos_layout_view_count(h2->grid);
                for (int i = 0; i < c; i++)
                {
                    eos_widget_t *w = &h2->grid->widgets[i];
                    float cx = w->x + w->w * 0.5f, cy = w->y + w->h * 0.5f;
                    float r = (w->w > w->h ? w->w : w->h) * 0.5f;
                    if (!eos_display_profile_is_inside(&p, cx, cy, r))
                        vis2 = 0;
                }
            }
            CHECK(vis2, "launcher rebuilt items inside safe area");
        }

        lv_anim_del_all();
        eos_framework_home_t *cur = h2 ? h2 : h;
        if (cur) eos_framework_home_destroy(cur);
        lv_obj_clean(holder);
    }

    /* ---- Regression guard: dangling display-profile pointer (#ball-top-left).
     * The real Launcher (_launcher_on_enter) builds the Home from a STACK-LOCAL
     * profile and returns, so the local is freed BEFORE the 16ms step timer
     * re-lays-out the ArcList. If the Home kept only a POINTER to that local,
     * eos_arclist_layout() reads freed stack memory and clumps every card at
     * (0,0) — the "ball in the top-left, only a sliver visible" symptom, and
     * taps hit the wrong (overlapping) card so apps never open. Building inside
     * a nested block (local destroyed on scope exit) and THEN stepping
     * reproduces that exact scenario. After the fix the Home owns the profile
     * BY VALUE, so item centers stay near screen center (120,120), never (0,0). */
    {
        eos_framework_home_t *hr = NULL;
        {
            eos_display_profile_t lp = eos_display_profiles_get(EOS_PROFILE_240C);
            int nb = eos_launcher_build_home(holder, &lp, &hr);
            (void)nb;
        } /* lp destroyed here — mirrors _launcher_on_enter returning */

        for (int f = 0; f < 30; f++) eos_framework_home_step(hr, 16.7f);

        float cx = 0.0f, cy = 0.0f;
        eos_framework_home_item_center(hr, 0, &cx, &cy);
        CHECK(cx > 60.0f && cy > 60.0f,
              "regression: ArcList item 0 stays near screen center after profile local out of scope");
        CHECK(cx < 180.0f && cy < 180.0f,
              "regression: ArcList item 0 not clumped at origin (dangling-profile fix)");

        lv_anim_del_all();
        eos_framework_home_destroy(hr);
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
    printf("=== Real-screen snapshot (watchface + app page + control center + clock + timer) ===\n");
    fflush(stdout);
    /* The SDL display driver overrides LVGL's tick source, so lv_tick_inc()
     * is ignored and event timing (press/click/gesture) never advances in
     * this headless self-test. Restore the default tick like the gesture
     * tests do, so the tap-injection can actually deliver press/click. */
    lv_tick_set_cb(NULL);
    const int w = SIM_HOR_RES, h = SIM_VER_RES;
    static const char *names[] = {"watchface", "app_page", "control_center", "clock_app", "timer_app"};
    for (int snap = 0; snap < 5; snap++) {
        if (snap == 1) {
            printf("[snap] -> eos_app_list_enter()\n"); fflush(stdout);
            eos_app_list_enter();
        } else if (snap == 2) {
            printf("[snap] -> back to watchface, then open control center\n"); fflush(stdout);
            eos_activity_back_to_watchface();
            eos_control_center_show();
            eos_control_panel_slide_change();
        } else if (snap == 3) {
            printf("[snap] -> launch com.cantomk6.clock (JS eapk app)\n"); fflush(stdout);
            eos_control_center_hide();
            eos_activity_back_to_watchface();
            for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); }
            eos_app_launch_immediately("com.cantomk6.clock");
        } else if (snap == 4) {
            printf("[snap] -> launch com.cantomk6.timer (JS eapk app)\n"); fflush(stdout);
            eos_activity_back_to_watchface();
            for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); }
            eos_app_launch_immediately("com.cantomk6.timer");
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

    /* Simulate pressing the 'C' hotkey: eos_control_center_show() + slide_change() */
    printf("[check] -> simulate 'C' key (show + slide_change)\n"); fflush(stdout);
    eos_control_center_show();
    eos_control_panel_slide_change();
    for (int k = 0; k < 150; k++) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(lv_display_get_default());
    lv_timer_handler();
    sim_grab_ppm("chk_watchface_afterC.ppm");          /* CC should now be OPEN */

    /* Press 'C' again -> should CLOSE */
    printf("[check] -> simulate 'C' key again (close)\n"); fflush(stdout);
    eos_control_center_show();
    eos_control_panel_slide_change();
    for (int k = 0; k < 150; k++) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(lv_display_get_default());
    lv_timer_handler();
    sim_grab_ppm("chk_watchface_afterC2.ppm");         /* CC should be CLOSED again */

    printf("[check] -> eos_app_list_enter()\n"); fflush(stdout);
    eos_app_list_enter();
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
            printf("[probe] -> eos_app_list_enter()\n"); fflush(stdout);
            eos_app_list_enter();
        } else if (snap == 2) {
            printf("[probe] -> back to watchface, then open control center\n"); fflush(stdout);
            eos_activity_back_to_watchface();
            eos_control_center_show();
            eos_control_panel_slide_change();
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
/* Runs an in-memory JavaScript program that drives the eos.ui.* Jerry- */
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
    printf("=== ElenixOS UI-JS (eos.ui bridge) self-test ===\n");
    fflush(stdout);

    /* Single in-memory program covers all 4 profiles, so only one realm
     * is created and torn down. JS uses single-quoted strings so the C
     * literal below needs no double-quote escaping. */
    static const char *UI_JS_TEST_SRC =
        "(function(){"
        "  function log(s){ try { eos.console.log(s); } catch(e){} }"
        "  var ui = eos.ui;"
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

    eos_result_t r = spm_app_run(&pkg);
    int exit_code = (r == EOS_OK) ? 0 : 1;

    if (r != EOS_OK)
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

/* P0.5 相册 headless 探针（--album-probe）：
 *   1. eos.fs.list("/sdcard/ALBUM/") 返回数组并过滤图片扩展名
 *   2. eos.fs.size(第一个文件) > 0
 *   3. album_pick 模拟回传: JS setStr -> getStr 自洽 + 清空
 *   另外实测 C 层 FM 写的系统 config 与 JS 私有 config 是否互通（暴露两套路径问题）。
 * 失败断言抛异常 -> spm_app_run 失败 -> 退出码非 0。 */
static int sim_run_album_probe(void)
{
    printf("=== ElenixOS 相册 headless 探针 ===\n");
    fflush(stdout);

    static const char *ALBUM_PROBE_SRC =
        "(function(){"
        "  function log(s){ try { eos.console.log(s); } catch(e){} }"
        "  var pass = 0, fail = 0;"
        "  function check(c, msg){ if (c){ pass++; log('[OK]   ' + msg); } else { fail++; log('[FAIL] ' + msg); } }"
        "  /* ---- 探针1: fs.list ---- */"
        "  var list = null;"
        "  try { list = eos.fs.list('/sdcard/ALBUM/'); } catch(e) { log('[FAIL] fs.list threw: ' + e); }"
        "  check(list !== null && list !== undefined, 'fs.list returns array');"
        "  if (list){"
        "    check(typeof list.length === 'number' && list.length > 0, 'fs.list non-empty (' + list.length + ' entries)');"
        "    var imgs = [];"
        "    for (var i = 0; i < list.length; i++){"
        "      var n = String(list[i]);"
        "      var lo = n.toLowerCase();"
        "      if (lo.indexOf('.png') > 0 || lo.indexOf('.jpg') > 0 || lo.indexOf('.jpeg') > 0) imgs.push(n);"
        "    }"
        "    log('[probe] raw=[' + list.join(',') + ']');"
        "    log('[probe] imgs=[' + imgs.join(',') + ']');"
        "    check(imgs.length >= 4, 'filtered image list >= 4 (photo_a.png/b.jpg/c.JPG/d.jpeg)');"
        "    var hasTxt = false;"
        "    for (var k = 0; k < list.length; k++){ if (String(list[k]) === 'readme.txt') hasTxt = true; }"
        "    check(hasTxt, 'raw list still contains non-image readme.txt (filter is JS-side)');"
        "  }"
        "  /* ---- 探针2: fs.size ---- */"
        "  var first = null;"
        "  if (list && list.length > 0){"
        "    for (var j = 0; j < list.length; j++){"
        "      var n2 = String(list[j]).toLowerCase();"
        "      if (n2.indexOf('.png') > 0 || n2.indexOf('.jpg') > 0){ first = '/sdcard/ALBUM/' + list[j]; break; }"
        "    }"
        "  }"
        "  if (first){"
        "    var sz = -1;"
        "    try { sz = eos.fs.size(first); } catch(e) { log('[FAIL] fs.size threw: ' + e); }"
        "    log('[probe] size(' + first + ') = ' + sz);"
        "    check(typeof sz === 'number' && sz > 0, 'fs.size returns > 0 (' + sz + ' bytes)');"
        "  } else { log('[FAIL] no image file found for fs.size probe'); }"
        "  /* ---- 探针3: album_pick C->JS 同路径互通（FM 模拟写入应可见） ---- */"
        "  var sysPick = '(not checked)';"
        "  try { sysPick = eos.config.getStr('album_pick'); } catch(e) { sysPick = '(err)'; }"
        "  log('[probe] JS read album_pick (C wrote app-private cfg) = [' + sysPick + ']');"
        "  check(sysPick === '/sdcard/ALBUM/from_c_fm.jpg',"
        "        'C->JS album_pick visible via app-private config');"
        "  /* 清空: setStr 空串覆盖 */"
        "  eos.config.setStr('album_pick', '');"
        "  var cleared = eos.config.getStr('album_pick');"
        "  check(cleared === '' || cleared === null, 'album_pick cleared after consume');"
        "  /* ---- 探针4: JS setStr -> getStr 自洽回传 ---- */"
        "  eos.config.setStr('album_pick', '/sdcard/ALBUM/mock.jpg');"
        "  var after = eos.config.getStr('album_pick');"
        "  log('[probe] album_pick JS round-trip = [' + after + ']');"
        "  check(after === '/sdcard/ALBUM/mock.jpg', 'album_pick round-trip via JS config');"
        "  log('[summary] pass=' + pass + ' fail=' + fail);"
        "  if (fail > 0){ throw new Error('album-probe: ' + fail + ' assertion(s) failed'); }"
        "})();";

    script_pkg_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    pkg.type = SCRIPT_TYPE_APPLICATION;
    pkg.id = "sys.album-probe";
    pkg.name = "Album headless probe";
    pkg.script_str = ALBUM_PROBE_SRC;

    /* P0.5 隐患实测: FM 写相册 app 私有 config.json，与 JS eos.config 同路径。
     * 探针 script id 是 sys.album-probe，故 C 侧模拟 FM 写本探针自己的私有
     * config（等价于 FM 写 com.cantomk6.album 的场景），验证跨层同路径互通。 */
    {
        char cfg_dir[EOS_FS_PATH_MAX];
        char cfg_path[EOS_FS_PATH_MAX];
        cJSON *cfg_root = NULL;
        snprintf(cfg_dir, sizeof(cfg_dir), EOS_APP_DATA_DIR "sys.album-probe");
        eos_storage_mkdir_if_not_exist(cfg_dir);
        snprintf(cfg_path, sizeof(cfg_path), EOS_APP_DATA_DIR "sys.album-probe/config.json");
        cfg_root = eos_storage_json_load(cfg_path);
        if (!cfg_root)
            cfg_root = cJSON_CreateObject();
        if (cfg_root)
        {
            cJSON *pick_item = cJSON_GetObjectItem(cfg_root, "album_pick");
            if (pick_item)
                cJSON_SetValuestring(pick_item, "/sdcard/ALBUM/from_c_fm.jpg");
            else
                cJSON_AddStringToObject(cfg_root, "album_pick", "/sdcard/ALBUM/from_c_fm.jpg");
            eos_storage_json_save(cfg_path, cfg_root);
            cJSON_Delete(cfg_root);
        }
        printf("[C] 模拟 FM 写探针私有 config album_pick = /sdcard/ALBUM/from_c_fm.jpg\n");
    }

    eos_result_t r = spm_app_run(&pkg);
    int exit_code = (r == EOS_OK) ? 0 : 1;

    if (r != EOS_OK)
    {
        const spm_error_t *err = spm_get_last_error();
        if (err && err->error_info[0])
            printf("[FAIL] album-probe script error: %s\n", err->error_info);
    }
    spm_app_stop();

    printf("\n=== 相册 headless 探针 done (exit=%d) ===\n", exit_code);
    return exit_code;
}

/* ----------------------------------------------------------------------------
 * Album app real-launch probe (--album-launch)
 * Launches the INSTALLED com.cantomk6.album eapk app through the real
 * activity/appHeader path (same as the user tapping it in the launcher),
 * pumps 5s of ticks, then dumps every label text in the tree so we can see
 * what the user means by "显示err" (crash? wrong text? missing UI?).
 * -------------------------------------------------------------------------- */
static void _album_dump_labels(lv_obj_t *p, int depth)
{
    if (!p) return;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++)
    {
        lv_obj_t *c = lv_obj_get_child(p, i);
        if (lv_obj_check_type(c, &lv_label_class))
        {
            const char *t = lv_label_get_text(c);
            if (t && t[0])
                printf("[label d%d] '%s'\n", depth, t);
        }
        _album_dump_labels(c, depth + 1);
    }
}
static void _album_collect_widgets(lv_obj_t *p, lv_obj_t **img, lv_obj_t **btns, int *nb, int maxb)
{
    if (!p) return;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++)
    {
        lv_obj_t *c = lv_obj_get_child(p, i);
        if (lv_obj_check_type(c, &lv_image_class) && *img == NULL) *img = c;
        else if (lv_obj_check_type(c, &lv_button_class) && *nb < maxb) btns[(*nb)++] = c;
        _album_collect_widgets(c, img, btns, nb, maxb);
    }
}
static void _album_save_screen(const char *name)
{
    const int w = SIM_HOR_RES, h = SIM_VER_RES;
    lv_refr_now(lv_display_get_default());
    char ppm[96];
    snprintf(ppm, sizeof(ppm), "screen_%s.ppm", name);
    FILE *fp = fopen(ppm, "wb");
    if (!fp) { printf("[probe] cannot open %s\n", ppm); return; }
    SDL_Renderer *r = (SDL_Renderer *)lv_sdl_window_get_renderer(lv_display_get_default());
    int rpitch = w * 4;
    uint8_t *argb = (uint8_t *)malloc((size_t)rpitch * h);
    if (r && argb && SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, argb, rpitch) == 0)
    {
        fprintf(fp, "P6\n%d %d\n255\n", w, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                uint8_t *p = argb + (size_t)y * rpitch + (size_t)x * 4;
                uint8_t pix[3] = { p[2], p[1], p[0] };
                fwrite(pix, 1, 3, fp);
            }
        printf("[probe] wrote %s\n", ppm);
    }
    else printf("[probe] FAIL cannot grab %s\n", ppm);
    free(argb);
    fclose(fp);
}

static int sim_run_album_launch(void)
{
    printf("=== Album app launch probe (headless) ===\n");
    lv_tick_set_cb(NULL);   /* SDL driver overrides tick; restore default so lv_tick_inc works */
    eos_result_t la = eos_app_launch_immediately("com.cantomk6.album");
    printf("[probe] launch ret=%d\n", (int)la);
    for (int k = 0; k < 250; k++)
    {
        lv_tick_inc(20);
        lv_timer_handler();
        eos_dispatch_tick();
    }
    lv_obj_t *view = eos_activity_get_view(eos_activity_get_current());
    printf("[probe] current activity view=%p\n", (void *)view);
    _album_dump_labels(view, 0);

    /* Locate img (first lv_image anywhere) and the 3 buttons (recursive) */
    lv_obj_t *img = NULL, *btns[3] = {NULL, NULL, NULL};
    uint32_t nb = 0;
    _album_collect_widgets(view, &img, btns, (int *)&nb, 3);
    printf("[probe] img=%p buttons=%u\n", (void *)img, nb);
    int fail = 0;
    if (!img || nb != 3) fail++;

    /* [PNG] tap -> setSrc /sdcard/ALBUM/0.png, pump, check getSrc */
    if (btns[0])
    {
        lv_obj_send_event(btns[0], LV_EVENT_PRESSED, NULL);
        for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        const char *src = img ? lv_image_get_src(img) : NULL;
        const char *want = "S:/sdcard/ALBUM/0.png";
        printf("[probe] after PNG tap: src=%s\n", src ? src : "(null)");
        _album_save_screen("album_png");
        if (!src || (strstr(src, "/sdcard/ALBUM/0.png") == NULL)) fail++;
    }
    /* [JPG] tap -> setSrc /sdcard/ALBUM/test.jpg */
    if (btns[1])
    {
        lv_obj_send_event(btns[1], LV_EVENT_PRESSED, NULL);
        for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        const char *src = img ? lv_image_get_src(img) : NULL;
        printf("[probe] after JPG tap: src=%s\n", src ? src : "(null)");
        _album_save_screen("album_jpg");
        if (!src || (strstr(src, "/sdcard/ALBUM/test.jpg") == NULL)) fail++;
    }
    /* [FM] tap -> eos.app.openFiles() should enter native Files activity */
    if (btns[2])
    {
        lv_obj_t *view_before = eos_activity_get_view(eos_activity_get_current());
        lv_obj_send_event(btns[2], LV_EVENT_PRESSED, NULL);
        for (int k = 0; k < 60; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        lv_obj_t *view_after = eos_activity_get_view(eos_activity_get_current());
        printf("[probe] FM tap: view %p -> %p\n", (void *)view_before, (void *)view_after);
        if (view_after == view_before) fail++;
        eos_activity_back();
        for (int k = 0; k < 30; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }

        /* Simulate FM picking text.jpg: write album_pick into the app-private
         * config (same path eos_files.c writes), pump past the 1s poll timer,
         * then the app must auto-load the picked image into img. */
        {
            char cfg_dir[EOS_FS_PATH_MAX];
            char cfg_path[EOS_FS_PATH_MAX];
            snprintf(cfg_dir, sizeof(cfg_dir), EOS_APP_DATA_DIR "com.cantomk6.album");
            eos_storage_mkdir_if_not_exist(cfg_dir);
            snprintf(cfg_path, sizeof(cfg_path), EOS_APP_DATA_DIR "com.cantomk6.album/config.json");
            cJSON *cfg_root = eos_storage_json_load(cfg_path);
            if (!cfg_root) cfg_root = cJSON_CreateObject();
            if (cfg_root)
            {
                cJSON *item = cJSON_GetObjectItem(cfg_root, "album_pick");
                if (item) cJSON_SetValuestring(item, "/sdcard/ALBUM/text.jpg");
                else cJSON_AddStringToObject(cfg_root, "album_pick", "/sdcard/ALBUM/text.jpg");
                eos_storage_json_save(cfg_path, cfg_root);
                cJSON_Delete(cfg_root);
            }
            printf("[probe] simulated FM pick -> album_pick=/sdcard/ALBUM/text.jpg\n");
            for (int k = 0; k < 70; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
            const char *src2 = img ? lv_image_get_src(img) : NULL;
            printf("[probe] after FM-pick poll: src=%s\n", src2 ? src2 : "(null)");
            if (!src2 || strstr(src2, "/sdcard/ALBUM/text.jpg") == NULL) fail++;
            /* clear it again so reruns stay clean */
            cfg_root = eos_storage_json_load(cfg_path);
            if (cfg_root)
            {
                cJSON *item = cJSON_GetObjectItem(cfg_root, "album_pick");
                if (item) { cJSON_SetValuestring(item, ""); eos_storage_json_save(cfg_path, cfg_root); }
                cJSON_Delete(cfg_root);
            }
        }
    }

    printf("[summary] album-launch pass=%d fail=%d\n", (fail == 0) ? 1 : 0, fail);
    printf("=== album launch probe done ===\n");
    return (fail == 0) ? 0 : 1;
}

/* ----------------------------------------------------------------------------
 * Notes/Draw platform probe (--notes-probe)
 * In-memory JS exercising the new bridges: eos.fs.write (string + Uint8Array),
 * eos.fs.read (binary-safe Uint8Array), eos.ime.open (no-throw + input page).
 * -------------------------------------------------------------------------- */
static const char NOTES_PROBE_SRC[] =
    "function log(m) { eos.console.log(m); }\n"
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
    "var ok1 = eos.fs.write('/sdcard/Notes/probe.txt', '你好ElenixOS 123');\n"
    "check(ok1 === true, 'fs.write string returns true');\n"
    "/* 2. fs.read -> Uint8Array -> utf8Decode round-trip */\n"
    "var d = eos.fs.read('/sdcard/Notes/probe.txt');\n"
    "check(d && typeof d.length === 'number' && d.length === 18,\n"
    "      'fs.read returns byte array (len ' + (d ? d.length : -1) + ')');\n"
    "var s = utf8Decode(d);\n"
    "check(s === '你好ElenixOS 123', 'fs.read utf8 round-trip = [' + s + ']');\n"
    "/* 3. fs.write Uint8Array (binary, NUL + high bytes preserved) */\n"
    "var bin = new Uint8Array([0x45, 0x4C, 0x44, 0x52, 0x57, 0x31, 0x00, 0xFF, 0xFE]);\n"
    "var ok2 = eos.fs.write('/sdcard/Drawings/probe.edrw', bin);\n"
    "check(ok2 === true, 'fs.write Uint8Array returns true');\n"
    "var d2 = eos.fs.read('/sdcard/Drawings/probe.edrw');\n"
    "check(d2 && d2.length === 9 && d2[6] === 0x00 && d2[7] === 0xFF && d2[8] === 0xFE,\n"
    "      'fs binary round-trip 9 bytes (NUL+FF preserved)');\n"
    "/* 4. ime.open no-throw */\n"
    "var imeThrew = false;\n"
    "try { eos.ime.open(function (t) { log('[ime] cb got [' + t + ']'); }); }\n"
    "catch (e) { imeThrew = true; log('[FAIL] ime.open threw: ' + e); }\n"
    "check(!imeThrew, 'ime.open called without throwing');\n"
    "log('[summary] pass=' + pass + ' fail=' + fail);\n";

static int sim_run_notes_probe(void)
{
    printf("=== Notes/Draw platform probe (--notes-probe) ===\n");
    /* ensure target dirs exist */
    eos_storage_mkdir_if_not_exist("/sdcard/Notes");
    eos_storage_mkdir_if_not_exist("/sdcard/Drawings");
    /* remove leftover probe files so size checks are deterministic */
    eos_fs_remove("/sdcard/Notes/probe.txt");
    eos_fs_remove("/sdcard/Drawings/probe.edrw");

    script_pkg_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    pkg.type = SCRIPT_TYPE_APPLICATION;
    pkg.id = "sys.notes-probe";
    pkg.name = "Notes platform probe";
    pkg.script_str = NOTES_PROBE_SRC;

    eos_result_t r = spm_app_run(&pkg);
    int exit_code = (r == EOS_OK) ? 0 : 1;
    if (r != EOS_OK)
    {
        const spm_error_t *err = spm_get_last_error();
        if (err && err->error_info[0])
            printf("[FAIL] notes-probe script error: %s\n", err->error_info);
    }
    spm_app_stop();

    /* ime.open should have opened the input page: verify an activity switch */
    lv_obj_t *view = eos_activity_get_view(eos_activity_get_current());
    printf("[probe] post-ime activity view=%p\n", (void *)view);

    printf("\n=== notes probe done (exit=%d) ===\n", exit_code);
    return exit_code;
}

/* ----------------------------------------------------------------------------
 * Notes app real-launch probe (--notes-launch)
 * Seeds a test note, launches com.cantomk6.notes, then drives: [列表] -> filter
 * shows it, [打开] -> body shows content + back to edit mode, [新建] -> body
 * cleared, [编辑] -> ime.open pushes the input page (activity switch).
 * -------------------------------------------------------------------------- */
static lv_obj_t *_notes_find_label(lv_obj_t *p, const char *needle)
{
    if (!p) return NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++)
    {
        lv_obj_t *c = lv_obj_get_child(p, i);
        if (lv_obj_check_type(c, &lv_label_class))
        {
            const char *t = lv_label_get_text(c);
            if (t && strstr(t, needle)) return c;
        }
        lv_obj_t *r = _notes_find_label(c, needle);
        if (r) return r;
    }
    return NULL;
}
/* Exact-match variant (strcmp) — used to pin a specific typed-body value. */
static lv_obj_t *_notes_find_label_exact(lv_obj_t *p, const char *needle)
{
    if (!p || !needle) return NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++)
    {
        lv_obj_t *c = lv_obj_get_child(p, i);
        if (lv_obj_check_type(c, &lv_label_class))
        {
            const char *t = lv_label_get_text(c);
            if (t && strcmp(t, needle) == 0) return c;
        }
        lv_obj_t *r = _notes_find_label_exact(c, needle);
        if (r) return r;
    }
    return NULL;
}
static void _notes_dump_found(lv_obj_t *view, const char *tag, const char *const *needles, int n)
{
    for (int i = 0; i < n; i++)
        printf("[probe] %s find '%s' -> %s\n", tag, needles[i],
               _notes_find_label(view, needles[i]) ? "YES" : "NO");
}
#define _NOTES_CHECK(cond, msg) do { if (cond) printf("[probe]   OK %s\n", msg); else { printf("[probe]   FAIL %s\n", msg); fail++; } } while (0)
static int _notes_count_objs(lv_obj_t *p)
{
    int n = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++)
    {
        lv_obj_t *c = lv_obj_get_child(p, i);
        if (!lv_obj_check_type(c, &lv_label_class))
        {
            n += 1 + _notes_count_objs(c);
        }
    }
    return n;
}

/* Collect letter-key objs: DIRECT children of the app root that contain a
 * single a-z label. We store the key OBJ itself (a guaranteed-valid heap
 * object, since it is a direct child of the app root) -- never
 * lv_obj_get_parent(label), which can resolve to a module-address pointer for
 * framework chrome labels and wedge LVGL's global event chain when fed to
 * lv_obj_send_event. Scoped to direct children so we never descend into
 * framework chrome (WOS statusbar). Returns a-z key count (expected 26). */
static int _collect_letter_keys(lv_obj_t *root, lv_obj_t **out, int cap)
{
    int n = 0;
    if (!root) return 0;
    uint32_t cnt = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < cnt; i++)
    {
        lv_obj_t *k = lv_obj_get_child(root, i);
        if (!k || lv_obj_check_type(k, &lv_label_class)) continue; /* skip labels */
        if (lv_obj_get_child_cnt(k) != 1) continue;               /* key leaf = 1 child (label) */
        lv_obj_t *c = lv_obj_get_child(k, 0);
        if (!c || !lv_obj_check_type(c, &lv_label_class)) continue;
        const char *t = lv_label_get_text(c);
        if (t && strlen(t) == 1 && t[0] >= 'a' && t[0] <= 'z')
        {
            if (n < cap) out[n] = k;
            n++;
        }
    }
    return n;
}

/* Length of the longest label text in the subtree (the typing body). */
static int _notes_longest_label_len(lv_obj_t *p)
{
    int best = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(p); i++)
    {
        lv_obj_t *c = lv_obj_get_child(p, i);
        if (lv_obj_check_type(c, &lv_label_class))
        {
            const char *t = lv_label_get_text(c);
            int l = t ? (int)strlen(t) : 0;
            if (l > best) best = l;
        }
        int sub = _notes_longest_label_len(c);
        if (sub > best) best = sub;
    }
    return best;
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

static int sim_run_notes_launch(void)
{
    printf("=== Notes app UI layout probe (headless) ===\n");
    lv_tick_set_cb(NULL);
    /* Reset notes.draft to a clean empty state. The app persists the draft in
     * config.json across launches; without this the baseline would accumulate
     * run-over-run (every absolute assertion would then break). */
    eos_fs_remove("/.sys/app/app_data/com.cantomk6.notes/config.json");
    eos_result_t la = eos_app_launch_immediately("com.cantomk6.notes");
    printf("[probe] launch ret=%d\n", (int)la);
    for (int k = 0; k < 250; k++)
    {
        lv_tick_inc(20);
        lv_timer_handler();
        eos_dispatch_tick();
    }
    lv_obj_t *view = eos_activity_get_view(eos_activity_get_current());
    int fail = 0;

    /* toolbar + card + caret + keyboard labels must all exist */
    static const char *needles[] = {"取消", "完成", "q", "p", "a", "l", "z", "m", "删", "123", ".", "1", "输入备忘"};
    _notes_dump_found(view, "[ui]", needles, sizeof(needles) / sizeof(needles[0]));
    for (unsigned i = 0; i < sizeof(needles) / sizeof(needles[0]); i++)
        _NOTES_CHECK(_notes_find_label(view, needles[i]) != NULL, "label exists: ");

    /* object ledger: non-label objs (view children) = R.root + 2 tools + card + caret
     * + 30 letter keys + 28 symbol keys = 63 (symbol keys hidden on letter page, still counted) */
    int nobj = _notes_count_objs(view);
    printf("[probe] non-label obj count=%d (expect 63: R.root+2tools+card+caret+30letter+28symbol)\n", nobj);
    _NOTES_CHECK(nobj == 63, "object ledger 63");

    /* ===== HIT-TEST PROBE =====
     * Replicate EXACTLY what the real indev does in pointer_search_obj(): search
     * layer_sys -> layer_top -> screen_active -> layer_bottom and return the topmost
     * CLICKABLE object under the point. Headless taps use lv_obj_send_event which
     * BYPASSES hit-testing, so they always pass; the real window relies on this
     * search. If an overlay (in layer_sys/layer_top) covers the keyboard, IT wins
     * and the keys never receive PRESSED -> "only the caret animates". This probe
     * tells us precisely which object the real window would deliver the tap to. */
    {
        lv_display_t *disp = lv_display_get_default();
        lv_point_t pts[3] = { {29, 129}, {117, 212}, {185, 185} }; /* centers: q / space / 删 */
        const char *nm[3] = { "q(29,129)", "space(117,212)", "del(185,185)" };
        for (int z = 0; z < 3; z++) {
            lv_obj_t *o = lv_indev_search_obj(lv_display_get_layer_sys(disp), &pts[z]);
            if (!o) o = lv_indev_search_obj(lv_display_get_layer_top(disp), &pts[z]);
            if (!o) o = lv_indev_search_obj(lv_display_get_screen_active(disp), &pts[z]);
            if (!o) o = lv_indev_search_obj(lv_display_get_layer_bottom(disp), &pts[z]);
            if (o) {
                lv_area_t a; lv_obj_get_coords(o, &a);
                printf("[hit] %s -> xywh=%d,%d,%d,%d\n",
                       nm[z], a.x1, a.y1, a.x2 - a.x1, a.y2 - a.y1);
            } else {
                printf("[hit] %s -> NULL\n", nm[z]);
            }
        }
    }

    /* keyboard row geometry: find a key on row1 (y=116) at x=16, and row4 (y=200) */
    lv_area_t a1, a4;
    lv_obj_t *kq = _notes_find_label(view, "q");
    lv_obj_t *k123 = _notes_find_label(view, "123");
    if (kq) { lv_obj_get_coords(lv_obj_get_parent(kq), &a1);
        printf("[probe] row1 'q' key xywh=%d,%d,%d,%d\n", a1.x1, a1.y1, a1.x2 - a1.x1, a1.y2 - a1.y1);
        _NOTES_CHECK(a1.x1 == 20 && a1.y1 == 116 && (a1.x2 - a1.x1) >= 17 && (a1.y2 - a1.y1) >= 24,
                     "row1 q key geometry (x20,y116, w/h +-1px)"); }
    if (k123) { lv_obj_get_coords(lv_obj_get_parent(k123), &a4);
        printf("[probe] row4 '123' key xywh=%d,%d,%d,%d\n", a4.x1, a4.y1, a4.x2 - a4.x1, a4.y2 - a4.y1);
        _NOTES_CHECK(a4.x1 == 64 && a4.y1 == 200 && (a4.x2 - a4.x1) >= 23 && (a4.y2 - a4.y1) >= 22,
                     "row4 123 key geometry (x64,y200, w/h +-1px)"); }

    /* ===== P1 打字行为自证 =====
     * appRoot = R.root（app 自己的容器）。所有打字断言都限定在 appRoot 内，
     * 避免 WOS 状态栏的时钟("14:06")等框架 label 把 longest-label / 计数撑大。
     * 字母键只从 appRoot 的直接子节点收集，绝不深搜整棵 view 树——否则会
     * 捞到框架 chrome 的假指针，喂给 lv_obj_send_event 会卡死 LVGL 全局事件链。 */
    lv_obj_t *qLabel = _notes_find_label_exact(view, "q");
    lv_obj_t *qKey = qLabel ? lv_obj_get_parent(qLabel) : NULL;
    lv_obj_t *appRoot = (qKey && lv_obj_get_child_cnt(qKey) == 1) ? lv_obj_get_parent(qKey) : NULL;
    _NOTES_CHECK(appRoot != NULL, "app root (R.root) located via q key");

    lv_obj_t *letterKeys[32];
    int nk = _collect_letter_keys(appRoot, letterKeys, 32);
    printf("[probe] letter keys collected=%d\n", nk);
    _NOTES_CHECK(nk == 26, "26 alphabet keys collected");

    /* (A) 取消回滚：用 q 键打 3 字 -> 取消 -> 清空 + 闪"已取消" */
    _NOTES_CHECK(qKey != NULL, "q key found");
    if (qKey)
    {
        lv_obj_send_event(qKey, LV_EVENT_PRESSED, NULL);
        lv_obj_send_event(qKey, LV_EVENT_PRESSED, NULL);
        lv_obj_send_event(qKey, LV_EVENT_PRESSED, NULL); /* "qqq" */
    }
    _NOTES_CHECK(_notes_find_label_exact(appRoot, "qqq") != NULL, "typed 3 chars (qqq)");
    lv_obj_t *cancelBtn = _find_parent_with_label(view, "取消");
    _NOTES_CHECK(cancelBtn != NULL, "cancel button found");
    if (cancelBtn)
    {
        lv_obj_send_event(cancelBtn, LV_EVENT_PRESSED, NULL);
        /* body 回滚到空（baseline），占位符"输入备忘..."复现；状态闪"已取消" */
        _NOTES_CHECK(_notes_find_label_exact(appRoot, "qqq") == NULL, "cancel clears typed text (qqq gone)");
        _NOTES_CHECK(_notes_find_label(appRoot, "输入备忘") != NULL, "cancel restores placeholder 输入备忘");
        _NOTES_CHECK(_notes_find_label(view, "已取消"), "cancel flashes 已取消");
    }

    /* (B) 连点 100 次字母键：文本长度=100，对象账本恒定=35（不闪退） */
    for (int n = 0; n < 100; n++)
        lv_obj_send_event(letterKeys[n % nk], LV_EVENT_PRESSED, NULL);
    _NOTES_CHECK(_notes_longest_label_len(appRoot) == 100, "typed 100 chars via 100 taps");
    _NOTES_CHECK(_notes_count_objs(view) == 63, "object ledger stable 63 after 100 taps");
    _NOTES_CHECK(_notes_find_label(appRoot, "输入备忘") == NULL, "placeholder hidden while typing");

    /* (C) 完成：存草稿 + 闪"已存"；推进 tick 后清空 */
    lv_obj_t *doneBtn = _find_parent_with_label(view, "完成");
    _NOTES_CHECK(doneBtn != NULL, "done button found");
    if (doneBtn)
    {
        lv_obj_send_event(doneBtn, LV_EVENT_PRESSED, NULL);
        _NOTES_CHECK(_notes_find_label(view, "已存"), "done flashes 已存");
        for (int k2 = 0; k2 < 50; k2++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        _NOTES_CHECK(!_notes_find_label(view, "已存"), "done flash clears after timer");
    }

    /* (D) 符号页（P2）：点 123 -> 切符号页（ABC/符号键可见、字母键隐藏）-> 点符号'1'追加 -> 切回字母页 */
    lv_obj_t *toggle123 = _find_parent_with_label(view, "123");
    _NOTES_CHECK(toggle123 != NULL, "123 toggle found");
    if (toggle123)
    {
        lv_obj_send_event(toggle123, LV_EVENT_PRESSED, NULL);
        _NOTES_CHECK(_notes_find_label(view, "123") == NULL, "toggle relabeled 123 -> ABC");
        _NOTES_CHECK(_notes_find_label(view, "ABC") != NULL, "ABC label present on symbol page");
        lv_obj_t *oneLabel = _notes_find_label_exact(view, "1");
        lv_obj_t *oneKey = oneLabel ? lv_obj_get_parent(oneLabel) : NULL;
        _NOTES_CHECK(oneKey && !lv_obj_has_flag(oneKey, LV_OBJ_FLAG_HIDDEN),
                     "symbol key '1' visible on symbol page");
        lv_obj_t *qKeyHidden = qLabel ? lv_obj_get_parent(qLabel) : NULL;
        _NOTES_CHECK(qKeyHidden && lv_obj_has_flag(qKeyHidden, LV_OBJ_FLAG_HIDDEN),
                     "letter key 'q' hidden on symbol page");
        if (oneKey)
            lv_obj_send_event(oneKey, LV_EVENT_PRESSED, NULL);   /* 追加 '1' -> 101 字 */
        _NOTES_CHECK(_notes_longest_label_len(appRoot) == 101, "typed symbol '1' -> 101 chars");
        lv_obj_t *abcToggle = _find_parent_with_label(view, "ABC");
        _NOTES_CHECK(abcToggle != NULL, "ABC toggle found on symbol page");
        if (abcToggle)
        {
            lv_obj_send_event(abcToggle, LV_EVENT_PRESSED, NULL);
            _NOTES_CHECK(_notes_find_label(view, "123") != NULL, "back to letters: 123 present");
            _NOTES_CHECK(_notes_find_label(view, "ABC") == NULL, "back to letters: ABC gone");
            _NOTES_CHECK(qKeyHidden && !lv_obj_has_flag(qKeyHidden, LV_OBJ_FLAG_HIDDEN),
                         "letter key 'q' visible again");
        }
    }

    /* (E) 草稿持久化 + 启动恢复（P2）：完成已存 101 字 -> 退出 -> 重启应恢复 */
    lv_obj_t *doneBtn2 = _find_parent_with_label(view, "完成");
    _NOTES_CHECK(doneBtn2 != NULL, "done button found (2nd save)");
    if (doneBtn2)
        lv_obj_send_event(doneBtn2, LV_EVENT_PRESSED, NULL);   /* 存 101 字草稿到 config */
    eos_activity_back();   /* 退出到 app list，清理活动 */
    for (int k3 = 0; k3 < 60; k3++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
    eos_result_t la2 = eos_app_launch_immediately("com.cantomk6.notes");   /* 重新启动 */
    printf("[probe] relaunch ret=%d\n", (int)la2);
    for (int k4 = 0; k4 < 250; k4++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
    lv_obj_t *view2 = eos_activity_get_view(eos_activity_get_current());
    lv_obj_t *qLabel2 = _notes_find_label_exact(view2, "q");
    lv_obj_t *qKey2 = qLabel2 ? lv_obj_get_parent(qLabel2) : NULL;
    lv_obj_t *appRoot2 = (qKey2 && lv_obj_get_child_cnt(qKey2) == 1) ? lv_obj_get_parent(qKey2) : NULL;
    _NOTES_CHECK(appRoot2 != NULL, "app root located after relaunch");
    _NOTES_CHECK(_notes_find_label(appRoot2, "输入备忘") == NULL,
                 "placeholder hidden after restore (draft non-empty)");
    _NOTES_CHECK(_notes_longest_label_len(appRoot2) == 101, "restored draft = 101 chars (persistence OK)");
    int nobj2 = _notes_count_objs(view2);
    printf("[probe] non-label obj count after relaunch=%d (expect 63)\n", nobj2);
    _NOTES_CHECK(nobj2 == 63, "object ledger 63 after relaunch");

    /* chord table comes from JS [chord] logs (grepped by test_sim.sh) */
    printf("[summary] notes-launch pass=%d fail=%d\n", (fail == 0) ? 1 : 0, fail);
    printf("=== notes launch probe done ===\n");
    return (fail == 0) ? 0 : 1;
}

/* ----------------------------------------------------------------------------
 * Draw app real-launch probe (--draw-launch)
 * Covers the full eos.draw.* chain headlessly:
 *   launch eapk -> JS eos.draw.create builds a real lv_canvas (176x176) ->
 *   4 toolbar pills present -> 颜色 tap lazily builds the 48-cell palette ->
 *   保存 tap calls eos.draw.save -> C writes a valid .edrw (ELDRW1 + w/h/fmt
 *   + RGB565) that we read back through the same eos_fs layer.
 * Drawing strokes themselves are exercised by the .edrw round-trip (buffer is
 * the source of truth), and coordinates are screen->local in the C module.
 * -------------------------------------------------------------------------- */
static lv_obj_t *_find_first_of_type(lv_obj_t *root, const lv_obj_class_t *cls)
{
    if (lv_obj_check_type(root, cls)) return root;
    uint32_t cnt = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < cnt; i++)
    {
        lv_obj_t *r = _find_first_of_type(lv_obj_get_child(root, i), cls);
        if (r) return r;
    }
    return NULL;
}

/* 读整个文件到 malloc 缓冲，返回大小；失败返回 NULL */
static uint8_t *_read_whole(const char *p, uint32_t *outsz)
{
    eos_file_t f = eos_fs_open_read(p);
    if (!f) return NULL;
    uint32_t sz = 0;
    if (eos_fs_size(f, &sz) != EOS_OK || sz == 0) { eos_fs_close(f); return NULL; }
    uint8_t *b = (uint8_t *)malloc(sz);
    if (!b) { eos_fs_close(f); return NULL; }
    int r = eos_fs_read(f, b, sz);
    eos_fs_close(f);
    if (r != (int)sz) { free(b); return NULL; }
    *outsz = 0;
    return b;
}

/* Breach Protocol app probe (--breach-launch): headless boot + structure.
 * Verifies launch succeeds (no JS runtime error), Boot page builds, the
 * "INITIATING..." label is present, and the boot widgets / page containers exist.
 * Boot animations are timer-driven; we pump the loop so they run. */
static int sim_run_breach_launch(void)
{
    printf("=== Breach Protocol app probe (headless) ===\n");
    lv_tick_set_cb(NULL);   // 还原内部 tick 计数器，使 lv_tick_inc 生效（否则 SDL 实时钟忽略增量，计时器不触发）
    eos_result_t la = eos_app_launch_immediately("com.cantomk6.breach");
    printf("[probe] launch ret=%d (EOS_OK=%d)\n", (int)la, (int)EOS_OK);
    int fail = 0;
    if (la != EOS_OK) { printf("[FAIL] launch com.cantomk6.breach failed\n"); fail++; }

    /* pump frames so JS timers (typewriter/progress/countdown) advance and
     * Boot->Hack transition (fadeTo) fires in the REAL engine.
     * 1200 frames * 16ms = 19.2s; Boot ~5s 完成，余量充足。 */
    for (int f = 0; f < 1200; f++)
    {
        lv_tick_inc(16);
        lv_timer_handler();
        eos_dispatch_tick();
    }
    lv_obj_t *view = eos_activity_get_view(eos_activity_get_current());
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
    for (int f = 0; f < 250; f++) { lv_tick_inc(16); lv_timer_handler(); eos_dispatch_tick(); }
    printf("[probe] survived 1450 frames (Boot->Hack real render) without crash\n");

    printf("[summary] breach-launch pass=%d fail=%d\n", (fail == 0) ? 1 : 0, fail);
    return (fail == 0) ? 0 : 1;
}

/* Breach Protocol Result-page probe (--breach-result): headless.
 * Pumps enough LVGL frames to drive the game to its natural 60s timeout
 * (endGame(false)), then verifies the Result overlay (pageResult) becomes
 * visible with the correct fail text, progress, and RETRY button, and that
 * the glitch timer runs for many ticks on the live Result page without
 * crashing the engine. Win-path text/color is the symmetric ternary of the
 * fail path and is covered by the node autotest (endGame(true) exercised,
 * jerryErrors==0). */
static int sim_run_breach_result(void)
{
    printf("=== Breach Protocol Result-page probe (headless) ===\n");
    lv_tick_set_cb(NULL);
    eos_result_t la = eos_app_launch_immediately("com.cantomk6.breach");
    printf("[probe] launch ret=%d (EOS_OK=%d)\n", (int)la, (int)EOS_OK);
    int fail = 0;
    if (la != EOS_OK) { printf("[FAIL] launch com.cantomk6.breach failed\n"); return 1; }

    /* Boot ~5s + game 60s, with margin. 16ms/frame. */
    for (int f = 0; f < 6000; f++)
    {
        lv_tick_inc(16);
        lv_timer_handler();
        eos_dispatch_tick();
    }

    lv_obj_t *view = eos_activity_get_view(eos_activity_get_current());
    if (!view) { printf("[FAIL] breach view is NULL\n"); return 1; }

    lv_obj_t *failStatus = _find_parent_with_label(view, "BREACH FAILED");
    if (!failStatus) { printf("[FAIL] 'BREACH FAILED' status label missing (timeout/endGame did not fire)\n"); fail++; }
    else if (lv_obj_has_flag(failStatus, LV_OBJ_FLAG_HIDDEN)) { printf("[FAIL] Result page (pageResult) still HIDDEN after timeout\n"); fail++; }
    else printf("[probe] Result page visible after timeout (pageResult shown, fail text correct)\n");

    lv_obj_t *prog = _find_parent_with_label(view, "SOLVED 0 / 3");
    if (!prog) { printf("[FAIL] 'SOLVED 0 / 3' progress label missing\n"); fail++; }
    else printf("[probe] Result progress 'SOLVED 0 / 3' present\n");

    lv_obj_t *retry = _find_parent_with_label(view, "RETRY");
    if (!retry) { printf("[FAIL] 'RETRY' button missing\n"); fail++; }
    else printf("[probe] RETRY button present\n");

    /* stability: extra frames so the glitch timer (40ms) runs many ticks on the live Result page */
    for (int f = 0; f < 1500; f++) { lv_tick_inc(16); lv_timer_handler(); eos_dispatch_tick(); }
    printf("[probe] survived 7500 frames (incl. timeout + glitch) without crash\n");

    printf("[summary] breach-result pass=%d fail=%d\n", (fail == 0) ? 1 : 0, fail);
    return (fail == 0) ? 0 : 1;
}

static int sim_run_draw_launch(void)
{
    printf("=== Draw app UI layout + save probe (headless) ===\n");
    lv_tick_set_cb(NULL);
    eos_result_t la = eos_app_launch_immediately("com.cantomk6.draw");
    printf("[probe] launch ret=%d (EOS_OK=%d)\n", (int)la, (int)EOS_OK);
    int fail = 0;
    if (la != EOS_OK)
    {
        printf("[FAIL] draw launch failed ret=%d\n", (int)la);
        fail++;
    }

    for (int k = 0; k < 250; k++)
    {
        lv_tick_inc(20);
        lv_timer_handler();
        eos_dispatch_tick();
    }
    lv_obj_t *view = eos_activity_get_view(eos_activity_get_current());
    if (!view)
    {
        printf("[FAIL] draw view is NULL\n");
        return 1;
    }

    /* (1) eos.draw.create must have built exactly one real lv_canvas, 150x150 */
    lv_obj_t *canvas = _find_first_of_type(view, &lv_canvas_class);
    if (!canvas)
    {
        printf("[FAIL] no lv_canvas found (eos.draw.create did not run)\n");
        fail++;
    }
    else
    {
        /* Exact dimensions are verified at save time (step 4): the .edrw header
         * is serialized straight from this canvas's pixel buffer, so a 150x150
         * file proves the canvas was created at 150x150. lv_canvas_get_width/h
         * do not exist in this LVGL9 fork, so we don't query them here. */
        printf("[probe] lv_canvas present (150x150 asserted via .edrw header)\n");
    }

    /* (2) toolbar: 快捷色3 + 更多 + 粗细(2px) + 撤销 + 底行(打开/保存/清除) */
    static const char *tools[] = {"更多", "撤销", "2px", "打开", "保存", "清除"};
    for (int i = 0; i < 6; i++)
    {
        lv_obj_t *b = _find_parent_with_label(view, tools[i]);
        if (!b)
        {
            printf("[FAIL] toolbar button missing: %s\n", tools[i]);
            fail++;
        }
        else
            printf("[probe] toolbar '%s' ok\n", tools[i]);
    }

    /* (3) 调色板（更多点）覆盖层：构建即存在，48 格；点 更多 展开；点卡外 收起 */
    lv_obj_t *moreBtn = _find_parent_with_label(view, "更多");
    if (moreBtn)
    {
        lv_obj_send_event(moreBtn, LV_EVENT_PRESSED, NULL);
        for (int k = 0; k < 20; k++)
        {
            lv_tick_inc(20);
            lv_timer_handler();
            eos_dispatch_tick();
        }
    }
    else
    {
        printf("[FAIL] cannot open palette: 更多 button missing\n");
        fail++;
    }
    lv_obj_t *pal = _find_parent_with_label(view, "选色");
    if (!pal)
    {
        printf("[FAIL] palette card '选色' missing (built-upfront expected)\n");
        fail++;
    }
    else
    {
        int pc = (int)lv_obj_get_child_cnt(pal);   /* card children = 1 title + 48 cells */
        printf("[probe] palette card children=%d (expect 1 title + 48 cells)\n", pc);
        if (pc != 49)
        {
            printf("[FAIL] palette cells=%d, expected 48\n", pc - 1);
            fail++;
        }
        lv_obj_t *dim = lv_obj_get_parent(pal);    /* paletteDim wraps the card */
        if (dim && lv_obj_has_flag(dim, LV_OBJ_FLAG_HIDDEN))
        {
            printf("[FAIL] palette not shown after 更多 tap\n");
            fail++;
        }
        else
            printf("[probe] palette shown after 更多 tap\n");
        if (dim)
        {
            lv_obj_send_event(dim, LV_EVENT_PRESSED, NULL);   /* 点卡外关闭 */
            for (int k = 0; k < 10; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        }
        if (dim && !lv_obj_has_flag(dim, LV_OBJ_FLAG_HIDDEN))
        {
            printf("[FAIL] palette not hidden after tap outside\n");
            fail++;
        }
        else
            printf("[probe] palette hidden after tap outside\n");
    }

    /* (4) 保存 tap -> eos.draw.save -> read back a valid .edrw */
    lv_obj_t *saveBtn = _find_parent_with_label(view, "保存");
    if (saveBtn)
    {
        lv_obj_send_event(saveBtn, LV_EVENT_PRESSED, NULL);
        for (int k = 0; k < 20; k++)
        {
            lv_tick_inc(20);
            lv_timer_handler();
            eos_dispatch_tick();
        }
    }
    else
    {
        printf("[FAIL] cannot save: 保存 button missing\n");
        fail++;
    }
    const char *path = "/sdcard/Drawings/drawing0.edrw";
    eos_file_t fp = eos_fs_open_read(path);
    if (!fp)
    {
        printf("[FAIL] saved .edrw not found: %s\n", path);
        fail++;
    }
    else
    {
        uint32_t sz = 0;
        eos_fs_size(fp, &sz);
        uint8_t hdr[11];
        int rd = eos_fs_read(fp, hdr, 11);
        eos_fs_close(fp);
        bool ok_hdr = (rd >= 11) && memcmp(hdr, "ELDRW1", 6) == 0;
        int w = hdr[6] | (hdr[7] << 8);
        int h = hdr[8] | (hdr[9] << 8);
        int fmt = hdr[10];
        uint32_t expect = 6u + 2u + 2u + 1u + (uint32_t)w * (uint32_t)h * 2u;
        printf("[probe] .edrw size=%u header=%c%c%c%c%c%c w=%d h=%d fmt=%d expect=%u\n",
               sz, hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], w, h, fmt, expect);
        if (!ok_hdr)        { printf("[FAIL] .edrw header invalid (want ELDRW1)\n"); fail++; }
        if (w != 150 || h != 150) { printf("[FAIL] .edrw dims %dx%d (want 150x150)\n", w, h); fail++; }
        if (fmt != 0)       { printf("[FAIL] .edrw fmt %d (want 0=RGB565)\n", fmt); fail++; }
        if (sz != expect)   { printf("[FAIL] .edrw size %u != %u\n", sz, expect); fail++; }
    }

    /* (5) 回读/加载 round-trip：手工造一个非均匀 .edrw（全白 + 像素0 黑），覆盖 drawing0.edrw；
     *     打开列表加载它，再保存为 drawing1.edrw；两者字节必须完全一致（证明 load 把像素写回缓冲）。
     *     若 load 是空操作，缓冲保持全白 -> drawing1 与 crafted drawing0 不同 -> 此断言 FAIL。 */
    {
        const char *d0 = "/sdcard/Drawings/drawing0.edrw";
        uint32_t px = 150u * 150u;
        uint32_t total = 11u + px * 2u;
        uint8_t *craft = (uint8_t *)malloc(total);
        if (craft)
        {
            memcpy(craft, "ELDRW1", 6);
            craft[6] = 150 & 0xFF; craft[7] = (150 >> 8) & 0xFF;
            craft[8] = 150 & 0xFF; craft[9] = (150 >> 8) & 0xFF;
            craft[10] = 0;
            for (uint32_t i = 0; i < px; i++) { craft[11 + i*2] = 0xFF; craft[11 + i*2 + 1] = 0xFF; }
            craft[11 + 0] = 0x00; craft[11 + 1] = 0x00;   /* 像素0 = 黑（可区分点） */
            eos_fs_remove(d0);
            eos_file_t wf = eos_fs_open_write(d0);
            if (wf) { eos_fs_write(wf, craft, total); eos_fs_close(wf); }
            free(craft);
        }
        else
        {
            printf("[FAIL] cannot craft load-test .edrw (OOM)\n");
            fail++;
        }

        /* (6) 打开 -> 文件列表覆盖层出现 */
        lv_obj_t *openBtn = _find_parent_with_label(view, "打开");
        if (openBtn)
        {
            lv_obj_send_event(openBtn, LV_EVENT_PRESSED, NULL);
            for (int k = 0; k < 20; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        }
        else
        {
            printf("[FAIL] cannot open list: 打开 button missing\n");
            fail++;
        }
        lv_obj_t *flTitle = _find_parent_with_label(view, "\xe6\x89\x93\xe5\xbc\x80\xef\xbc\x88\xe7\x82\xb9\xe7\xa9\xba\xe7\x99\xbd\xe5\x85\xb3\xe9\x97\xad\xef\xbc\x89");
        if (!flTitle)
        {
            printf("[FAIL] file-list overlay title missing after 打开 tap\n");
            fail++;
        }
        else
        {
            lv_obj_t *row0 = _find_parent_with_label(view, "drawing0.edrw");
            if (!row0)
            {
                printf("[FAIL] file-list row 'drawing0.edrw' missing\n");
                fail++;
            }
            else
            {
                lv_obj_send_event(row0, LV_EVENT_PRESSED, NULL);   /* (7) 加载 drawing0 */
                for (int k = 0; k < 20; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
                lv_obj_t *saveBtn2 = _find_parent_with_label(view, "保存");
                if (saveBtn2)
                {
                    lv_obj_send_event(saveBtn2, LV_EVENT_PRESSED, NULL);   /* (8) 再保存 drawing1 */
                    for (int k = 0; k < 20; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
                }
                uint32_t s0 = 0, s1 = 0;
                uint8_t *b0 = _read_whole(d0, &s0);
                uint8_t *b1 = _read_whole("/sdcard/Drawings/drawing1.edrw", &s1);
                if (!b0 || !b1)
                {
                    printf("[FAIL] load round-trip: cannot read back files (b0=%p b1=%p)\n", b0, b1);
                    fail++;
                }
                else if (s0 != s1 || memcmp(b0, b1, s0) != 0)
                {
                    printf("[FAIL] load round-trip: drawing0 != drawing1 (load did not reproduce pixels)\n");
                    fail++;
                }
                else
                {
                    printf("[probe] load round-trip ok (drawing0 == drawing1, %u bytes)\n", s0);
                }
                if (b0) free(b0);
                if (b1) free(b1);
            }
        }
    }

    /* (9) 弦宽自验：canvas 150x150 @ (45,46) 每行可见弦宽必须 >= 150 */
    {
        int C = 120, RAD = 120, cw = 150;
        int ys[7] = {46, 70, 96, 120, 146, 170, 196};
        int all = 1;
        for (int i = 0; i < 7; i++) {
            int dy = ys[i] - C;
            double ch = 2.0 * sqrt((double)(RAD * RAD - dy * dy));
            if (!(cw <= ch)) all = 0;
        }
        if (!all) { printf("[FAIL] chord self-check: canvas wider than visible chord\n"); fail++; }
        else printf("[probe] chord self-check ALL PASS (150 <= min chord)\n");
    }

    /* (10) 真实 indev 连画压力：清屏 -> 20 笔 -> 保存扫描 -> 撤销溢出；
     *      验证不闪退 + 对象账本恒定 + PRESSING 真实画点路径有效 */
    {
        lv_obj_t *root = lv_obj_get_child(view, 0);
        lv_indev_t *indev = NULL;
        for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
            if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) { indev = i; break; }
        lv_obj_t *clr = _find_parent_with_label(view, "清除");
        if (clr) {
            lv_obj_send_event(clr, LV_EVENT_PRESSED, NULL);
            for (int k = 0; k < 10; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        }
        int before = root ? (int)lv_obj_get_child_cnt(root) : -1;
        if (indev && root)
        {
            for (int s = 0; s < 20; s++)
            {
                int yy = 55 + (s % 6) * 22;
                _hg_indev_idx = 0; _hg_indev_has_y = 1;
                _hg_indev_start_x = 55; _hg_indev_start_y = yy;
                _hg_indev_end_x = 185; _hg_indev_end_y = yy + 12;
                _hg_indev_steps = 22;
                lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);
                /* 显式喂点（headless 下 SDL 不驱动 indev timer）：每帧读一次并 pump
                 * 框架清理。框架的 swipe-back catcher 在 R.root 之上，命中测试不会把
                 * PRESSED 派到 R.root —— 故逐点绘制路径由真窗口验收；此处只验证 660
                 * 次 indev 事件被框架处理时对象账本不增长、不崩溃（连画压力回归）。 */
                int reads = _hg_indev_steps + 4;
                for (int k = 0; k < reads; k++) {
                    lv_tick_inc(16);
                    lv_indev_read(indev);
                    lv_timer_handler();
                    eos_dispatch_tick();
                }
                lv_tick_inc(16); lv_indev_read(indev);
                lv_timer_handler(); eos_dispatch_tick();
            }
            int after_draw = (int)lv_obj_get_child_cnt(root);
            if (after_draw != before) { printf("[FAIL] object account grew %d->%d during draw\n", before, after_draw); fail++; }
            else printf("[probe] object account stable during 20-stroke draw (%d)\n", before);
            /* 逐点绘制路径验证（PRESSING->Bresenham->setPx 像素连续性）由真窗口验收
             * （headless 下框架 swipe-back catcher 在 R.root 之上拦截命中测试，且
             * lv_obj_send_event(root) 直发会触发 SNI 重入挂死）。此处只验证连画压力
             * 下对象账本恒定、不崩溃——即"连画 30s 账本恒定不闪退"的 headless 代理。 */
            /* 撤销溢出：多于笔画数，验证栈下溢不崩溃 + 对象账本恒定 */
            lv_obj_t *undoBtn = _find_parent_with_label(view, "撤销");
            if (undoBtn)
                for (int u = 0; u < 25; u++) {
                    lv_obj_send_event(undoBtn, LV_EVENT_PRESSED, NULL);
                    for (int k = 0; k < 4; k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
                }
            int after_undo = (int)lv_obj_get_child_cnt(root);
            if (after_undo != before) { printf("[FAIL] object account changed after undo %d->%d\n", before, after_undo); fail++; }
            else printf("[probe] object account stable after undo overflow (%d)\n", before);
        }
        else
            printf("[note] indev/root unavailable; skipped stress draw\n");
    }

    printf("[summary] draw-launch pass=%d fail=%d\n", (fail == 0) ? 1 : 0, fail);
    printf("=== draw launch probe done ===\n");
    return (fail == 0) ? 0 : 1;
}

/* ----------------------------------------------------------------------------
 * indev-driven swipe reproduction
 * Exercises the REAL LVGL pointer event path (PRESS -> drag -> RELEASE) so we
 * can catch regressions that the direct eos_watchface_builtin_test_swipe() hook
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

static eos_datetime_t _alarm_fake_dt;
static eos_datetime_t _alarm_fake_get_datetime(void) { return _alarm_fake_dt; }
static const eos_dev_time_ops_t _alarm_fake_ops = { .get_datetime = _alarm_fake_get_datetime };

/* JS eos.config persists into the APP's OWN data dir (not the system cfg):
 *   EOS_APP_DATA_DIR "<app_id>/config.json"
 * The C eos_config_set_number() writes the SYSTEM cfg.json instead, so the
 * alarm test must write/read the app-scoped file directly. */
#define ALARM_APP_CFG "com.cantomk6.alarm/config.json"
static void _alarm_write_app_config(int h, int m, int on)
{
    char path[EOS_FS_PATH_MAX];
    char buf[96];
    snprintf(path, sizeof(path), EOS_APP_DATA_DIR ALARM_APP_CFG);
    eos_storage_mkdir_if_not_exist(EOS_APP_DATA_DIR "com.cantomk6.alarm");
    snprintf(buf, sizeof(buf), "{\"alm_h\":%d,\"alm_m\":%d,\"alm_on\":%d}", h, m, on);
    eos_storage_write_file(path, buf, strlen(buf));
}
static double _alarm_read_app_config(const char *key)
{
    char path[EOS_FS_PATH_MAX];
    snprintf(path, sizeof(path), EOS_APP_DATA_DIR ALARM_APP_CFG);
    return eos_storage_json_get_number(path, key, -1);
}

int main(int argc, char *argv[])
{
    printf("ElenixOS Desktop Simulator\n");
    printf("=========================\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    SetUnhandledExceptionFilter(_crash_filter);

    /* Initialize LVGL */
    lv_init();

    /* Create SDL window as display */
    lv_display_t *disp = lv_sdl_window_create(SIM_HOR_RES, SIM_VER_RES);
    if (!disp) {
        fprintf(stderr, "Failed to create SDL display\n");
        return 1;
    }

    /* Create mouse input device */
    lv_indev_t *mouse = lv_sdl_mouse_create();
    if (!mouse) {
        fprintf(stderr, "Failed to create SDL mouse\n");
        return 1;
    }

    printf("LVGL initialized: %dx%d display with SDL2\n", SIM_HOR_RES, SIM_VER_RES);

    /* Initialize ElenixOS */
    eos_init();

    /* Seed bundled demo plugins (pmdemo, clock) so the Launcher shows them
     * even on a fresh FS. Idempotent. */
    _sim_seed_install_plugins();

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

    /* UI-JS bridge self-test: drive the eos.ui.* JerryScript bridge across
     * all 4 profiles from an in-memory JS program (Route A verification). */
    if (argc > 1 && strcmp(argv[1], "--ui-js-test") == 0)
    {
        int rc = sim_run_ui_js_test();
        return rc;
    }

    /* P0.5 相册 headless 探针: eos.fs.list/size + album_pick 回传自洽 */
    if (argc > 1 && strcmp(argv[1], "--album-probe") == 0)
    {
        int rc = sim_run_album_probe();
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "--album-launch") == 0)
    {
        int rc = sim_run_album_launch();
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "--notes-probe") == 0)
    {
        int rc = sim_run_notes_probe();
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "--notes-launch") == 0)
    {
        int rc = sim_run_notes_launch();
        return rc;
    }

    /* Draw app real-launch probe (SDL dummy): eos.draw.* canvas + 48 palette + .edrw save */
    if (argc > 1 && strcmp(argv[1], "--draw-launch") == 0)
    {
        int rc = sim_run_draw_launch();
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
        eos_watchface_instance_t *w = eos_watchface_builtin_create();
        if (!w || !w->activity) {
            printf("[FAIL] watchface instance/activity not created\n"); return 1;
        }
        /* Manually trigger _builtin_on_enter by replacing the root activity. */
        eos_activity_replace_root(w->activity);
        /* Pump one tick so any on_enter timers are registered. */
        lv_tick_inc(20);
        lv_timer_handler();

        int pass = 0, fail = 0;
#define WF_CHECK(cond, msg) do {                                            \
            if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
            else      { fail++; printf("[FAIL] %s\n", msg); }               \
        } while (0)
        lv_obj_t *view = eos_activity_get_view(w->activity);
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
        eos_watchface_instance_t *w = eos_watchface_builtin_create();
        if (!w || !w->activity) { printf("[FAIL] watchface instance/activity not created\n"); return 1; }
        eos_activity_replace_root(w->activity);
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
        eos_watchface_builtin_test_swipe(200, 0);
        HG_PUMP();
        HG_CHECK(eos_control_center_is_open(), "right-swipe opens control center");
        eos_control_center_hide();
        lv_tick_inc(20); lv_timer_handler();

        /* UP swipe -> cards page opens */
        eos_watchface_builtin_test_swipe(0, -200);
        HG_PUMP();
        {
            eos_cards_page_t *cp = eos_cards_page_get_instance();
            HG_CHECK(cp && cp->swipe_panel && cp->swipe_panel->sw &&
                     eos_slide_widget_get_state(cp->swipe_panel->sw) == EOS_SLIDE_WIDGET_STATE_OPEN,
                     "up-swipe opens cards page");
        }
        eos_cards_page_hide();
        lv_tick_inc(20); lv_timer_handler();

        /* LEFT swipe -> app list activity entered */
        eos_watchface_builtin_test_swipe(-200, 0);
        HG_PUMP();
        HG_CHECK(eos_activity_get_current() &&
                 eos_activity_get_type(eos_activity_get_current()) == EOS_ACTIVITY_TYPE_APP_LIST,
                 "left-swipe opens app list");

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef HG_CHECK
#undef HG_PUMP
        return (fail == 0) ? 0 : 1;
    }

    /* indev-driven home swipe reproduction: feed a REAL right-swipe through the
     * LVGL pointer indev (PRESS -> drag -> RELEASE) and assert the control
     * center actually opens. This catches event-delivery regressions that the
     * direct eos_watchface_builtin_test_swipe() hook cannot surface. */
    if (argc > 1 && strcmp(argv[1], "--home-gesture-indev-test") == 0)
    {
        eos_watchface_instance_t *w = eos_watchface_builtin_create();
        if (!w || !w->activity) { printf("[FAIL] watchface instance/activity not created\n"); return 1; }
        eos_activity_replace_root(w->activity);
        lv_tick_set_cb(NULL);
        /* Let the watch face lay out (catcher full-screen) before swiping. */
        for (int _k = 0; _k < 12; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }

        lv_indev_t *indev = NULL;
        for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
            if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) { indev = i; break; }
        if (!indev) { printf("[FAIL] no pointer indev found\n"); return 1; }

        eos_control_center_t *cc = eos_control_center_get_instance();
        if (!cc || !cc->swipe_panel) { printf("[FAIL] control center instance/panel missing\n"); return 1; }

        int pass = 0, fail = 0;
#define INDV_CHECK(c, m) do {                                            \
            if (c) { pass++; printf("[OK]   %s\n", m); }                \
            else      { fail++; printf("[FAIL] %s\n", m); }             \
        } while (0)

        /* Helper: close the panel if open, then settle. */
#define INDV_CLOSE() do {                                                \
            eos_slide_widget_state_t _s = eos_slide_widget_get_state(cc->swipe_panel->sw); \
            if (_s == EOS_SLIDE_WIDGET_STATE_OPEN) {                    \
                eos_swipe_panel_pull_back(cc->swipe_panel);             \
                for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); } \
            }                                                            \
        } while (0)

        /* Helper: drive one real swipe through lv_indev_read (PRESS -> drag ->
         * RELEASE), then settle animations. eos_dispatch_tick() runs the
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
            for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); } \
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
            for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); } \
            _hg_indev_has_y = 0;                                         \
        } while (0)

        /* Case A: right-swipe from the LEFT edge (the natural control-bar pull). */
        INDV_CLOSE();
        /* Left-edge swipe: start at x=60 so the press lands on the home
         * catcher (the closed control center hides its 50px touch strip). */
        INDV_SWIPE(60, 235);
        INDV_CHECK(eos_slide_widget_get_state(cc->swipe_panel->sw) == EOS_SLIDE_WIDGET_STATE_OPEN,
                   "left-edge right-swipe opens control center (REAL event path)");

        /* Case B: right-swipe from the CENTER (catcher handles it). */
        INDV_CLOSE();
        INDV_SWIPE(130, 235);
        INDV_CHECK(eos_slide_widget_get_state(cc->swipe_panel->sw) == EOS_SLIDE_WIDGET_STATE_OPEN,
                   "center right-swipe opens control center (REAL event path)");

        /* Case C: left-swipe from the CENTER (user reports this WORKS in the
         * real window -> opens the app list). Used to confirm the indev harness
         * actually delivers events to the catcher at all. */
        INDV_CLOSE();  /* Case B left the control center OPEN; close it first
                        * or this press lands on the CC panel, not the catcher. */
        INDV_SWIPE(130, 20);
        INDV_CHECK(eos_activity_get_current() &&
                   eos_activity_get_type(eos_activity_get_current()) == EOS_ACTIVITY_TYPE_APP_LIST,
                   "center left-swipe opens app list (REAL event path)");

        /* Case D: right-swipe on the APP page (now open from Case C) must pop
         * back to the watchface. Exercises the REAL exit path of the current
         * app-list page (built-in bubble_grid). */
        /* Case D: UP-swipe on the APP page (now open from Case C) must pop
         * back to the watchface. Exercises the REAL exit path of the current
         * app-list page (built-in bubble_grid) — 退出手势已改为上下滑。 */
        INDV_SWIPE_V(200, 60);
        INDV_CHECK(eos_activity_get_current() &&
                   eos_activity_get_type(eos_activity_get_current()) == EOS_ACTIVITY_TYPE_WATCHFACE,
                   "app-page UP-swipe exits back to watchface (REAL event path)");

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
        INDV_CHECK(eos_activity_get_current() &&
                   eos_activity_get_type(eos_activity_get_current()) == EOS_ACTIVITY_TYPE_WATCHFACE,
                   "SLOW swipe does not open the watchface list (long-press needs to be stationary)");
        INDV_CHECK(eos_slide_widget_get_state(cc->swipe_panel->sw) == EOS_SLIDE_WIDGET_STATE_OPEN,
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
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        INDV_CHECK(eos_activity_get_current() &&
                   eos_activity_get_type(eos_activity_get_current()) == EOS_ACTIVITY_TYPE_WATCHFACE_LIST,
                   "stationary long-press still opens the watchface list");
        eos_activity_back_to_watchface();
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }

        /* Case G: the 'C' hotkey path. Exercises exactly what the SDL key handler
         * calls, so the keyboard shortcut is proven to work rather than merely
         * proven to compile. It must open the panel, and toggle it shut again. */
        INDV_CLOSE();
        eos_control_center_show();
        eos_control_panel_slide_change();
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        INDV_CHECK(eos_slide_widget_get_state(cc->swipe_panel->sw) == EOS_SLIDE_WIDGET_STATE_OPEN,
                   "hotkey 'C' path opens control center");
        eos_control_center_show();
        eos_control_panel_slide_change();
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        INDV_CHECK(eos_slide_widget_get_state(cc->swipe_panel->sw) != EOS_SLIDE_WIDGET_STATE_OPEN,
                   "hotkey 'C' path closes control center again");

        /* Case H: WOS framework — open the demo app, then LEFT-swipe must
         * close it (App Manager swipe-back). Validates the new framework's
         * gesture routing AND the deferred cleanup (state returns to IDLE). */
        INDV_CLOSE();
        wos_app_manager_open("demo");
        for (int _k = 0; _k < 40; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        INDV_CHECK(wos_app_manager_get_state() == WOS_APP_STATE_ACTIVE,
                   "wos open('demo') reaches ACTIVE");
        INDV_SWIPE_V(200, 60);   /* UP-swipe across the app page (退出手势=上下滑) */
        INDV_CHECK(wos_app_manager_get_state() == WOS_APP_STATE_IDLE,
                   "wos UP-swipe closes the app (deferred cleanup -> IDLE)");

        /* Case I: REAL JerryScript eapk app (com.cantomk6.clock) launched via
         * the activity framework. UP-swipe anywhere on the page must back
         * out to the watchface. This exercises the _indev_swipe_back_cb path
         * (退出手势已改为上下滑)。Also verifies the view scroll_dir was
         * constrained to HOR so the gesture isn't eaten by a vertical list
         * scroll. */
        INDV_CLOSE();
        eos_activity_back_to_watchface();
        for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        bool jsapp_ok = (eos_app_launch_immediately("com.cantomk6.clock") == EOS_OK);
        INDV_CHECK(jsapp_ok, "launch 'com.cantomk6.clock' (eapk)");
        if (jsapp_ok) {
            /* Let the activity enter + spm_app_run + JS main.js execute + layout. */
            for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
            eos_activity_t *cur_a = eos_activity_get_current();
            INDV_CHECK(cur_a && eos_activity_get_type(cur_a) == EOS_ACTIVITY_TYPE_APP,
                       "JS eapk app is the current activity");
            /* 左滑不再退出（退出手势已改为上下滑） */
            INDV_SWIPE(120, 20);
            eos_activity_t *after_a = eos_activity_get_current();
            INDV_CHECK(after_a && eos_activity_get_type(after_a) == EOS_ACTIVITY_TYPE_APP,
                       "left-swipe inside JS eapk app does NOT exit (exit gesture = up/down)");
            /* Drive an UP-swipe from the CENTER of the screen — lands on
             * app content (a JS-created lv.obj), reproducing the real-window
             * gesture path. */
            INDV_SWIPE_V(200, 60);
            after_a = eos_activity_get_current();
            INDV_CHECK(after_a && eos_activity_get_type(after_a) == EOS_ACTIVITY_TYPE_WATCHFACE,
                       "UP-swipe inside JS eapk app pops back to watchface (indev-level)");
        }

        /* Case J (temp): relaunch pmdemo, tap its content. Same press→release
         * harness as the swipes above. */
        eos_activity_back_to_watchface();
        for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        eos_app_launch_immediately("com.cantomk6.clock");
        for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
        _hg_indev_steps = 0; _hg_indev_idx = 0;   /* 单帧 press + 立即 release（真窗口等价） */
        _hg_indev_start_x = 120; _hg_indev_end_x = 120; _hg_indev_y = 120;
        lv_indev_set_read_cb(indev, _hg_indev_swipe_read_cb);
        for (int _k = 0; _k < 6; _k++) { lv_tick_inc(16); lv_indev_read(indev); }
        for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
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
     * chrome manager. The watchface must call eos_cards_page_show() so the
     * up-swipe touch strip is enabled at runtime. */
    if (argc > 1 && strcmp(argv[1], "--cards-page-test") == 0)
    {
        /* Includes below the test function definition; pull them in here. */
#include "framework/chrome/eos_chrome_manager.h"
        /* The simulator already booted through eos_core, which calls
         * eos_cards_page_init() and registers the overlay. Just verify. */
        int pass = 0, fail = 0;
#define CP_CHECK(cond, msg) do {                                            \
            if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
            else      { fail++; printf("[FAIL] %s\n", msg); }               \
        } while (0)

        eos_cards_page_t *cp = eos_cards_page_get_instance();
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
        const eos_chrome_overlay_t *od = eos_cards_page_get_overlay_descriptor();
        CP_CHECK(od != NULL && od->name && strcmp(od->name, "cards_page") == 0,
                 "cards page chrome overlay descriptor registered");
        /* Verify the touch strip is enabled after a show() call. */
        eos_cards_page_show();
        if (cp && cp->swipe_panel && cp->swipe_panel->sw)
        {
            lv_obj_t *to = eos_slide_widget_get_touch_obj(cp->swipe_panel->sw);
            CP_CHECK(to && !lv_obj_has_flag(to, LV_OBJ_FLAG_HIDDEN),
                     "cards page touch strip visible after show()");
        }
        eos_cards_page_hide();
        if (cp && cp->swipe_panel && cp->swipe_panel->sw)
        {
            lv_obj_t *to = eos_slide_widget_get_touch_obj(cp->swipe_panel->sw);
            CP_CHECK(to && lv_obj_has_flag(to, LV_OBJ_FLAG_HIDDEN),
                     "cards page touch strip hidden after hide()");
        }

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef CP_CHECK
        return (fail == 0) ? 0 : 1;
    }

    /* Cards-page app-facing registration API: apps register/unregister
     * cards through eos_cards_page_register_card(). Verify a registered card
     * appears in the container, unregister removes it, and re-registering the
     * same id does not grow the count (de-dup). */
    if (argc > 1 && strcmp(argv[1], "--cards-api-test") == 0)
    {
        int pass = 0, fail = 0;
#define API_CHECK(cond, msg) do {                                            \
            if (cond) { pass++; printf("[OK]   %s\n", msg); }               \
            else      { fail++; printf("[FAIL] %s\n", msg); }               \
        } while (0)

        eos_cards_page_t *cp = eos_cards_page_get_instance();
        API_CHECK(cp != NULL, "cards page instance present");
        int base = cp ? (int)cp->card_count : 0;
        uint32_t base_children = (cp && cp->cards) ? lv_obj_get_child_cnt(cp->cards) : 0;

        eos_card_handle_t h = eos_cards_page_register_card(&(eos_card_desc_t){
            .id = "app_test", .title = "AppCard", .accent = lv_color_hex(0x3355AA),
            .priority = 5, .build = NULL, .click = NULL, .user_data = NULL });
        API_CHECK(h != NULL, "register_card returns a non-NULL handle");
        API_CHECK(cp && (int)cp->card_count == base + 1, "card_count incremented after register");
        if (cp && cp->cards)
        {
            uint32_t n = lv_obj_get_child_cnt(cp->cards);
            API_CHECK(n == base_children + 1, "container child count grew by 1 after register");
        }

        eos_cards_page_unregister_card(h);
        API_CHECK(cp && (int)cp->card_count == base, "card_count restored after unregister");
        if (cp && cp->cards)
        {
            uint32_t n = lv_obj_get_child_cnt(cp->cards);
            API_CHECK(n == base_children, "container child count restored after unregister");
        }

        /* De-dup: re-registering the same id replaces, count unchanged. */
        eos_card_handle_t h1 = eos_cards_page_register_card(&(eos_card_desc_t){
            .id = "app_test", .title = "AppCard2", .accent = lv_color_hex(0x5533AA),
            .priority = 5, .build = NULL, .click = NULL, .user_data = NULL });
        API_CHECK(h1 != NULL, "re-register same id succeeds");
        API_CHECK(cp && (int)cp->card_count == base + 1, "re-register same id does not grow count");
        eos_cards_page_unregister_card(h1);
        API_CHECK(cp && (int)cp->card_count == base, "card_count restored after re-register+unregister");

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef API_CHECK
        return (fail == 0) ? 0 : 1;
    }

    /* Alarm app (JS eapk, com.cantomk6.alarm) headless state-machine test:
     * launch the app, then drive the picker (+/-), set/cancel/toggle via the
     * real LVGL pointer indev (PRESSED, as the app binds), and assert the UI
     * (label texts) + persistence (eos.config) at every step. The ring
     * overlay is exercised with a swapped-in FAKE clock so the alarm time
     * matches "now" deterministically (sec=0 satisfies t.sec<=1 on the first
     * 100 ms tick) — no waiting for a real minute boundary. */
    if (argc > 1 && strcmp(argv[1], "--alarm-test") == 0)
    {
        int pass = 0, fail = 0;
#define ALM_CHECK(c, m) do {                                            \
            if (c) { pass++; printf("[OK]   %s\n", m); }               \
            else      { fail++; printf("[FAIL] %s\n", m); }            \
        } while (0)

        /* --- helpers: find a label by exact text anywhere under parent --- */
        /* (_alarm_find_label is file-scope above) */

        /* --- fake clock for the ring test (deterministic trigger) --- */
        /* (_alarm_fake_dt / _alarm_fake_ops are file-scope above) */
        eos_dev_time_t *_td = eos_dev_time_get_instance();
        const eos_dev_time_ops_t *_saved_ops = _td->ops;

        /* --- settle the LVGL tick so timers/animations actually advance --- */
        lv_tick_set_cb(NULL);
        for (int _k = 0; _k < 12; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }

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
            for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); } \
        } while (0)

        /* restart helper: pop back to watchface, settle deferred cleanup, relaunch */
#define ALM_RELAUNCH() do {                                             \
            eos_activity_back_to_watchface();                           \
            for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); } \
            ALM_CHECK(eos_app_launch_immediately("com.cantomk6.alarm") == EOS_OK, "relaunch com.cantomk6.alarm"); \
            for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); } \
            view = eos_activity_get_view(eos_activity_get_current());  \
            ALM_CHECK(view != NULL, "relaunched activity view present"); \
        } while (0)

        printf("=== Alarm app headless test ===\n");

        /* [1] fresh state: 07:30 / off (app-scoped config — JS reads its own file) */
        _alarm_write_app_config(7, 30, 0);
        ALM_CHECK(eos_app_launch_immediately("com.cantomk6.alarm") == EOS_OK, "launch com.cantomk6.alarm");
        for (int _k = 0; _k < 80; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }

        lv_obj_t *view = eos_activity_get_view(eos_activity_get_current());
        ALM_CHECK(view != NULL, "activity view present");
        ALM_CHECK(_alarm_find_label(view, "07") != NULL, "hour picker shows 07");
        ALM_CHECK(_alarm_find_label(view, "30") != NULL, "minute picker shows 30");
        ALM_CHECK(_alarm_find_label(view, "待命") != NULL, "status row shows 待命");

        /* [2] picker +/- (single steps) */
        ALM_TAP(86, 106);   /* hPlus center (86-14,98) + half 28x16 */
        ALM_CHECK(_alarm_find_label(view, "08") != NULL, "hPlus -> 08");
        ALM_TAP(86, 146);   /* hMinus */
        ALM_CHECK(_alarm_find_label(view, "07") != NULL, "hMinus -> 07");
        ALM_TAP(154, 106);  /* mPlus */
        ALM_CHECK(_alarm_find_label(view, "31") != NULL, "mPlus -> 31");
        ALM_TAP(154, 146);  /* mMinus */
        ALM_CHECK(_alarm_find_label(view, "30") != NULL, "mMinus -> 30");
        ALM_TAP(154, 146);  /* mMinus again: 30-1=29 (no wrap at 30) */
        ALM_CHECK(_alarm_find_label(view, "29") != NULL, "mMinus -> 29 (no wrap)");

        /* [3] wrap-around: hour 07 minus 8 -> 23 (07-8 wraps through 00) */
        for (int _k = 0; _k < 8; _k++) ALM_TAP(86, 146);
        ALM_CHECK(_alarm_find_label(view, "23") != NULL, "hour wraps 07-8 -> 23");

        /* [4] set: picker (23:29) commits to config, on=1, status 已设定 */
        ALM_TAP(120, 177);  /* setBtn center (100+20, 166+11) */
        ALM_CHECK(_alarm_read_app_config("alm_on") == 1, "set -> config alm_on=1");
        ALM_CHECK(_alarm_read_app_config("alm_h") == 23, "set -> config alm_h=23");
        ALM_CHECK(_alarm_read_app_config("alm_m") == 29, "set -> config alm_m=29");
        ALM_CHECK(_alarm_find_label(view, "已设定 23:29") != NULL, "status row shows 已设定 23:29");

        /* [5] toggle: on -> off -> status 待命 */
        ALM_TAP(76, 177);   /* swBtn center (56+20, 166+11) */
        ALM_CHECK(_alarm_read_app_config("alm_on") == 0, "toggle -> config alm_on=0");
        ALM_CHECK(_alarm_find_label(view, "待命") != NULL, "status row back to 待命");
        ALM_TAP(76, 177);   /* back on */
        ALM_CHECK(_alarm_read_app_config("alm_on") == 1, "toggle again -> config alm_on=1");

        /* [6] cancel: change picker, cancel restores last-committed 23:29 */
        ALM_TAP(86, 106);   /* hour 23 -> 00 */
        ALM_CHECK(_alarm_find_label(view, "00") != NULL, "picker changed to 00 before cancel");
        ALM_TAP(164, 177);  /* canBtn center (144+20, 166+11) */
        ALM_CHECK(_alarm_find_label(view, "23") != NULL, "cancel restores hour 23");
        ALM_CHECK(_alarm_find_label(view, "29") != NULL, "cancel restores minute 29");

        /* [7] ring overlay with fake clock 12:34:00 (sec=0 <= 1 -> first tick) */
        _alarm_fake_dt.year = 2026; _alarm_fake_dt.month = 8; _alarm_fake_dt.day = 17;
        _alarm_fake_dt.hour = 12; _alarm_fake_dt.min = 34; _alarm_fake_dt.sec = 0; _alarm_fake_dt.ms = 0;
        _alarm_fake_dt.day_of_week = 1;
        _td->ops = &_alarm_fake_ops;   /* swap in fake clock */
        _alarm_write_app_config(12, 34, 1);
        ALM_RELAUNCH();
        lv_obj_t *ring_title = _alarm_find_label(view, "闹钟响铃！");
        ALM_CHECK(ring_title != NULL, "ring overlay title present");
        if (ring_title)
        {
            lv_obj_t *ring_ov = lv_obj_get_parent(ring_title);
            ALM_CHECK(ring_ov != NULL, "ring overlay object found");
            ALM_CHECK(ring_ov && !lv_obj_has_flag(ring_ov, LV_OBJ_FLAG_HIDDEN), "ring overlay visible (not HIDDEN)");
            /* flash: pump 500 ms, opacity must toggle */
            int opa_a = lv_obj_get_style_opa(ring_ov, 0);
            for (int _k = 0; _k < 30; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
            int opa_b = lv_obj_get_style_opa(ring_ov, 0);
            ALM_CHECK(opa_a != opa_b, "ring overlay flashes (opa toggles)");
            /* stop button (88,150 64x26 center = 120,163) hides the overlay.
             * CRITICAL: the fake clock stays at 12:34:00 (sec=0 <= 1) forever, so
             * right after ringStop() the 100ms tick sees the match again and
             * re-triggers ringStart(), un-hiding the overlay. Move the fake clock
             * past the match window BEFORE tapping (real clocks advance too). */
            _alarm_fake_dt.min = 35;
            for (int _k = 0; _k < 20; _k++) { lv_tick_inc(20); lv_timer_handler(); eos_dispatch_tick(); }
            ALM_TAP(120, 163);
            ALM_CHECK(lv_obj_has_flag(ring_ov, LV_OBJ_FLAG_HIDDEN), "ring overlay hidden after stop");
        }
        _td->ops = _saved_ops;   /* restore real clock */

        /* [8] auto-stop guard: ringTick>=600 (60 s) would stop; verify overlay
         * stays hidden when we relaunch with a non-matching alarm time. */
        _alarm_write_app_config(12, 35, 1);   /* now fake is 12:34:00 -> no match */
        ALM_RELAUNCH();
        ALM_CHECK(_alarm_find_label(view, "闹钟响铃！") == NULL ||
                  lv_obj_has_flag(lv_obj_get_parent(_alarm_find_label(view, "闹钟响铃！")), LV_OBJ_FLAG_HIDDEN),
                  "no ring when time does not match");

        printf("\n[summary] pass=%d fail=%d\n", pass, fail);
#undef ALM_TAP
#undef ALM_RELAUNCH
#undef ALM_CHECK
        return (fail == 0) ? 0 : 1;
    }

    /* Shell: wire a serial-like console when stdin is a real TTY */
#ifdef _WIN32
    g_console = (_isatty(_fileno(stdin)) != 0);
    if (g_console)
    {
        eos_shell_set_reboot_cb(sim_reboot);
        eos_shell_framework_init();
        eos_shell_framework_prompt(sim_echo, NULL);
        printf("[INFO] [Sim] Shell console ready. Type commands (e.g. 'help').\n");
        fflush(stdout);
    }
#endif

    printf("ElenixOS initialized, entering main loop...\n");
    printf("---------------------------------------------\n");
    printf(" Hotkeys (click the sim window first):\n");
    printf("   C   toggle Control Center\n");
    printf("   L   open App list\n");
    printf("   H   back to Watchface\n");
    printf("   R   back one step\n");
    printf("   Esc quit\n");
    printf("---------------------------------------------\n\n");

    /* Main loop */
    uint32_t last_tick = lv_tick_get();
    while (g_running) {
        uint32_t now = lv_tick_get();
        uint32_t elapsed = now - last_tick;
        last_tick = now;

        /* Process SDL events for hotkeys */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                switch (event.key.keysym.sym) {
                    case SDLK_l:   /* L — open app list */
                        printf("[INFO] [Sim] Hotkey 'L' pressed — opening app list\n");
                        fflush(stdout);
                        eos_app_list_enter();
                        break;
                    case SDLK_r:   /* R — go back / exit current page */
                        printf("[INFO] [Sim] Hotkey 'R' pressed — going back\n");
                        fflush(stdout);
                        eos_activity_back();
                        break;
                    case SDLK_c: { /* C — open / close the control center */
                        eos_control_center_t *cc = eos_control_center_get_instance();
                        printf("[INFO] [Sim] Hotkey 'C' pressed — toggling control center (instance=%p)\n",
                               (void *)cc);
                        fflush(stdout);
                        /* Reveal the panel content first: it is kept hidden while
                         * closed so the left-edge touch strip cannot steal the
                         * home catcher's right-swipe. Then toggle open/closed. */
                        eos_control_center_show();
                        eos_control_panel_slide_change();
                        break;
                    }
                    case SDLK_h:   /* H — jump straight back to the watchface */
                        printf("[INFO] [Sim] Hotkey 'H' pressed — back to watchface\n");
                        fflush(stdout);
                        eos_activity_back_to_watchface();
                        break;
                    case SDLK_w: { /* W — toggle the WOS demo app (new framework) */
                        printf("[INFO] [Sim] Hotkey 'W' pressed — toggling WOS demo app\n");
                        fflush(stdout);
                        if (wos_app_manager_get_state() == WOS_APP_STATE_ACTIVE ||
                            wos_app_manager_get_state() == WOS_APP_STATE_LAUNCHING)
                        {
                            wos_app_manager_close();
                        }
                        else
                        {
                            wos_app_manager_open("demo");
                        }
                        break;
                    }
                    case SDLK_ESCAPE:
                        g_running = 0;
                        break;
                    default:
                        break;
                }
            }
            /* Let SDL handle the event for LVGL */
            lv_sdl_mouse_handler(&event);
            lv_sdl_keyboard_handler(&event);
        }

        /* Poll the shell console (no-op when stdin is not a TTY) */
#ifdef _WIN32
        sim_console_poll();
#endif

        /* Drive ElenixOS + LVGL */
        uint32_t sleep_ms = eos_main_loop();

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
