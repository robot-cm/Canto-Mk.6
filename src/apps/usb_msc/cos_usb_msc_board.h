/**
 * @file cos_usb_msc_board.h
 * @brief USB MSC App 所使用的"板级能力"接口(由 port/esp32s3/main/main.c 实现)
 *
 * 架构说明(AGENTS.md 第3节:Core 稳定 / 硬件放 port):
 *   - src/ 下的 App 不允许直接触碰 SPI/SDMMC/GPIO 细节;
 *   - 这里只暴露 5 个最小能力:SD 是否真卡、卡句柄、SD 释放/恢复、
 *     USB 串口调试器是否在线、电源保持;
 *   - 具体实现全部在板级 main.c 内,并用 CONFIG_USB_MSC_APP_ENABLE 宏包裹。
 *
 * 与现有代码的关系:全部为"新增"接口,不修改任何现有 App / 电源管理逻辑。
 */
#ifndef COS_USB_MSC_BOARD_H
#define COS_USB_MSC_BOARD_H

#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE

/**
 * @brief /sdcard 是否挂载在真实可移动 SD 卡上(而非 SPIFFS 回退)。
 * @return true 真 SD(可做 MSC);false SPIFFS 回退或无卡。
 */
bool board_sd_is_real(void);

/**
 * @brief 获取当前已挂载 SD 卡的 sdmmc_card_t(裸扇区读写用)。
 * @return 卡句柄;未挂载真 SD 时返回 NULL。
 */
sdmmc_card_t *board_sd_get_card(void);

/**
 * @brief 释放 SD 给 MSC(TinyUSB)独占:卸载 FATFS/VFS,保留 sdmmc_card_t。
 * @return ESP_OK 成功。
 * @note 卸载后其它 App 的 POSIX 文件访问会失败,但不会破坏数据。
 */
esp_err_t board_sd_release(void);

/**
 * @brief MSC 结束:重新挂载 FATFS/VFS,恢复其它 App 的 SD 访问。
 * @return ESP_OK 成功。
 * @note 若重挂载失败,App 会显示 errcode,不会 panic。
 */
esp_err_t board_sd_acquire(void);

/**
 * @brief USB 串口调试器(CDC/JTAG console)当前是否被主机连接。
 * @return true 说明 idf.py monitor 正占用 USB 口 → 不允许进入 MSC。
 */
bool board_usb_serial_jtag_connected(void);

/**
 * @brief 通知板级"MSC 会话进行中":期间禁止 Light Sleep 等会扰乱 USB 的行为。
 */
void board_pm_usb_msc_hold(bool hold);

/** @brief 查询 MSC 会话保持标志(供板级自己的 pm 状态机判断)。 */
bool board_pm_usb_msc_is_held(void);

#endif /* CONFIG_USB_MSC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* COS_USB_MSC_BOARD_H */
