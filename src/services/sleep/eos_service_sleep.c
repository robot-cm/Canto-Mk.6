/**
 * @file eos_service_sleep.c
 * @brief Sleep service - persistent bedtime scheduler (Core)
 *
 * Rule config format (system config key `sleeps`, JSON array):
 * [
 *   {"id":0,"on":true,"sh":22,"sm":0,"eh":7,"em":30,
 *    "days":127,   // bit0=Mon ... bit6=Sun
 *    "rep":-1,     // remaining repeats, -1 = always
 *    "ovr":[       // 例外日(按星期)拆分段,由闹钟命中自动生成/清除
 *      {"d":32,"h":23,"m":0}   // 该星期窗口被闹钟 23:00 拆成两段
 *    ]}
 * ]
 *
 * 拆分语义(数据维护,非运行时):
 *   - 闹钟创建/修改时:命中规则窗口的那一天(星期)自动拆成两段:
 *       段1 = [规则开始, 闹钟时刻)
 *       段2 = [闹钟时刻+10min, 规则结束)
 *     其他星期保持原规则不变。
 *   - 闹钟删除后:拆分段自动合并回原规则。
 *   - 拆分/合并发生在"规则或闹钟创建/删除时"(本服务每秒检测闹钟配置变化)。
 *
 * 硬件深睡:窗口内通过 eos_pm_deep_sleep_request(rem) 请求深睡,
 *   rem 为到下一个唤醒点(闹钟时刻 / 规则结束)的秒数,由 port 层
 *   eos_hw_deep_sleep() 实现真正的 esp_deep_sleep_start()。
 */

#include "eos_service_sleep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#define EOS_LOG_TAG "SleepSrv"
#include "eos_log.h"
#include "eos_service_config.h"
#include "eos_service_pm.h"
#include "eos_service_time.h"
#include "eos_service_storage.h"
#include "eos_storage_paths.h"
#include "eos_mem.h"

/* Macros and Definitions -------------------------------------*/

#define EOS_SLEEP_CONFIG_KEY "sleeps"
#define EOS_SLEEP_POLL_MS 1000 /**< 规则检查周期 */
#define EOS_SLEEP_MAX_RULES 8  /**< 最多规则数 */
#define EOS_SLEEP_MAX_OVR 7    /**< 每规则最多例外日(按星期,最多 7) */
#define EOS_SLEEP_DEEP_TAPS 8  /**< 睡眠期间深睡唤醒所需点击次数(防误触) */
#define EOS_SLEEP_DEFAULT_TAPS 5 /**< 与 PM 服务默认深睡点击次数一致 */
#define EOS_SLEEP_ALARM_10MIN (10) /**< 闹钟响后 10min 才恢复深睡(分钟单位) */

/* Alarm App 固定 id(与 apps/alarm/manifest.json 一致) */
#define EOS_ALARM_APP_ID "com.elenix.alarm"
/* App 私有配置路径:JS 侧 eos.config 读写的是同一文件 */
#define EOS_ALARM_CFG_PATH EOS_APP_DATA_DIR EOS_ALARM_APP_ID "/config.json"

/* Typedefs ---------------------------------------------------*/

/** 例外日拆分:某星期(单 bit)的窗口被闹钟(ah,am)拆成两段 */
typedef struct
{
    uint8_t days; /**< 受影响星期位图(单 bit) */
    int ah, am;   /**< 闹钟时刻(段1结束/段2起点+10min) */
} eos_sleep_ovr_t;

typedef struct
{
    int id;         /**< 规则 ID */
    int sh, sm;     /**< 开始 时/分 */
    int eh, em;     /**< 结束 时/分 */
    uint8_t days;   /**< 星期位图:bit0=Mon ... bit6=Sun */
    bool on;        /**< 启用 */
    int rep;        /**< 剩余次数:-1=永久 */
    eos_sleep_ovr_t ovr[EOS_SLEEP_MAX_OVR];
    int ovr_count;
} eos_sleep_rule_t;

/* Variables --------------------------------------------------*/

