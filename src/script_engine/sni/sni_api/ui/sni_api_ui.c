/**
 * @file sni_api_ui.c
 * @brief JerryScript bridge for the CantoMk6 adaptive UI framework.
 *
 * Exposes the P8 rendering layer (DisplayProfile, safe-area, Liquid Glass,
 * ArcList/LayoutManager Home) to JavaScript as `cos.ui.*`. A UI written in JS
 * therefore becomes screen-shape adaptive automatically, using the exact same
 * code path the native Launcher uses.
 */

#include "sni_api_ui.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdlib.h>
#include "lvgl.h"
#include "cos_log.h"
#include "cos_mem.h"
#include "cos_framework_home.h"
#include "cos_display_profile.h"
#include "cos_display_profiles.h"
#include "cos_liquid_glass.h"
#include "sni_api_export.h"
#include "sni_type_bridge.h"
#include "sni_types.h"
#include "sni_context.h"
#include "sni_callback_runtime.h"
#include "script_engine_core.h"
#include "spm.h"

/* Macros and Definitions -------------------------------------*/

#define COS_UI_LOG_TAG "SNI-UI"

/* Variables --------------------------------------------------*/

static jerry_value_t ui_api_obj = 0;

/** @brief Per-home JS bridge wrapper. Keeps the native home + JS onSelect. */
typedef struct
{
    cos_framework_home_t *home;
    jerry_value_t js_on_select;   /**< persistent reference to the JS callback */
    struct script_program *owner; /**< program to invoke via spm_call          */
} sni_ui_home_t;

/* Function Implementations -----------------------------------*/

/* Inline method registrar (mirrors sni_api_export's sni_register_methods,
 * which is file-static and therefore not reusable here). */
static void ui_register_methods(const sni_method_desc_t *methods, jerry_value_t target)
{
    if (!methods)
        return;
    for (size_t i = 0; methods[i].name != NULL && methods[i].handler != NULL; i++)
    {
        jerry_value_t func = jerry_function_external(methods[i].handler);
        if (jerry_value_is_exception(func))
        {
            jerry_value_free(func);
            return;
        }
        jerry_value_t set = jerry_object_set_sz(target, methods[i].name, func);
        jerry_value_free(func);
        jerry_value_free(set);
    }
}

/* Forward declaration so home() can wire it as the C on_select. */
static void sni_api_ui_home_on_select(int index, void *user);

/* ---- profileForId(id) -> profile handle ---------------------------------- */
static jerry_value_t sni_api_ui_profile_for_id(const jerry_call_info_t *call_info_p,
                                               const jerry_value_t args_p[],
                                               const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1 || !jerry_value_is_number(args_p[0]))
        return sni_api_throw_error("Usage: ui.profileForId(id)");

    cos_profile_id_t id = (cos_profile_id_t)(int32_t)jerry_value_as_number(args_p[0]);
    cos_display_profile_t *p = cos_malloc(sizeof(*p));
    if (!p)
        return sni_api_throw_error("Out of memory");
    *p = cos_display_profiles_get(id);
    return sni_tb_c2js(&p, SNI_H_COS_UI_PROFILE);
}

/* ---- makeProfile(shape, w, h) -> profile handle -------------------------- */
static jerry_value_t sni_api_ui_make_profile(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 3 || !jerry_value_is_number(args_p[0]) ||
        !jerry_value_is_number(args_p[1]) || !jerry_value_is_number(args_p[2]))
        return sni_api_throw_error("Usage: ui.makeProfile(shape, w, h)");

    cos_display_shape_t shape = (cos_display_shape_t)(int32_t)jerry_value_as_number(args_p[0]);
    int w = (int)jerry_value_as_number(args_p[1]);
    int h = (int)jerry_value_as_number(args_p[2]);

    cos_display_profile_t *p = cos_malloc(sizeof(*p));
    if (!p)
        return sni_api_throw_error("Out of memory");
    *p = cos_display_profile_create(shape, w, h);
    return sni_tb_c2js(&p, SNI_H_COS_UI_PROFILE);
}

