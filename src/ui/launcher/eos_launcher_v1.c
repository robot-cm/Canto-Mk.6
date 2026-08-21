/**
 * @file eos_launcher.c
 * @brief ElenixOS adaptive Launcher (root activity)
 *
 * Full migration onto the UI framework (P8+): the Launcher is now a single
 * adaptive Home screen that renders its app list through the framework —
 * ArcList on round displays, LayoutManager grid on square/rectangle — with a
 * Liquid Glass status bar + live clock. No hardcoded coordinates, no page
 * pager; the same code path serves all four required screen shapes.
 *
 * App launching goes through the real Plugin Manager for everything except
 * the two native media apps (Gallery / Files). Disabled apps are filtered out,
 * and the Home rebuilds live when apps are installed/uninstalled.
 */
#include "ui/launcher/eos_launcher.h"

#include "eos_config.h"
#include "eos_log.h"
#include "eos_activity.h"
#include "lvgl.h"
#include "eos_app.h"
#include "eos_app_list.h"   /* eos_app_launch_immediately() */
#include "eos_service_storage.h" /* eos_storage_is_file() for icon.bin */
#include "apps/gallery/eos_gallery.h" /* eos_gallery_enter() */
#include "apps/files/eos_files.h"     /* eos_files_enter() */
#include "ui/system/eos_round_clip.h" /* eos_round_clip() */
#include "eos_event.h"                 /* EOS_EVENT_APP_INSTALLED / UNINSTALLED */
#include "ui/framework/eos_ui_framework.h" /* framework Home + views + profiles */
#include "eos_ui_anim.h"                      /* page transition animations */
#include "eos_service_config.h"               /* square-mode setting */
#include "eos_mem.h"                          /* eos_free() for config string */

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* App definitions                                                    */
/* ------------------------------------------------------------------ */

/* Native (local) app discriminators — only used when app_id == NULL */
typedef enum
{
    LAUNCHER_APP_KIND_GALLERY,
    LAUNCHER_APP_KIND_FILES,
    LAUNCHER_APP_KIND_PLUGIN
} launcher_app_kind_t;

typedef struct
{
    const char *name;
    const char *app_id;   /* NULL => local (native Gallery/Files); otherwise launched via Plugin Manager */
    uint32_t color;
    char mono;
    bool local;
    launcher_app_kind_t kind;  /* disambiguates local (native) apps */
} launcher_app_def_t;

/* Dynamic app list, populated from the real Plugin Manager at build time. */
#define LAUNCHER_APP_MAX 64
static launcher_app_def_t s_apps[LAUNCHER_APP_MAX];
static int s_app_count = 0;
/* Persistent icon-path buffers (LVGL strdups the path; keep them alive). */
static char s_app_icon_paths[LAUNCHER_APP_MAX][EOS_FS_PATH_MAX];

/* Palette for auto-colored installed plugins */
static const uint32_t s_plugin_palette[] = {
    0x27AE60, 0xEB5757, 0x9B59B6, 0x00B8D4, 0xF2C94C, 0x2F80ED, 0xE67E22, 0x16A085
};
#define PLUGIN_PALETTE_N (sizeof(s_plugin_palette) / sizeof(s_plugin_palette[0]))

/* Live Home + timers (rebuilt on install/uninstall). */
static eos_framework_home_t *s_home = NULL;
static lv_timer_t *s_step_timer = NULL;
static eos_activity_t *s_launcher_activity = NULL;
/* Square-layout mode chosen at runtime (set only from _launcher_on_enter,
   where config is guaranteed initialized). Kept as a static so the
   install/uninstall rebuild path can re-apply it. NOTE: config reads must
   stay OUT of eos_launcher_build_home / eos_launcher_rebuild — those are
   also driven by the headless --launcher-test where the config service is
   not initialized (would NULL-deref). */
static bool s_square_bubble = false;
/* Parent + profile captured at build time so the live-rebuild path needs no
 * activity handle (also lets the headless --launcher-test drive a rebuild). */
