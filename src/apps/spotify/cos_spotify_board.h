/**
 * @file eos_spotify_board.h
 * @brief Spotify App 所使用的"板级能力"接口(由 port/esp32s3/main/main.c 实现)。
 *
 * 架构说明(AGENTS.md 第 3 节:Core 稳定 / 硬件放 port):
 *   - src/ 下的 App 不允许直接触碰 SPI/SDMMC/GPIO/USB PHY 细节;
 *   - Spotify 需要的能力与 USB MSC 高度重合,故直接复用
 *     eos_usb_msc_board.h 已声明的:board_sd_is_real / board_sd_get_card /
 *     board_sd_release / board_sd_acquire / board_usb_serial_jtag_connected;
 *   - 本头文件只额外声明 Spotify 特有、且 USB MSC 未提供的能力。
 *
 * 与现有代码的关系:全部为"新增"接口,不修改任何现有 App / 电源管理逻辑。
 */
#ifndef EOS_SPOTIFY_BOARD_H
#define EOS_SPOTIFY_BOARD_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/**
 * @brief USB-Serial-JTAG(调试控制台)当前是否正在驱动 USB PHY。
 * @return true 说明 idf.py monitor / 串口在线,PHY 归 USJ;
 *         false 说明 PHY 空闲,USB-OTG 可直接使用。
 * @note 与 board_usb_serial_jtag_connected() 语义相同,此处提供独立命名,
 *       便于 Spotify 内部阅读;实现复用同一探测函数。
 */
bool board_spotify_usj_online(void);

/**
 * @brief 通知板级"UAC 音频会话进行中":期间禁止 Light Sleep 等扰乱 USB 的行为。
 * @param hold true 进入保持,false 解除。
 * @note 与 board_pm_usb_msc_hold 共用同一套电源保持状态机,语义一致。
 */
void board_spotify_pm_hold(bool hold);

/**
 * @brief 查询 UAC 会话保持标志(供板级 pm 状态机判断)。
 */
bool board_spotify_pm_is_held(void);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_BOARD_H */