/* ---- safeArea(profile) -> {shape,width,height,center_x,...,dp_scale} ----- */
static jerry_value_t sni_api_ui_safe_area(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1)
        return sni_api_throw_error("Usage: ui.safeArea(profile)");

    cos_display_profile_t *p = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_PROFILE, &p))
        return sni_api_throw_error("Invalid profile handle");

    jerry_value_t obj = jerry_object();
    if (!jerry_value_is_object(obj))
        return obj;
    script_engine_set_prop_number(obj, "shape", (double)p->shape);
    script_engine_set_prop_number(obj, "width", (double)p->width);
    script_engine_set_prop_number(obj, "height", (double)p->height);
    script_engine_set_prop_number(obj, "center_x", (double)p->center_x);
    script_engine_set_prop_number(obj, "center_y", (double)p->center_y);
    script_engine_set_prop_number(obj, "radius", (double)p->radius);
    script_engine_set_prop_number(obj, "safe_radius", (double)p->safe_radius);
    script_engine_set_prop_number(obj, "safe_x", (double)p->safe_x);
    script_engine_set_prop_number(obj, "safe_y", (double)p->safe_y);
    script_engine_set_prop_number(obj, "safe_w", (double)p->safe_w);
    script_engine_set_prop_number(obj, "safe_h", (double)p->safe_h);
    script_engine_set_prop_number(obj, "aspect_ratio", (double)p->aspect_ratio);
    script_engine_set_prop_number(obj, "dp_scale", (double)p->dp_scale);
    return obj;
}

/* ---- profileIsInside(profile, x, y, r) -> bool --------------------------- */
static jerry_value_t sni_api_ui_profile_is_inside(const jerry_call_info_t *call_info_p,
                                                  const jerry_value_t args_p[],
                                                  const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 4 || !jerry_value_is_number(args_p[1]) ||
        !jerry_value_is_number(args_p[2]) || !jerry_value_is_number(args_p[3]))
        return sni_api_throw_error("Usage: ui.profileIsInside(profile, x, y, r)");

    cos_display_profile_t *p = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_PROFILE, &p))
        return sni_api_throw_error("Invalid profile handle");

    float x = (float)jerry_value_as_number(args_p[1]);
    float y = (float)jerry_value_as_number(args_p[2]);
    float r = (float)jerry_value_as_number(args_p[3]);
    return jerry_boolean(cos_display_profile_is_inside(p, x, y, r));
}

/* ---- profileDestroy(profile) -> undefined -------------------------------- */
static jerry_value_t sni_api_ui_profile_destroy(const jerry_call_info_t *call_info_p,
                                                const jerry_value_t args_p[],
                                                const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1)
        return sni_api_throw_error("Usage: ui.profileDestroy(profile)");

    cos_display_profile_t *p = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_PROFILE, &p))
        return sni_api_throw_error("Invalid profile handle");

    cos_free(p);
    sni_tb_clear_resource_native_ptr(args_p[0]);
    return jerry_undefined();
}

/* ---- liquidGlass(obj) -> undefined --------------------------------------- */
static jerry_value_t sni_api_ui_liquid_glass(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1)
        return sni_api_throw_error("Usage: ui.liquidGlass(obj)");

    lv_obj_t *obj = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &obj))
        return sni_api_throw_error("Invalid LVGL object");
    cos_liquid_glass_card(obj);
    return jerry_undefined();
}

/* ---- home(view, profile, apps, onSelect) -> home handle ------------------ */
static jerry_value_t sni_api_ui_home(const jerry_call_info_t *call_info_p,
                                     const jerry_value_t args_p[],
                                     const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count < 3)
        return sni_api_throw_error("Usage: ui.home(view, profile, apps, onSelect)");

    lv_obj_t *view = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_LV_OBJ, &view))
        return sni_api_throw_error("Invalid view object");
    cos_display_profile_t *p = NULL;
    if (!sni_tb_js2c(args_p[1], SNI_H_COS_UI_PROFILE, &p))
        return sni_api_throw_error("Invalid profile handle");
    if (!jerry_value_is_array(args_p[2]))
        return sni_api_throw_error("apps must be an array of strings");

    uint32_t n = jerry_array_length(args_p[2]);
    const char **names = cos_malloc((n ? n : 1) * sizeof(char *));
    if (!names)
        return sni_api_throw_error("Out of memory");
    for (uint32_t i = 0; i < n; i++)
    {
        jerry_value_t item = jerry_object_get_index(args_p[2], i);
        const char *s = sni_tb_js2c_string(item);
        jerry_value_free(item);
        /* Keep NULL if conversion failed; the free loop below skips NULL. */
        names[i] = s;
    }

    jerry_value_t on_select = jerry_undefined();
    if (args_count >= 4 && jerry_value_is_function(args_p[3]))
        on_select = jerry_value_copy(args_p[3]);

    cos_framework_home_t *native =
        cos_framework_home_create(view, p, (const char **)names, (int)n, NULL);

    /* Names are copied into LVGL labels by the view; free the temp array. */
    for (uint32_t i = 0; i < n; i++)
        if (names[i]) cos_free((void *)names[i]);
    cos_free(names);

    if (!native)
    {
        jerry_value_free(on_select);
        return sni_api_throw_error("Failed to create home");
    }

    sni_ui_home_t *wrap = cos_malloc(sizeof(*wrap));
    if (!wrap)
    {
        cos_framework_home_destroy(native);
        jerry_value_free(on_select);
        return sni_api_throw_error("Out of memory");
    }
    memset(wrap, 0, sizeof(*wrap));
    wrap->home = native;
    wrap->js_on_select = on_select;
    sni_context_t *ctx = sni_cb_get_context();
    wrap->owner = ctx ? ctx->owner : NULL;

    cos_framework_home_set_on_select(native, sni_api_ui_home_on_select, wrap);
    return sni_tb_c2js(&wrap, SNI_H_COS_UI_HOME);
}