static lv_obj_t *s_home_parent = NULL;
static eos_display_profile_t s_home_profile;

/* ------------------------------------------------------------------ */
/* App launching                                                       */
/* ------------------------------------------------------------------ */

static void _open_app(const launcher_app_def_t *app)
{
    if (!app)
        return;

    if (app->local)
    {
        /* Local (native) apps — each has its own entry function */
        switch (app->kind)
        {
            case LAUNCHER_APP_KIND_GALLERY:
                eos_gallery_enter();
                break;
            case LAUNCHER_APP_KIND_FILES:
                eos_files_enter();
                break;
            default:
                break;
        }
        return;
    }

    /* Everything else goes through the real Plugin Manager chain:
     * scan manifest -> launch via SPM -> JerryScript runs main.js.
     * eos_app_launch_immediately() handles both system apps
     * (sys.settings, sys.flash_light, ...) and installed script apps. */
    eos_result_t r = eos_app_launch_immediately(app->app_id);
    if (r != EOS_OK)
        EOS_LOG_W("Failed to launch app '%s' (result=%d)", app->app_id ? app->app_id : "?", (int)r);
}

/* Card-tap -> launch the tapped app. */
static void _home_on_select(int index, void *user)
{
    LV_UNUSED(user);
    if (index < 0 || index >= s_app_count)
        return;
    _open_app(&s_apps[index]);
}

/* ------------------------------------------------------------------ */
/* Build the app definition list from the real Plugin Manager         */
/* ------------------------------------------------------------------ */

static void _build_app_defs(void)
{
    s_app_count = 0;

    /* Gallery + Files: native (local) media apps */
    if (s_app_count < LAUNCHER_APP_MAX)
    {
        s_apps[s_app_count].name   = "Gallery";
        s_apps[s_app_count].app_id = NULL;
        s_apps[s_app_count].color  = 0xEB5757;
        s_apps[s_app_count].mono   = 'G';
        s_apps[s_app_count].local  = true;
        s_apps[s_app_count].kind   = LAUNCHER_APP_KIND_GALLERY;
        s_app_count++;
    }
    if (s_app_count < LAUNCHER_APP_MAX)
    {
        s_apps[s_app_count].name   = "Files";
        s_apps[s_app_count].app_id = NULL;
        s_apps[s_app_count].color  = 0x00B8D4;
        s_apps[s_app_count].mono   = 'D';
        s_apps[s_app_count].local  = true;
        s_apps[s_app_count].kind   = LAUNCHER_APP_KIND_FILES;
        s_app_count++;
    }

    /* Everything the Plugin Manager knows about: installed script apps
     * followed by built-in system apps (sys.settings, sys.flash_light, ...). */
    uint32_t installed = eos_app_get_installed();
    for (uint32_t i = 0; i < installed && (uint32_t)s_app_count < LAUNCHER_APP_MAX; i++)
    {
        const char *id = eos_app_list_get_id(i);
        if (!id)
            continue;

        /* Hidden if the user disabled this app */
        if (eos_app_is_disabled(id))
            continue;

        launcher_app_def_t *a = &s_apps[s_app_count];

        if (strcmp(id, "sys.settings") == 0)
        {
            a->name = "Settings"; a->color = 0xF2994A; a->mono = 'S';
        }
        else if (strcmp(id, "sys.flash_light") == 0)
        {
            a->name = "Flash Light"; a->color = 0x9B59B6; a->mono = 'F';
        }
#ifdef EOS_ENABLE_TEST_APP
        else if (strcmp(id, "sys.test") == 0)
        {
            a->name = "Test"; a->color = 0xE67E22; a->mono = 'T';
        }
#endif
        else
        {
            const char *nm = eos_app_get_name(id);
            if (!nm || nm[0] == '\0')
                nm = id;
            a->name = nm;
            a->color = s_plugin_palette[s_app_count % PLUGIN_PALETTE_N];
            a->mono = (nm[0] ? nm[0] : (id[0] ? id[0] : '?'));
        }
        a->app_id = id;
        a->local = false;
        a->kind = LAUNCHER_APP_KIND_PLUGIN;
        s_app_count++;
    }
}

