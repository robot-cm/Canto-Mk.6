/**
 * @file cos_service_countdown.h
 * @brief Persistent countdown trigger service (Core)
 *
 * JS 插件退出后代码不再执行，无法自行到点提醒。
 * 本服务作为 Core 常驻组件：每 1s 轮询系统时间，检查 Timer App 私有
 * config.json 中的 "countdown" 字段，当某个 RUN 任务的 end 时间戳到达时
 * 将 App 拉回前台，"DONE" 提示 UI 完全由 App 内 JS 呈现。
 */
#ifndef COS_SERVICE_COUNTDOWN_H
#define COS_SERVICE_COUNTDOWN_H

#ifdef __cplusplus
extern "C" {
#endif

void cos_service_countdown_init(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_COUNTDOWN_H */
