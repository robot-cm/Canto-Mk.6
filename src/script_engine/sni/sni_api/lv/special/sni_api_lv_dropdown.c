/**
 * @file sni_api_lv_dropdown.c
 * @brief LVGL dropdown SNI special wrappers
 */

#include "sni_api_lv_special.h"

/* Includes ---------------------------------------------------*/
#include "lvgl.h"
#include "sni_api_export.h"
#include "sni_type_bridge.h"
#include "sni_types.h"
#include "cos_mem.h"

/* Macros and Definitions -------------------------------------*/
#define SNI_LV_STR_SLOT_COUNT 2

/* LVGL 的 lv_dropdown_set_text / lv_dropdown_set_symbol 只保存传入指针、不做复制。
 * JS 侧传入的是 sni_tb_js2c_string 分配的临时串,直接交给它们会在临时串释放后悬垂,
 * 不释放则每次调用泄漏。这里把字符串复制一份挂到对象上:替换时释放旧值,对象删除时
 * 经 LV_EVENT_DELETE 自动释放。存储指针借 LVGL 的 event descriptor 承载(与
 * lv_gridnav / lv_menu 的做法一致),避免占用 obj 的 user_data(SNI 框架已占用)。 */
typedef struct
{
    char *values[SNI_LV_STR_SLOT_COUNT];
} sni_lv_str_owned_t;

/* Function Implementations -----------------------------------*/

static void sni_lv_str_owned_delete_cb(lv_event_t *e)
{
    sni_lv_str_owned_t *owned = (sni_lv_str_owned_t *)lv_event_get_user_data(e);
    if (!owned)
        return;

    for (int i = 0; i < SNI_LV_STR_SLOT_COUNT; i++)
    {
        if (owned->values[i])
            cos_free(owned->values[i]);
    }
    cos_free(owned);
}

static sni_lv_str_owned_t *sni_lv_str_owned_find(lv_obj_t *obj)
{
    uint32_t cnt = lv_obj_get_event_count(obj);
    for (uint32_t i = 0; i < cnt; i++)
    {
        lv_event_dsc_t *dsc = lv_obj_get_event_dsc(obj, i);
        if (dsc && lv_event_dsc_get_cb(dsc) == sni_lv_str_owned_delete_cb)
            return (sni_lv_str_owned_t *)lv_event_dsc_get_user_data(dsc);
    }
    return NULL;
}

static sni_lv_str_owned_t *sni_lv_str_owned_ensure(lv_obj_t *obj)
{
    sni_lv_str_owned_t *owned = sni_lv_str_owned_find(obj);
    if (owned)
        return owned;

    owned = (sni_lv_str_owned_t *)cos_malloc_zeroed(sizeof(*owned));
    if (!owned)
        return NULL;

    lv_obj_add_event_cb(obj, sni_lv_str_owned_delete_cb, LV_EVENT_DELETE, owned);
    return owned;
}

void sni_lv_str_bind(lv_obj_t *obj, sni_lv_str_slot_t slot, const char *value)
{
    if (!obj || slot < 0 || slot >= SNI_LV_STR_SLOT_COUNT)
        return;

    sni_lv_str_owned_t *owned = sni_lv_str_owned_ensure(obj);
    if (!owned)
        return;

    char *dup = NULL;
    if (value)
    {
        dup = cos_strdup(value);
        if (!dup)
            return;
    }

    if (owned->values[slot])
        cos_free(owned->values[slot]);
    owned->values[slot] = dup;

    switch (slot)
    {
    case SNI_LV_STR_SLOT_DROPDOWN_TEXT:
        lv_dropdown_set_text(obj, dup);
        break;
    case SNI_LV_STR_SLOT_DROPDOWN_SYMBOL:
        lv_dropdown_set_symbol(obj, dup);
        break;
    default:
        break;
    }
}

jerry_value_t sni_api_lv_dropdown_set_symbol(const jerry_call_info_t *call_info_p,
                                             const jerry_value_t args_p[],
                                             const jerry_length_t args_count)
{
    lv_obj_t *self_obj = NULL;
    const char *arg_symbol = NULL;

    if (args_count != 1)
    {
        return sni_api_throw_error("Invalid argument count");
    }

    if (!sni_tb_js2c(call_info_p->this_value, SNI_H_LV_OBJ, &self_obj))
    {
        return sni_api_throw_error("Failed to convert argument");
    }

    if (!jerry_value_is_null(args_p[0]))
    {
        if (!jerry_value_is_string(args_p[0]))
        {
            return sni_api_throw_error("Invalid argument type");
        }
        arg_symbol = sni_tb_js2c_string(args_p[0]);
    }

    /* lv_dropdown_set_symbol 只保存指针:绑定持久副本,再释放临时串 */
    sni_lv_str_bind(self_obj, SNI_LV_STR_SLOT_DROPDOWN_SYMBOL, arg_symbol);
    if (arg_symbol)
        cos_free((void *)arg_symbol);

    return jerry_undefined();
}

jerry_value_t sni_api_prop_set_dropdown_symbol(const jerry_call_info_t *call_info_p,
                                               const jerry_value_t args_p[],
                                               const jerry_length_t args_count)
{
    return sni_api_lv_dropdown_set_symbol(call_info_p, args_p, args_count);
}