/* ------------------------------------------------------------------ */
/* Build the adaptive Home (framework-backed) from the app list      */
/* ------------------------------------------------------------------ */

int eos_launcher_build_home(lv_obj_t *parent, const eos_display_profile_t *p,
                            eos_framework_home_t **out_home)
{
    _build_app_defs();

    const char *names[LAUNCHER_APP_MAX];
    const char *icons[LAUNCHER_APP_MAX];
    for (int i = 0; i < s_app_count; i++)
    {
        names[i] = s_apps[i].name;
        icons[i] = NULL;
        if (s_apps[i].app_id)
        {
            snprintf(s_app_icon_paths[i], sizeof(s_app_icon_paths[i]),
                     EOS_APP_INSTALLED_DIR "%s/" EOS_APP_ICON_FILE_NAME, s_apps[i].app_id);
            if (eos_storage_is_file(s_app_icon_paths[i]))
                icons[i] = s_app_icon_paths[i];
        }
    }

    eos_framework_home_t *h = eos_framework_home_create(parent, p, names, s_app_count, icons);
    if (!h)
        return 0;
    eos_framework_home_set_on_select(h, _home_on_select, NULL);

    /* Capture parent + profile so the live-rebuild path can re-build without
     * an activity handle. Keep the module home handle in sync so rebuild /
     * get_home always refer to the current Home (the caller may pass a local
     * out_home, but the rebuild path relies on the module static). */
    s_home_parent = parent;
    s_home_profile = *p;
    s_home = h;

    if (out_home)
        *out_home = h;
    return s_app_count;
}

/* ------------------------------------------------------------------ */
/* Keep the ArcList physics animating (no-op on square/rect grids). */
/* ------------------------------------------------------------------ */

static void _step_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (s_home)
        eos_framework_home_step(s_home, 16.0f);
}

/* ------------------------------------------------------------------ */
/* Rebuild on install/uninstall (plugins seeded after init)          */
/* ------------------------------------------------------------------ */

void eos_launcher_rebuild(void)
{
    if (!s_home_parent || !s_home)
        return;

    /* Stop timers before deleting the objects they reference. */
    if (s_step_timer)  { lv_timer_delete(s_step_timer);  s_step_timer = NULL; }
    eos_framework_home_destroy(s_home);
    s_home = NULL;

    int n = eos_launcher_build_home(s_home_parent, &s_home_profile, &s_home);
    EOS_LOG_I("Launcher: rebuild Apps grid (install/uninstall event), apps=%d", n);
    if (s_home)
    {
        eos_framework_home_set_on_select(s_home, _home_on_select, NULL);
        /* Re-apply the cached square-layout mode (grid or bubble) so an
           install/uninstall event preserves the user's choice. Config reads
           stay out of this path (headless --launcher-test calls it). */
        if (s_square_bubble && !s_home->is_circle)
            eos_framework_home_use_bubble(s_home);
        s_step_timer = lv_timer_create(_step_tick, 16, NULL);
    }
}

eos_framework_home_t *eos_launcher_get_home(void)
{
    return s_home;
}

static void _on_app_changed_cb(eos_event_t *e)
{
    LV_UNUSED(e);
    eos_launcher_rebuild();
}

/* ------------------------------------------------------------------ */
/* Launcher as a SEPARATE page (entered from the watchface)          */
/* ------------------------------------------------------------------ */

static bool s_routes_registered = false;

/* Page-exit animation: when backing from the Launcher (APP_LIST) to the
 * WatchFace, fade the launcher view out (opacity 255->0) and scale it
 * 1.0 -> 0.8 — matching the page-open spec in reverse. */
static void _launcher_close_anim_cb(lv_anim_timeline_t *at,
                                    eos_activity_t *from, eos_activity_t *to)
{
    LV_UNUSED(to);
    lv_obj_t *view = eos_activity_get_view(from);
    if (!view) return;
    eos_anim_add_fade_scale(at, view, 255, 0, 256, 205, 0, 250);
}

