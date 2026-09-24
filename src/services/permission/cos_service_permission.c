/**
 * @file cos_service_permission.c
 * @brief Permission service implementation
 */
#include "cos_service_permission.h"

/* Includes ---------------------------------------------------*/
#include <string.h>
#include "cJSON.h"
#include "cos_service_config.h"
#include "cos_event.h"
#include "cos_mem.h"
#define COS_LOG_TAG "Permission"
#include "cos_log.h"

/* Macros and Definitions -------------------------------------*/
#define COS_CONFIG_KEY_PERM_GRANTS "perm_grants"

/* Static lookup tables ---------------------------------------*/

/** Permission category string keys (used in manifest and JSON storage) */
static const char *_perm_category_keys[COS_PERM_CATEGORY_COUNT] = {
    [COS_PERM_CATEGORY_LOCATION] = "location",
    [COS_PERM_CATEGORY_SENSOR] = "sensor",
    [COS_PERM_CATEGORY_NOTIFICATION] = "notification",
    [COS_PERM_CATEGORY_STORAGE] = "storage",
    [COS_PERM_CATEGORY_BLUETOOTH] = "bluetooth",
    [COS_PERM_CATEGORY_AUDIO] = "audio",
    [COS_PERM_CATEGORY_HEALTH] = "health",
    [COS_PERM_CATEGORY_CONTACTS] = "contacts",
    [COS_PERM_CATEGORY_CALENDAR] = "calendar",
};

/** Permission category display name string IDs */
static const lang_string_id_t _perm_category_name_ids[COS_PERM_CATEGORY_COUNT] = {
    [COS_PERM_CATEGORY_LOCATION] = STR_ID_PERM_NAME_LOCATION,
    [COS_PERM_CATEGORY_SENSOR] = STR_ID_PERM_NAME_SENSOR,
    [COS_PERM_CATEGORY_NOTIFICATION] = STR_ID_PERM_NAME_NOTIFICATION,
    [COS_PERM_CATEGORY_STORAGE] = STR_ID_PERM_NAME_STORAGE,
    [COS_PERM_CATEGORY_BLUETOOTH] = STR_ID_PERM_NAME_BLUETOOTH,
    [COS_PERM_CATEGORY_AUDIO] = STR_ID_PERM_NAME_AUDIO,
    [COS_PERM_CATEGORY_HEALTH] = STR_ID_PERM_NAME_HEALTH,
    [COS_PERM_CATEGORY_CONTACTS] = STR_ID_PERM_NAME_CONTACTS,
    [COS_PERM_CATEGORY_CALENDAR] = STR_ID_PERM_NAME_CALENDAR,
};

/** Permission category description string IDs */
static const lang_string_id_t _perm_category_desc_ids[COS_PERM_CATEGORY_COUNT] = {
    [COS_PERM_CATEGORY_LOCATION] = STR_ID_PERM_DESC_LOCATION,
    [COS_PERM_CATEGORY_SENSOR] = STR_ID_PERM_DESC_SENSOR,
    [COS_PERM_CATEGORY_NOTIFICATION] = STR_ID_PERM_DESC_NOTIFICATION,
    [COS_PERM_CATEGORY_STORAGE] = STR_ID_PERM_DESC_STORAGE,
    [COS_PERM_CATEGORY_BLUETOOTH] = STR_ID_PERM_DESC_BLUETOOTH,
    [COS_PERM_CATEGORY_AUDIO] = STR_ID_PERM_DESC_AUDIO,
    [COS_PERM_CATEGORY_HEALTH] = STR_ID_PERM_DESC_HEALTH,
    [COS_PERM_CATEGORY_CONTACTS] = STR_ID_PERM_DESC_CONTACTS,
    [COS_PERM_CATEGORY_CALENDAR] = STR_ID_PERM_DESC_CALENDAR,
};

/** Grant state label string IDs */
static const lang_string_id_t _perm_state_label_ids[] = {
    [COS_PERM_STATE_DENIED] = STR_ID_PERM_STATE_DENIED,
    [COS_PERM_STATE_ALLOW_ONCE] = STR_ID_PERM_STATE_ALLOW_ONCE,
    [COS_PERM_STATE_ALLOW_FOREGROUND] = STR_ID_PERM_STATE_ALLOW_FOREGROUND,
    [COS_PERM_STATE_ALLOW_ALWAYS] = STR_ID_PERM_STATE_ALLOW_ALWAYS,
};

/* Function Implementations -----------------------------------*/

static void _perm_app_uninstalled_cb(cos_event_t *e)
{
    const char *app_id = (const char *)cos_event_get_param(e);
    if (!app_id)
    {
        return;
    }
    COS_LOG_D("Revoking permissions for uninstalled app: %s", app_id);
    cos_permission_revoke_all(app_id);
}

void cos_service_permission_init(void)
{
    cos_event_subscribe(COS_EVENT_APP_UNINSTALLED, _perm_app_uninstalled_cb, NULL);
    COS_LOG_I("Permission service initialized");
}

