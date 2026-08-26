/**
 * @file eos_service_countdown.c
 * @brief Persistent countdown trigger service (Core)
 *
 * 触发链路（退出 App 后仍能到点提醒）：
 *
 *   Timer App (JS)                     Core (本服务, C)
 *   ──────────────────                 ─────────────────────────
 *   eos.config.setStr("countdown",...) ─► 写入 config.json
 *        （任务列表 JSON 字符串）         每 1s 轮询 eos_time_get()
 *        RUN 任务带 end（绝对秒）        匹配 state==RUN && end<=now
 *                                      命中 ─► eos_app_launch_immediately()
 *   App 被拉回前台 ──────────────────── 由 JS 补算剩余时间并展示 DONE
 *
 * 数据契约（JS <-> C，仅读，C 绝不写回 App 数据）：
 *   config.json -> "countdown": "[{...}]"，数组元素：
 *     id     : 自增编号（App 内展示 #N）
 *     dur    : 总秒数
 *     remain : 剩余秒（最后一次持久化的值，JS 用于补算）
 *     end    : 结束绝对秒（仅 RUN 状态有效，PAUSE/READY/DONE 为 0）
 *     state  : READY | RUN | PAUSE | DONE
 *
 * 去重策略：同一 (id, end) 最多 launch 一次（内存 _last_id/_last_end），
 * 防止 1s 轮询在命中秒内重复拉起 App。launch 失败则本秒可重试。
 * JS 侧收到 launch 后会把任务置为 DONE 并写回 config，服务读到
 * state!=RUN 即不再触发。
 */
#include "eos_service_countdown.h"

#include <stdint.h>
#include <string.h>

#include "lvgl.h"
#include "cJSON.h"

#include "eos_service_time.h"
#include "eos_service_storage.h"
#include "eos_storage_paths.h"
#include "eos_app_list.h"
#include "script_engine_core.h"
#include "eos_log.h"
#include "eos_mem.h"

/* Timer App 固定 id（与 apps/timer/manifest.json 的 id 一致） */
#define EOS_COUNTDOWN_APP_ID "com.elenix.timer"
/* App 私有配置路径：JS 侧 eos.config 读写的是同一文件 */
#define EOS_COUNTDOWN_CFG_PATH EOS_APP_DATA_DIR EOS_COUNTDOWN_APP_ID "/config.json"

static lv_timer_t *_countdown_timer = NULL;
static int _last_id = -1;   /* 最近一次 launch 的任务 id */
static int64_t _last_end = 0; /* 最近一次 launch 的任务 end（绝对秒） */

/* 民事日期 -> 秒（Howard Hinnant days_from_civil），与 JS 侧 dateToSec 一致 */
static int64_t _date_to_sec(int y, int m, int d, int h, int mi, int s)
{
    y -= (m <= 2) ? 1 : 0;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp = (unsigned)(m + (m > 2 ? -3 : 9));
    unsigned doy = (153 * mp + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

static int64_t _now_sec(eos_datetime_t t)
{
    return _date_to_sec(t.year, t.month, t.day, t.hour, t.min, t.sec);
}

static void _countdown_tick_cb(lv_timer_t *timer)
{
    (void)timer;

    eos_datetime_t now = eos_time_get();
    if (now.year < 2000)
    {
        return; /* 时间未校准，不触发 */
    }

    char *data = eos_storage_read_file(EOS_COUNTDOWN_CFG_PATH);
    if (!data)
    {
        return;
    }
    cJSON *root = cJSON_Parse(data);
    eos_free(data);
    if (!root)
    {
        return;
    }

    int matched_id = -1;
    int64_t matched_end = 0;
    cJSON *str = cJSON_GetObjectItemCaseSensitive(root, "countdown");
    if (cJSON_IsString(str) && str->valuestring)
    {
        cJSON *arr = cJSON_Parse(str->valuestring);
        if (arr && cJSON_IsArray(arr))
        {
            int n = cJSON_GetArraySize(arr);
            int64_t now_sec = _now_sec(now);
            for (int i = 0; i < n && matched_id < 0; i++)
            {
                cJSON *a = cJSON_GetArrayItem(arr, i);
                if (!a || !cJSON_IsObject(a))
                {
                    continue;
                }
                cJSON *state = cJSON_GetObjectItemCaseSensitive(a, "state");
                cJSON *end = cJSON_GetObjectItemCaseSensitive(a, "end");
                cJSON *id = cJSON_GetObjectItemCaseSensitive(a, "id");
                if (!cJSON_IsString(state) || strcmp(state->valuestring, "RUN") != 0)
                {
                    continue;
                }
                if (!cJSON_IsNumber(end) || end->valuedouble <= 0)
                {
                    continue;
                }
                int64_t e = (int64_t)end->valuedouble;
                if (e > now_sec)
                {
                    continue;
                }
                matched_end = e;
                matched_id = cJSON_IsNumber(id) ? (int)id->valuedouble : -1;
            }
            cJSON_Delete(arr);
        }
    }
    cJSON_Delete(root);

    if (matched_id < 0)
    {
        return;
    }
    /* 同一 (id, end) 已 launch 过 → 跳过（JS 未及写回 DONE 时靠它防重） */
    if (matched_id == _last_id && matched_end == _last_end)
    {
        return;
    }

    /* 前台已是 Timer App：JS 自行检测 DONE，无需 launch */
    char *cur = script_engine_get_current_script_id();
    if (cur && strcmp(cur, EOS_COUNTDOWN_APP_ID) == 0)
    {
        return;
    }

    if (eos_app_launch_immediately(EOS_COUNTDOWN_APP_ID) == EOS_OK)
    {
        _last_id = matched_id;
        _last_end = matched_end;
    }
    /* launch 失败（如切换动画中）→ 不记录去重键，下一 tick 重试 */
}

void eos_service_countdown_init(void)
{
    if (_countdown_timer)
    {
        return;
    }
    _countdown_timer = lv_timer_create(_countdown_tick_cb, 1000, NULL);
    lv_timer_set_repeat_count(_countdown_timer, -1);
    EOS_LOG_I("Countdown service init (1s periodic check, app=%s)", EOS_COUNTDOWN_APP_ID);
}