static void _register_routes_once(void)
{
    if (s_routes_registered)
        return;
    eos_activity_register_anim_route(EOS_ACTIVITY_TYPE_APP_LIST,
                                     EOS_ACTIVITY_TYPE_WATCHFACE,
                                     _launcher_close_anim_cb);
    s_routes_registered = true;
}

static void _launcher_on_enter(eos_activity_t *activity)
{
    s_launcher_activity = activity;
    lv_obj_t *view = eos_activity_get_view(activity);

    /* Round clip only makes sense on a round panel; square/rect leave the
     * view unclipped so the grid/bubble fills the rectangle. */
    eos_display_profile_t p = eos_display_profiles_get(EOS_PROFILE_240C);
    if (p.shape == EOS_DISPLAY_SHAPE_CIRCLE)
        eos_round_clip(view);
    lv_obj_set_style_bg_color(view, lv_color_black(), 0);

    /* Build the adaptive Home on the real device profile (240x240 round in the
     * simulator). The same framework code path adapts to every shape — proven
     * by the headless --launcher-test across all four profiles. */
    int n = eos_launcher_build_home(view, &p, &s_home);
    EOS_LOG_I("Launcher apps: %d (Gallery + Files (local) + %d plugin(s))",
              n, (n > 2 ? n - 2 : 0));

    /* Square/rectangle screens honor the user's grid/bubble setting. Round
       screens always use the Huawei-style ArcList. Config is guaranteed to be
       initialized here (this runs after eos_init at runtime; the headless
       tests never enter this path), so eos_config_get_string is safe. Cache
       the decision in s_square_bubble so the rebuild path can re-apply it. */
    bool want_bubble = false;
    if (p.shape != EOS_DISPLAY_SHAPE_CIRCLE)
    {
        char *mode = eos_config_get_string(EOS_CONFIG_KEY_LAUNCHER_SQUARE_MODE, "grid");
        want_bubble = (mode && strcmp(mode, "bubble") == 0);
        if (mode) eos_free(mode);
    }
    s_square_bubble = want_bubble;

    if (s_home && s_square_bubble)
        eos_framework_home_use_bubble(s_home);

    s_step_timer = lv_timer_create(_step_tick, 16, NULL);

    eos_event_subscribe(EOS_EVENT_APP_INSTALLED, _on_app_changed_cb, NULL);
    eos_event_subscribe(EOS_EVENT_APP_UNINSTALLED, _on_app_changed_cb, NULL);

    EOS_LOG_I("Launcher shown");
}

static void _launcher_on_destroy(eos_activity_t *activity)
{
    LV_UNUSED(activity);
    if (s_step_timer)  { lv_timer_delete(s_step_timer);  s_step_timer = NULL; }
    eos_event_unsubscribe(EOS_EVENT_APP_INSTALLED, _on_app_changed_cb);
    eos_event_unsubscribe(EOS_EVENT_APP_UNINSTALLED, _on_app_changed_cb);
    if (s_home) { eos_framework_home_destroy(s_home); s_home = NULL; }
    s_launcher_activity = NULL;
}

static const eos_activity_lifecycle_t s_launcher_lc = {
    .on_enter = _launcher_on_enter,
    .on_destroy = _launcher_on_destroy,
    .on_pause = NULL,
    .on_resume = NULL,
};

/**
 * @brief Enter the v1 adaptive Launcher as a SEPARATE page (hidden legacy
 *        implementation). The dispatcher in eos_launcher.c routes here when
 *        launcher_mode == "v1".
 */
void eos_launcher_v1_enter(void)
{
    _register_routes_once();

    eos_activity_t *a = eos_activity_create(&s_launcher_lc);
    if (!a)
    {
        EOS_LOG_E("Failed to create launcher activity");
        return;
    }
    eos_activity_set_type(a, EOS_ACTIVITY_TYPE_APP_LIST);

    eos_activity_enter(a);
}