/* ---- Public API ---- */

const char *cos_permission_category_key(cos_perm_category_t cat)
{
    if (cat >= COS_PERM_CATEGORY_COUNT)
        return NULL;
    return _perm_category_keys[cat];
}

lang_string_id_t cos_permission_category_name_id(cos_perm_category_t cat)
{
    if (cat >= COS_PERM_CATEGORY_COUNT)
        return STR_ID_PERM_NAME_LOCATION;
    return _perm_category_name_ids[cat];
}

lang_string_id_t cos_permission_category_desc_id(cos_perm_category_t cat)
{
    if (cat >= COS_PERM_CATEGORY_COUNT)
        return STR_ID_PERM_DESC_LOCATION;
    return _perm_category_desc_ids[cat];
}

lang_string_id_t cos_permission_state_label_id(cos_perm_state_t state)
{
    if (state > COS_PERM_STATE_ALLOW_ALWAYS)
        return STR_ID_PERM_STATE_DENIED;
    return _perm_state_label_ids[state];
}

const char *cos_permission_category_name(cos_perm_category_t cat)
{
    return cos_lang_get_text(cos_permission_category_name_id(cat));
}

const char *cos_permission_category_desc(cos_perm_category_t cat)
{
    return cos_lang_get_text(cos_permission_category_desc_id(cat));
}

cos_perm_category_t cos_permission_name_to_category(const char *name)
{
    if (!name)
        return COS_PERM_CATEGORY_COUNT;
    for (int i = 0; i < COS_PERM_CATEGORY_COUNT; i++)
    {
        if (strcmp(name, _perm_category_keys[i]) == 0)
        {
            return (cos_perm_category_t)i;
        }
    }
    return COS_PERM_CATEGORY_COUNT;
}

cos_perm_state_t cos_permission_get(const char *app_id, cos_perm_category_t cat)
{
    if (!app_id || cat >= COS_PERM_CATEGORY_COUNT)
    {
        return COS_PERM_STATE_DENIED;
    }

    const char *cat_key = _perm_category_keys[cat];
    cJSON *grants = cos_config_get_json(COS_CONFIG_KEY_PERM_GRANTS);
    if (!grants)
    {
        return COS_PERM_STATE_DENIED;
    }

    cJSON *app_obj = cJSON_GetObjectItemCaseSensitive(grants, app_id);
    if (!app_obj)
    {
        cJSON_Delete(grants);
        return COS_PERM_STATE_DENIED;
    }

    cJSON *cat_item = cJSON_GetObjectItemCaseSensitive(app_obj, cat_key);
    if (!cat_item || !cJSON_IsNumber(cat_item))
    {
        cJSON_Delete(grants);
        return COS_PERM_STATE_DENIED;
    }

    int val = cat_item->valueint;
    cJSON_Delete(grants);

    if (val < 0 || val > COS_PERM_STATE_ALLOW_ALWAYS)
    {
        return COS_PERM_STATE_DENIED;
    }
    return (cos_perm_state_t)val;
}

bool cos_permission_set(const char *app_id, cos_perm_category_t cat, cos_perm_state_t state)
{
    if (!app_id || cat >= COS_PERM_CATEGORY_COUNT || state > COS_PERM_STATE_ALLOW_ALWAYS)
    {
        return false;
    }

    const char *cat_key = _perm_category_keys[cat];
    cJSON *grants = cos_config_get_json(COS_CONFIG_KEY_PERM_GRANTS);
    if (!grants)
    {
        grants = cJSON_CreateObject();
    }

    /* Ensure the top-level grants object exists */
    cJSON *app_obj = cJSON_GetObjectItemCaseSensitive(grants, app_id);
    if (!app_obj)
    {
        app_obj = cJSON_AddObjectToObject(grants, app_id);
    }

    /* Set/replace the category state */
    cJSON *cat_item = cJSON_GetObjectItemCaseSensitive(app_obj, cat_key);
    if (cat_item)
    {
        cJSON_ReplaceItemInObject(app_obj, cat_key, cJSON_CreateNumber(state));
    }
    else
    {
        cJSON_AddNumberToObject(app_obj, cat_key, state);
    }

    cos_result_t result = cos_config_set_json(COS_CONFIG_KEY_PERM_GRANTS, grants);
    /* Note: cos_config_set_json takes ownership of grants via AddItem/ReplaceItem,
     * then _cos_config_save_and_broadcast deletes the root tree. Do NOT delete grants here. */
    return result == COS_OK;
}

void cos_permission_revoke_all(const char *app_id)
{
    if (!app_id)
        return;

    cJSON *grants = cos_config_get_json(COS_CONFIG_KEY_PERM_GRANTS);
    if (!grants)
        return;

    cJSON_DeleteItemFromObjectCaseSensitive(grants, app_id);
    cos_config_set_json(COS_CONFIG_KEY_PERM_GRANTS, grants);
    /* Note: cos_config_set_json takes ownership of grants, do not delete here */

    COS_LOG_D("All permissions revoked for app: %s", app_id);
}
