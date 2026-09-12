/**
 * @file eos_usb_msc.h
 * @brief USB MSC App(把 SD 卡作为 U 盘暴露给 PC)。
 *
 * 交付批次:②USB 检测与模式切换 / ③界面冻结与图标 / ④集成与退出。
 *
 * 用户行为:
 *   从未连接 → 提示 "No USB connection detected"(可退出);
 *   PC 已枚举 → 冻结 UI、禁用触摸、全屏显示 eos_icon_usb_ok;
 *   任一环节失败 → 全屏显示 eos_icon_usb_err + errcode/errmsg(全英文);
 *   拔线 / 退出 → 卸载 USB、重挂载 SD、恢复触摸与界面。
 */
#ifndef EOS_USB_MSC_H
#define EOS_USB_MSC_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE

/**
 * @brief 进入 USB MSC App(native app entry,由 App 列表点击触发)。
 * @note 必须在 LVGL(ui_task)上下文调用。
 */
void eos_usb_msc_enter(void);

#endif /* CONFIG_USB_MSC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_USB_MSC_H */
