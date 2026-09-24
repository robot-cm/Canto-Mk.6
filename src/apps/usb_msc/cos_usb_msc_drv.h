/**
 * @file cos_usb_msc_drv.h
 * @brief USB MSC 底层驱动:TinyUSB MSC 类 + SD 裸扇区绑定 + 互斥/释放。
 *
 * 设计约束(AGENTS.md 第25/26节:崩溃隔离 + 不可信输入):
 *   - 本层只做"把 SD 卡当块设备暴露给 PC",不做 UI;
 *   - 每一个失败路径都返回 errcode,绝不 panic / abort;
 *   - SD 访问:进入前用 board_sd_release() 让 FATFS 让位,退出时 board_sd_acquire()
 *     重新挂载,避免 PC 与手表并发写同一张卡导致文件系统损坏。
 *
 * 注意:当前 XIAO ESP32-S3 的 SD 是 SDSPI(与 LCD 共用 SPI3),不是 SDMMC 卡槽。
 *   因此这里绑定的是 esp_vfs_fat_sdspi_mount() 返回的 sdmmc_card_t,
 *   由其 sdmmc_read_sectors()/sdmmc_write_sectors() 在 SPI 上做 512B 扇区访问,
 *   FATFS 层被绕过(MSC 需要原始块设备语义)。
 */
#ifndef COS_USB_MSC_DRV_H
#define COS_USB_MSC_DRV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE

/**
 * @brief MSC 会话错误码(全屏失败页显示的 errcode,英文 errmsg 见 strerror)。
 */
typedef enum
{
    COS_USB_MSC_OK = 0,
    COS_USB_MSC_ERR_NO_SD = 201,     /* No removable SD card detected          */
    COS_USB_MSC_ERR_JTAG_BUSY = 202, /* USB port occupied by debug console     */
    COS_USB_MSC_ERR_TUSB_INIT = 203, /* USB device stack init failed           */
    COS_USB_MSC_ERR_NOT_ENUM = 204,  /* No USB host enumerated (charging only) */
    COS_USB_MSC_ERR_UNMOUNT = 205,   /* Failed to release SD from FATFS        */
    COS_USB_MSC_ERR_REMOUNT = 206,   /* Failed to re-mount SD after MSC        */
} cos_usb_msc_err_t;

/** @brief errcode → 英文 errmsg(失败页显示用)。 */
const char *cos_usb_msc_strerror(cos_usb_msc_err_t e);

/* ── 探测阶段(不改变系统状态) ───────────────────────────── */

/** @brief /sdcard 是否是真 SD 卡(可做 MSC)。 */
bool cos_usb_msc_drv_sd_available(void);

/** @brief USB 串口调试器是否在线(在线则禁止进入 MSC)。 */
bool cos_usb_msc_drv_jtag_connected(void);

/* ── MSC 会话 ────────────────────────────────────────────── */

/**
 * @brief 进入 MSC:释放 SD → 初始化 TinyUSB device(MSC)。
 * @return COS_USB_MSC_OK 成功;其它为 errcode(失败时内部已回滚)。
 */
cos_usb_msc_err_t cos_usb_msc_drv_begin(void);

/** @brief PC 主机是否已完成枚举并挂载本设备。 */
bool cos_usb_msc_drv_mounted(void);

/**
 * @brief 退出 MSC:卸载 TinyUSB → 重新挂载 SD。
 * @return COS_USB_MSC_OK 或 COS_USB_MSC_ERR_REMOUNT。
 * @note 幂等;未进入时调用安全。
 */
cos_usb_msc_err_t cos_usb_msc_drv_end(void);

#endif /* CONFIG_USB_MSC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* COS_USB_MSC_DRV_H */