static eos_sleep_rule_t s_rules[EOS_SLEEP_MAX_RULES];
static int s_count = 0;
static int s_active = -1;       /**< 当前激活的规则索引,-1=无 */
static bool s_entered_deep = false; /**< 本次窗口是否真正进过深睡 */
static lv_timer_t *s_timer = NULL;
static char *s_last_alarms_raw = NULL; /**< 上次闹钟配置原文(变化检测) */
static char *s_last_rules_raw = NULL;  /**< 上次规则配置原文(变化检测) */

/* Function Implementations -----------------------------------*/

static uint8_t _dow_bit(uint8_t dow)
{
    return (uint8_t)(1u << ((dow + 6) % 7));
}

/** 半开区间 [s, e) 判断(分钟);e<=s 视为跨天 */
static bool _in_range(int min, int s, int e)
{
    if (s == e)
        return false;
    if (s < e)
        return min >= s && min < e;
    return min >= s || min < e;
}

/** 当前时刻是否在规则窗口内(含跨天;跨天后半段按前一天星期匹配) */
static bool _rule_active_today(const eos_sleep_rule_t *r, uint8_t dow, int now_min)
{
    if (!r->on || r->rep == 0)
        return false;

    int start = r->sh * 60 + r->sm;
    int end = r->eh * 60 + r->em;
    bool cross = (start >= end);

    uint8_t bit;
    if (cross && now_min < end)
        bit = _dow_bit((dow + 6) % 7); /* 跨天窗口后半段属于前一天 */
    else
        bit = _dow_bit(dow);
    if (!(r->days & bit))
        return false;

    /* 该归属日被闹钟拆分? 段1=[start,闹钟) ∪ 段2=[闹钟+10min, end) */
    for (int i = 0; i < r->ovr_count; i++)
    {
        if (r->ovr[i].days == bit)
        {
            int a = r->ovr[i].ah * 60 + r->ovr[i].am;
            int a2 = (a + EOS_SLEEP_ALARM_10MIN) % (24 * 60);
            return _in_range(now_min, start, a) || _in_range(now_min, a2, end);
        }
    }
    return _in_range(now_min, start, end);
}

/** 距下一唤醒点(闹钟时刻/规则结束)的剩余秒数 */
static uint32_t _remaining_sec(const eos_sleep_rule_t *r, uint8_t dow, int now_min, int now_sec)
{
    int start = r->sh * 60 + r->sm;
    int end = r->eh * 60 + r->em;
    uint8_t bit = _dow_bit(dow);
    int target = end;

    for (int i = 0; i < r->ovr_count; i++)
    {
        if (r->ovr[i].days == bit)
        {
            int a = r->ovr[i].ah * 60 + r->ovr[i].am;
            int a2 = (a + EOS_SLEEP_ALARM_10MIN) % (24 * 60);
            if (_in_range(now_min, start, a))
                target = a; /* 段1:唤醒点=闹钟时刻 */
            else if (_in_range(now_min, a2, end))
                target = end; /* 段2:唤醒点=规则结束 */
            break;
        }
    }

    if (target <= now_min)
        target += 24 * 60; /* 结束时刻在明天 */
    return (uint32_t)((target - now_min) * 60 - now_sec);
}