/* ---- homeDestroy(home) -> undefined (static method, home as arg 0) ------- */
static jerry_value_t sni_api_ui_home_destroy(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1)
        return sni_api_throw_error("Usage: ui.homeDestroy(home)");

    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");

    if (wrap->home)
    {
        /* Kill any in-flight page-open anim before deleting the root. */
        lv_anim_del_all();
        cos_framework_home_destroy(wrap->home);
        wrap->home = NULL;
    }
    if (!jerry_value_is_undefined(wrap->js_on_select) &&
        !jerry_value_is_null(wrap->js_on_select))
        jerry_value_free(wrap->js_on_select);
    cos_free(wrap);
    sni_tb_clear_resource_native_ptr(args_p[0]);
    return jerry_undefined();
}

/* ---- homeStatusbarRect(home) -> {x,y,w,h} -------------------------------- */
static jerry_value_t sni_api_ui_home_statusbar_rect(const jerry_call_info_t *call_info_p,
                                                    const jerry_value_t args_p[],
                                                    const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1)
        return sni_api_throw_error("Usage: ui.homeStatusbarRect(home)");

    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");

    float x, y, w, h;
    cos_framework_home_statusbar_rect(wrap->home, &x, &y, &w, &h);
    jerry_value_t obj = jerry_object();
    script_engine_set_prop_number(obj, "x", (double)x);
    script_engine_set_prop_number(obj, "y", (double)y);
    script_engine_set_prop_number(obj, "w", (double)w);
    script_engine_set_prop_number(obj, "h", (double)h);
    return obj;
}

/* ---- homeCount(home) -> int ---------------------------------------------- */
static jerry_value_t sni_api_ui_home_count(const jerry_call_info_t *call_info_p,
                                           const jerry_value_t args_p[],
                                           const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1)
        return sni_api_throw_error("Usage: ui.homeCount(home)");

    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");
    return jerry_number((double)cos_framework_home_count(wrap->home));
}

/* ---- homeItemCenter(home, i) -> {x,y} | undefined ------------------------ */
static jerry_value_t sni_api_ui_home_item_center(const jerry_call_info_t *call_info_p,
                                                 const jerry_value_t args_p[],
                                                 const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 2 || !jerry_value_is_number(args_p[1]))
        return sni_api_throw_error("Usage: ui.homeItemCenter(home, i)");

    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");

    int i = (int)jerry_value_as_number(args_p[1]);
    int cnt = cos_framework_home_count(wrap->home);
    if (i < 0 || i >= cnt)
        return jerry_undefined();

    float x, y;
    cos_framework_home_item_center(wrap->home, i, &x, &y);
    jerry_value_t obj = jerry_object();
    script_engine_set_prop_number(obj, "x", (double)x);
    script_engine_set_prop_number(obj, "y", (double)y);
    return obj;
}

/* ---- homeStep(home, dtMs) / homeDrag(home, dy) / homeRelease(home) ------- */
static jerry_value_t sni_api_ui_home_step(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 2 || !jerry_value_is_number(args_p[1]))
        return sni_api_throw_error("Usage: ui.homeStep(home, dtMs)");
    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");
    cos_framework_home_step(wrap->home, (float)jerry_value_as_number(args_p[1]));
    return jerry_undefined();
}

static jerry_value_t sni_api_ui_home_drag(const jerry_call_info_t *call_info_p,
                                          const jerry_value_t args_p[],
                                          const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 2 || !jerry_value_is_number(args_p[1]))
        return sni_api_throw_error("Usage: ui.homeDrag(home, dy)");
    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");
    cos_framework_home_drag(wrap->home, (float)jerry_value_as_number(args_p[1]));
    return jerry_undefined();
}

static jerry_value_t sni_api_ui_home_release(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 1)
        return sni_api_throw_error("Usage: ui.homeRelease(home)");
    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");
    cos_framework_home_release(wrap->home);
    return jerry_undefined();
}

