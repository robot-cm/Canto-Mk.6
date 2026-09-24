/**
 * @file cos_service_alarm.c
 * @brief Persistent alarm trigger service (Core)
 *
 * 触发链路（退出 App 后仍能响铃）：
 *
 *   Alarm App (JS)                    Core (本服务, C)
 *   ──────────────────                ─────────────────────────
 *   cos.config.setStr("alarms", ...)  ─► 写入 config.json
 *        （闹钟列表 JSON 字符串）        每 1s 轮询 cos_time_get()
 *                                      匹配 h/m + 星期位掩码 + 未响去重
 *                                      命中 ─► cos_app_launch_immediately()
 *   App 被拉回前台 ──────────────────── 响铃 UI 由 JS 呈现
 *
 * 数据契约（JS <-> C，仅读，C 绝不写回 App 数据）：
 *   config.json -> "alarms": "[{...}]"，数组元素：
 *     id   : 自增编号
 *     h/m  : 触发小时/分钟
 *     days : 位掩码 bit0=周一 ... bit6=周日；0 = 每天
 *     rep  : -1=无限次数；N>0=剩余次数（由 JS 在响铃时扣减）
 *     on   : 启用
 *     lf   : 最近响铃分钟键 YYYYMMDDHHMM（JS 写入，C 只读去重）
 *
 * 去重策略：同一"分钟键"最多 launch 一次（内存 _last_launch_key），
 * 防止 1s 轮询在命中分钟内重复拉起 App。launch 失败则本分钟可重试。
 */
#include "cos_service_alarm.h"

#include <string.h>

#include "lvgl.h"
#include "cJSON.h"

#include "cos_service_time.h"
#include "cos_service_storage.h"
#include "cos_storage_paths.h"
#include "cos_app_list.h"
#include "script_engine_core.h"
#include "cos_log.h"
#include "cos_mem.h"

/* Alarm App 固定 id（与 apps/alarm/manifest.json 的 id 一致） */
#define COS_ALARM_APP_ID "com.cantomk6.alarm"
/* App 私有配置路径：JS 侧 cos.config 读写的是同一文件 */
#define COS_ALARM_CFG_PATH COS_APP_DATA_DIR COS_ALARM_APP_ID "/config.json"

static lv_timer_t *_alarm_timer = NULL;
static int _last_launch_key = 0; /* YYYYMMDDHHMM */

static void _alarm_tick_cb(lv_timer_t *timer)
{
    (void)timer;

    cos_datetime_t now = cos_time_get();
    if (now.year < 2000)
    {
        return; /* 时间未校准，不触发也不推进去重键 */
    }

    int key = now.year * 1000000 + now.month * 10000 + now.day * 100 +
              now.hour * 100 + now.min;
    if (key == _last_launch_key)
    {
        return; /* 本分钟已处理过 */
    }

    char *data = cos_storage_read_file(COS_ALARM_CFG_PATH);
    if (!data)
    {
        _last_launch_key = key;
        return;
    }
    cJSON *root = cJSON_Parse(data);
    cos_free(data);
    if (!root)
    {
        _last_launch_key = key;
        return;
    }

    int matched = 0;
    cJSON *alarms_str = cJSON_GetObjectItemCaseSensitive(root, "alarms");
    if (cJSON_IsString(alarms_str) && alarms_str->valuestring)
    {
        cJSON *arr = cJSON_Parse(alarms_str->valuestring);
        if (arr && cJSON_IsArray(arr))
        {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n && !matched; i++)
            {
                cJSON *a = cJSON_GetArrayItem(arr, i);
                if (!a || !cJSON_IsObject(a))
                {
                    continue;
                }
                cJSON *on = cJSON_GetObjectItemCaseSensitive(a, "on");
                cJSON *h = cJSON_GetObjectItemCaseSensitive(a, "h");
                cJSON *m = cJSON_GetObjectItemCaseSensitive(a, "m");
                cJSON *days = cJSON_GetObjectItemCaseSensitive(a, "days");
                cJSON *lf = cJSON_GetObjectItemCaseSensitive(a, "lf");
                if (!cJSON_IsTrue(on) || !cJSON_IsNumber(h) || !cJSON_IsNumber(m))
                {
                    continue;
                }
                if ((int)h->valuedouble != now.hour || (int)m->valuedouble != now.min)
                {
                    continue;
                }
                /* 本分钟已响过（JS 在响铃开始时写入 lf）→ 跳过 */
                if (cJSON_IsNumber(lf) && (int)lf->valuedouble == key)
                {
                    continue;
                }
                /* days==0 表示每天；否则位掩码匹配今天星期 */
                int d = cJSON_IsNumber(days) ? (int)days->valuedouble : 0;
                if (d != 0)
                {
                    /* day_of_week: 0=周日 ... 6=周六 → idx: 0=周一 ... 6=周日 */
                    int idx = (now.day_of_week + 6) % 7;
                    if (!(d & (1 << idx)))
                    {
                        continue;
                    }
                }
                matched = 1;
            }
            cJSON_Delete(arr);
        }
    }
    cJSON_Delete(root);

    if (!matched)
    {
        _last_launch_key = key;
        return;
    }

    /* 前台已是 Alarm App：JS 自行检测响铃，无需 launch */
    char *cur = script_engine_get_current_script_id();
    if (cur && strcmp(cur, COS_ALARM_APP_ID) == 0)
    {
        _last_launch_key = key;
        return;
    }

    if (cos_app_launch_immediately(COS_ALARM_APP_ID) == COS_OK)
    {
        _last_launch_key = key;
    }
    /* launch 失败（如切换动画中）→ 不推进去重键，下一 tick 重试 */
}

void cos_service_alarm_init(void)
{
    if (_alarm_timer)
    {
        return;
    }
    _alarm_timer = lv_timer_create(_alarm_tick_cb, 1000, NULL);
    lv_timer_set_repeat_count(_alarm_timer, -1);
    COS_LOG_I("Alarm service init (1s periodic check, app=%s)", COS_ALARM_APP_ID);
}