/** 从系统配置加载规则 */
static void _sleep_load(void)
{
    s_count = 0;
    memset(s_rules, 0, sizeof(s_rules));

    cJSON *arr = eos_config_get_json(EOS_SLEEP_CONFIG_KEY);
    if (!arr)
        return;

    if (!cJSON_IsArray(arr))
    {
        cJSON_Delete(arr);
        return;
    }

    int n = cJSON_GetArraySize(arr);
    if (n > EOS_SLEEP_MAX_RULES)
        n = EOS_SLEEP_MAX_RULES;

    for (int i = 0; i < n; i++)
    {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        if (!it || !cJSON_IsObject(it))
            continue;

        eos_sleep_rule_t *r = &s_rules[s_count];
        cJSON *v;

        r->id = (v = cJSON_GetObjectItem(it, "id")) ? v->valueint : s_count;
        r->sh = (v = cJSON_GetObjectItem(it, "sh")) ? v->valueint : 0;
        r->sm = (v = cJSON_GetObjectItem(it, "sm")) ? v->valueint : 0;
        r->eh = (v = cJSON_GetObjectItem(it, "eh")) ? v->valueint : 0;
        r->em = (v = cJSON_GetObjectItem(it, "em")) ? v->valueint : 0;
        r->days = (v = cJSON_GetObjectItem(it, "days")) ? (uint8_t)v->valueint : 0x7F;
        r->on = (v = cJSON_GetObjectItem(it, "on")) ? cJSON_IsTrue(v) : true;
        r->rep = (v = cJSON_GetObjectItem(it, "rep")) ? v->valueint : -1;

        /* 字段校验 */
        if (r->sh < 0 || r->sh > 23 || r->sm < 0 || r->sm > 59 ||
            r->eh < 0 || r->eh > 23 || r->em < 0 || r->em > 59)
            continue;

        /* 例外日拆分段(闹钟命中生成) */
        cJSON *ov = cJSON_GetObjectItem(it, "ovr");
        if (ov && cJSON_IsArray(ov))
        {
            int n_ovr = cJSON_GetArraySize(ov);
            if (n_ovr > EOS_SLEEP_MAX_OVR)
                n_ovr = EOS_SLEEP_MAX_OVR;
            for (int k = 0; k < n_ovr; k++)
            {
                cJSON *o = cJSON_GetArrayItem(ov, k);
                if (!o || !cJSON_IsObject(o))
                    continue;
                cJSON *od = cJSON_GetObjectItem(o, "d");
                cJSON *oh = cJSON_GetObjectItem(o, "h");
                cJSON *om = cJSON_GetObjectItem(o, "m");
                if (!od || !oh || !om)
                    continue;
                r->ovr[k].days = (uint8_t)od->valueint;
                r->ovr[k].ah = oh->valueint;
                r->ovr[k].am = om->valueint;
                r->ovr_count++;
            }
        }

        s_count++;
    }

    cJSON_Delete(arr);
}

/** 把当前规则写回系统配置(所有权转移给 set_json,不 delete) */
static void _sleep_save(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr)
        return;

    for (int i = 0; i < s_count; i++)
    {
        eos_sleep_rule_t *r = &s_rules[i];
        cJSON *it = cJSON_CreateObject();
        cJSON_AddNumberToObject(it, "id", r->id);
        cJSON_AddBoolToObject(it, "on", r->on);
        cJSON_AddNumberToObject(it, "sh", r->sh);
        cJSON_AddNumberToObject(it, "sm", r->sm);
        cJSON_AddNumberToObject(it, "eh", r->eh);
        cJSON_AddNumberToObject(it, "em", r->em);
        cJSON_AddNumberToObject(it, "days", r->days);
        cJSON_AddNumberToObject(it, "rep", r->rep);
        if (r->ovr_count > 0)
        {
            cJSON *ov = cJSON_CreateArray();
            for (int k = 0; k < r->ovr_count; k++)
            {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "d", r->ovr[k].days);
                cJSON_AddNumberToObject(o, "h", r->ovr[k].ah);
                cJSON_AddNumberToObject(o, "m", r->ovr[k].am);
                cJSON_AddItemToArray(ov, o);
            }
            cJSON_AddItemToObject(it, "ovr", ov);
        }
        cJSON_AddItemToArray(arr, it);
    }

    eos_config_set_json(EOS_SLEEP_CONFIG_KEY, arr);
}

/** 闹钟是否命中该规则窗口(含跨天) */
static bool _alarm_hits_rule(const eos_sleep_rule_t *r, int a_min)
{
    int start = r->sh * 60 + r->sm;
    int end = r->eh * 60 + r->em;
    return _in_range(a_min, start, end);
}

/** 单个闹钟对单条规则的拆分登记(只改受影响星期,其他不动) */
static void _rule_add_ovr_if_hit(eos_sleep_rule_t *r, int ah, int am, int adays)
{
    if (!r->on || r->rep == 0)
        return;
    int a_min = ah * 60 + am;
    if (!_alarm_hits_rule(r, a_min))
        return;
    int start = r->sh * 60 + r->sm;
    int end = r->eh * 60 + r->em;
    bool cross = (start >= end);

    for (int d = 0; d < 7; d++)
    {
        uint8_t bit = (uint8_t)(1u << d);
        if (!(adays & bit) || !(r->days & bit))
            continue;
        /* 跨天窗口凌晨段(a_min<end)归属前一天 */
        uint8_t own = bit;
        if (cross && a_min < end)
            own = (uint8_t)(1u << ((d + 6) % 7));
        if (!(r->days & own))
            continue;

        /* 同星期已拆分 → 保留更早的闹钟 */
        int k;
        for (k = 0; k < r->ovr_count; k++)
        {
            if (r->ovr[k].days == own)
            {
                if (a_min < r->ovr[k].ah * 60 + r->ovr[k].am)
                {
                    r->ovr[k].ah = ah;
                    r->ovr[k].am = am;
                }
                break;
            }
        }
        if (k == r->ovr_count && r->ovr_count < EOS_SLEEP_MAX_OVR)
        {
            r->ovr[r->ovr_count].days = own;
            r->ovr[r->ovr_count].ah = ah;
            r->ovr[r->ovr_count].am = am;
            r->ovr_count++;
        }
    }
}