/* ---- homeSelect(home, idx) -> fires the JS onSelect (proves JS<->C<->JS) - */
static jerry_value_t sni_api_ui_home_select(const jerry_call_info_t *call_info_p,
                                            const jerry_value_t args_p[],
                                            const jerry_length_t args_count)
{
    (void)call_info_p;
    if (args_count != 2 || !jerry_value_is_number(args_p[1]))
        return sni_api_throw_error("Usage: ui.homeSelect(home, idx)");
    sni_ui_home_t *wrap = NULL;
    if (!sni_tb_js2c(args_p[0], SNI_H_COS_UI_HOME, &wrap))
        return sni_api_throw_error("Invalid home handle");
    sni_api_ui_home_on_select((int)jerry_value_as_number(args_p[1]), wrap);
    return jerry_undefined();
}

/* ---- C on_select invoked by the native home (card tap) or homeSelect ----- */
static void sni_api_ui_home_on_select(int index, void *user)
{
    sni_ui_home_t *wrap = (sni_ui_home_t *)user;
    if (!wrap || wrap->owner == NULL)
        return;
    if (jerry_value_is_undefined(wrap->js_on_select) ||
        jerry_value_is_null(wrap->js_on_select))
        return;

    jerry_value_t args[1];
    args[0] = jerry_number((double)index);
    jerry_value_t ret = spm_call(wrap->owner, wrap->js_on_select,
                                 jerry_undefined(), args, 1);
    jerry_value_free(ret);
    jerry_value_free(args[0]);
}

/* ---- Static method table + class description ----------------------------- */
static const sni_method_desc_t ui_static_methods[] = {
    {.name = "profileForId",   .handler = sni_api_ui_profile_for_id},
    {.name = "makeProfile",    .handler = sni_api_ui_make_profile},
    {.name = "safeArea",       .handler = sni_api_ui_safe_area},
    {.name = "profileIsInside",.handler = sni_api_ui_profile_is_inside},
    {.name = "profileDestroy", .handler = sni_api_ui_profile_destroy},
    {.name = "liquidGlass",    .handler = sni_api_ui_liquid_glass},
    {.name = "home",           .handler = sni_api_ui_home},
    {.name = "homeDestroy",    .handler = sni_api_ui_home_destroy},
    {.name = "homeStatusbarRect", .handler = sni_api_ui_home_statusbar_rect},
    {.name = "homeCount",      .handler = sni_api_ui_home_count},
    {.name = "homeItemCenter", .handler = sni_api_ui_home_item_center},
    {.name = "homeStep",       .handler = sni_api_ui_home_step},
    {.name = "homeDrag",       .handler = sni_api_ui_home_drag},
    {.name = "homeRelease",    .handler = sni_api_ui_home_release},
    {.name = "homeSelect",     .handler = sni_api_ui_home_select},
    {.name = NULL, .handler = NULL},
};

static const sni_constant_desc_t ui_constants[] = {
    {.name = "PROFILE_COUNT",   .type = SNI_CONST_INT, .value.i = COS_PROFILE_COUNT},
    {.name = "SHAPE_CIRCLE",    .type = SNI_CONST_INT, .value.i = COS_DISPLAY_SHAPE_CIRCLE},
    {.name = "SHAPE_SQUARE",    .type = SNI_CONST_INT, .value.i = COS_DISPLAY_SHAPE_SQUARE},
    {.name = "SHAPE_RECTANGLE", .type = SNI_CONST_INT, .value.i = COS_DISPLAY_SHAPE_RECTANGLE},
    {.name = NULL, .type = SNI_CONST_INT, .value.i = 0},
};

void sni_api_ui_init(void)
{
    ui_api_obj = jerry_object();
    if (!jerry_value_is_object(ui_api_obj))
    {
        COS_LOG_E("[%s] failed to create ui API object", COS_UI_LOG_TAG);
        return;
    }
    ui_register_methods(ui_static_methods, ui_api_obj);
    if (!sni_api_register_constants(ui_constants, ui_api_obj))
    {
        COS_LOG_E("[%s] failed to register ui constants", COS_UI_LOG_TAG);
    }
}

void sni_api_ui_mount(jerry_value_t realm)
{
    jerry_value_t cos_obj = jerry_object_get_sz(realm, "cos");
    if (!jerry_value_is_object(cos_obj))
    {
        COS_LOG_E("[%s] cos namespace not found in realm", COS_UI_LOG_TAG);
        jerry_value_free(cos_obj);
        return;
    }
    jerry_value_t set = jerry_object_set_sz(cos_obj, "ui", ui_api_obj);
    jerry_value_free(set);
    jerry_value_free(cos_obj);
}
