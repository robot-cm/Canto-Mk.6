/**
 * @file eos_service_alarm.h
 * @brief Persistent alarm trigger service (Core)
 *
 * JS 插件退出后代码不再执行，无法自行到点提醒。
 * 本服务作为 Core 常驻组件：每 1s 轮询系统时间，检查 Alarm App 私有
 * config.json 中的 "alarms" 字段，命中到点闹钟时将 App 拉回前台，
 * 响铃 UI（白屏闪烁 / 关闭 / 贪睡）完全由 App 内 JS 呈现。
 */
#ifndef EOS_SERVICE_ALARM_H
#define EOS_SERVICE_ALARM_H

#ifdef __cplusplus
extern "C" {
#endif

void eos_service_alarm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SERVICE_ALARM_H */