/**
 * 重新计算所有规则的例外拆分段:
 *   - 遍历当前闹钟列表,命中窗口的(星期,时刻)生成 ovr;
 *   - 删除/修改闹钟后 ovr 自然消失 → 规则合并回原形。
 * 每次规则或闹钟变化后调用,结果写回系统配置。
 */
static void _sleep_sync_alarms(void)
{
    for (int i = 0; i < s_count; i++)
        s_rules[i].ovr_count = 0;

    char *data = eos_storage_read_file(EOS_ALARM_CFG_PATH);
    if (!data)
    {
        _sleep_save();
        return;
    }
    cJSON *root = cJSON_Parse(data);
    eos_free(data);
    if (!root)
    {
        _sleep_save();
        return;
    }

    cJSON *as = cJSON_GetObjectItemCaseSensitive(root, "alarms");
    if (cJSON_IsString(as) && as->valuestring)
    {
        cJSON *arr = cJSON_Parse(as->valuestring);
        if (arr && cJSON_IsArray(arr))
        {
            int n = cJSON_GetArraySize(arr);
            for (int i = 0; i < n; i++)
            {
                cJSON *a = cJSON_GetArrayItem(arr, i);
                if (!a || !cJSON_IsObject(a))
                    continue;
                cJSON *on = cJSON_GetObjectItemCaseSensitive(a, "on");
                cJSON *h = cJSON_GetObjectItemCaseSensitive(a, "h");
                cJSON *m = cJSON_GetObjectItemCaseSensitive(a, "m");
                cJSON *days = cJSON_GetObjectItemCaseSensitive(a, "days");
                if (!cJSON_IsTrue(on) || !cJSON_IsNumber(h) || !cJSON_IsNumber(m))
                    continue;
                int ah = (int)h->valuedouble;
                int am = (int)m->valuedouble;
                int adays = cJSON_IsNumber(days) ? (int)days->valuedouble : 0;
                if (adays == 0)
                    adays = 0x7F; /* 每天 */
                for (int j = 0; j < s_count; j++)
                    _rule_add_ovr_if_hit(&s_rules[j], ah, am, adays);
            }
            cJSON_Delete(arr);
        }
    }
    cJSON_Delete(root);

    _sleep_save();
}

/** 检测闹钟配置是否变化(创建/修改/删除) */
static bool _alarms_changed(void)
{
    char *data = eos_storage_read_file(EOS_ALARM_CFG_PATH);
    if (!data)
    {
        bool changed = (s_last_alarms_raw != NULL);
        if (s_last_alarms_raw)
        {
            eos_free(s_last_alarms_raw);
            s_last_alarms_raw = NULL;
        }
        return changed;
    }

    bool changed = !s_last_alarms_raw || strcmp(s_last_alarms_raw, data) != 0;
    if (changed)
    {
        size_t len = strlen(data);
        char *dup = (char *)eos_malloc(len + 1);
        if (dup)
        {
            memcpy(dup, data, len + 1);
            if (s_last_alarms_raw)
                eos_free(s_last_alarms_raw);
            s_last_alarms_raw = dup;
        }
    }
    eos_free(data);
    return changed;
}

/** 检测规则配置(sleeps)是否变化(创建/修改规则) */
static bool _rules_changed(void)
{
    cJSON *arr = eos_config_get_json(EOS_SLEEP_CONFIG_KEY);
    if (!arr)
        return false;
    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!s)
        return false;

    bool changed = !s_last_rules_raw || strcmp(s_last_rules_raw, s) != 0;
    if (changed)
    {
        size_t len = strlen(s);
        char *dup = (char *)eos_malloc(len + 1);
        if (dup)
        {
            memcpy(dup, s, len + 1);
            if (s_last_rules_raw)
                eos_free(s_last_rules_raw);
            s_last_rules_raw = dup;
        }
    }
    cJSON_free(s);
    return changed;
}

/** 每 1s 轮询:窗口内保持深睡,窗口外恢复默认点击次数并扣减次数 */
static void _sleep_poll_cb(lv_timer_t *tm)
{
    (void)tm;

    /* 规则/闹钟增删改 → 拆分/合并规则(数据维护,非运行时) */
    if (_rules_changed())
    {
        EOS_LOG_I("Sleep rule config changed, reload + re-sync splits");
        _sleep_load();
        _sleep_sync_alarms();
    }
    else if (_alarms_changed())
    {
        EOS_LOG_I("Alarm config changed, re-sync sleep rule splits");
        _sleep_sync_alarms();
    }

    eos_datetime_t now = eos_time_get();
    if (now.year < 2000)
        return; /* 时间未校准 */
    int now_min = now.hour * 60 + now.min;

    int matched = -1;
    for (int i = 0; i < s_count; i++)
    {
        if (_rule_active_today(&s_rules[i], now.day_of_week, now_min))
        {
            matched = i;
            break;
        }
    }

    eos_pm_state_t st = eos_pm_get_state();

    if (matched >= 0)
    {
        if (s_active != matched)
        {
            s_active = matched;
            s_entered_deep = false;
            EOS_LOG_I("Sleep rule #%d active: %02d:%02d - %02d:%02d (ovr=%d)",
                      s_rules[matched].id, s_rules[matched].sh, s_rules[matched].sm,
                      s_rules[matched].eh, s_rules[matched].em,
                      s_rules[matched].ovr_count);
        }

        if (st != EOS_PM_DEEP_SLEEP)
        {
            eos_pm_set_deep_sleep_wake_taps(EOS_SLEEP_DEEP_TAPS);
            uint32_t rem = _remaining_sec(&s_rules[matched], now.day_of_week, now_min, now.sec);
            if (rem < 300)
            {
                /* 下一个唤醒点(闹钟响/规则结束)在 5min 内:
                 * 不值得深睡重启,保持软睡眠让闹钟/规则自然处理 */
                EOS_LOG_I("Next wake event in <5 min (%u s), stay awake", (unsigned)rem);
                s_entered_deep = false;
                return;
            }
            EOS_LOG_I("Enter deep sleep for sleep rule (%u s)", (unsigned)rem);
            eos_pm_deep_sleep_request(rem);
            s_entered_deep = true;
        }
        return;
    }

    /* 无规则激活 */
    if (st == EOS_PM_DEEP_SLEEP && s_active >= 0)
    {
        /* 窗口已过仍处于深睡(自动唤醒 timer 兜底,这里主动唤醒防时间偏差) */
        EOS_LOG_I("Sleep rule window ended, waking up");
        eos_pm_wake_up();
    }

    if (s_active >= 0)
    {
        EOS_LOG_I("Sleep rule #%d window ended", s_rules[s_active].id);
        eos_pm_set_deep_sleep_wake_taps(EOS_SLEEP_DEFAULT_TAPS);

        if (s_entered_deep)
        {
            eos_sleep_rule_t *r = &s_rules[s_active];
            if (r->rep > 0)
            {
                r->rep--;
                if (r->rep == 0)
                {
                    r->on = false;
                    EOS_LOG_I("Sleep rule #%d used up, disabled", r->id);
                }
                _sleep_save();
            }
        }
        s_active = -1;
        s_entered_deep = false;
    }
}

/* Public functions -------------------------------------------*/

void eos_service_sleep_init(void)
{
    if (s_timer)
        return;

    _sleep_load();

    /* 初始同步一次闹钟拆分(创建/删除闹钟后的首次检测) */
    if (_alarms_changed())
        _sleep_sync_alarms();

    s_timer = lv_timer_create(_sleep_poll_cb, EOS_SLEEP_POLL_MS, NULL);
    EOS_LOG_I("Sleep service initialized (%d ms poll)", EOS_SLEEP_POLL_MS);
}
