/**
 * @file tusb_config.h
 * @brief TinyUSB 用户端配置(由 espressif/tinyusb 组件要求存在)。
 *
 * 两种角色:
 *   · Device 侧:USB MSC 类(把 SD 卡作为 U 盘暴露给 PC),由 USB MSC App 使用;
 *   · Host   侧:给 Spotify App 使用(枚举 USB-C 耳机并推 PCM)。
 *
 * 注意:Device 与 Host 共用同一个内部 FSLS PHY,由 App 在运行时二选一
 * (USB-Serial-JTAG / OTG-Device / OTG-Host),不会同时启用。
 *
 * 描述符字符串取自 sdkconfig.defaults 的 CONFIG_TINYUSB_DESC_*。
 *
 * 注意:本文件被 TinyUSB 组件以 -include / 全局 include 方式引用,
 * 因此这里 include 的 sdkconfig 路径需可被 main 组件的 include 目录解析
 * (main 组件 INCLUDE_DIRS 含 "." ,sdkconfig 在 port/esp32s3/ 下,
 * 由 IDF 在构建期生成并加入全局 include,见 project.cmake)。
 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── 角色总开关 ── */
#define CFG_TUD_ENABLED 1
/* Host 由 COS_USB_UAC_APP_ENABLE 控制:TinyUSB 0.21 无内置 UAC Host,
 * 本仓 App 自带 audio host 驱动(见 src/apps/spotify/cos_spotify_uac_host.c),
 * 因此这里只需打开 USBH 核心,不需要 CFG_TUH_AUDIO。 */
#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE
#define CFG_TUH_ENABLED 1
#else
#define CFG_TUH_ENABLED 0
#endif

/* ── 设备端类使能 ── */
#define CFG_TUD_MSC 1
#define CFG_TUD_CDC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_AUDIO 0
#define CFG_TUD_DFU 0
/* 注意:TinyUSB 0.21 把 CFG_TUD_NET 改名为 CFG_TUD_ECM_RNDIS。
 * 用新名;不要定义旧名 CFG_TUD_NET,否则 tusb_option.h 报重命名 warning。 */
#define CFG_TUD_ECM_RNDIS 0
#define CFG_TUD_BTH 0
#define CFG_TUD_USBTMC 0
#define CFG_TUD_VIDEO 0

/* 单设备、单逻辑单元(一个 SD 卡)。
 * TinyUSB 0.21 标准名为 CFG_TUD_MSC_EP_BUFSIZE;旧名 CFG_TUD_MSC_BUFSIZE
 * 由 msc_device.h 的兼容层转换。直接用新名,避免歧义。 */
#define CFG_TUD_MSC_EP_BUFSIZE 4096

/* ── 主机端参数(仅 UAC App 使用) ── */
#if CFG_TUH_ENABLED
/* 只服务一个 UAC 耳机 */
#define CFG_TUH_DEVICE_MAX 1
#define CFG_TUH_ENUMERATION_BUFSIZE 256
/* 等时端点不需要 hub */
#define CFG_TUH_HUB 0
#define CFG_TUH_HID 0
#define CFG_TUH_MSC 0
#define CFG_TUH_CDC 0
#define CFG_TUH_VENDOR 0
#define CFG_TUH_MIDI 0
#endif

/* ── 栈/缓冲参数 ── */
/* RHPort0 由 App 在运行时用 tuh_rhport_init()/tud_rhport_init() 指定角色,
 * 这里不固定为 DEVICE/MODE;保留 DEVICE 默认不影响 Host 初始化
 * (tuh_rhport_init 会覆盖 gusbcfg 的 FDMOD/FHMOD 位)。 */
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
/* ESP32-S3 内置 USB-OTG 为 Full Speed,显式声明避免 High Speed 描述符回调需求 */
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    TU_ATTR_ALIGNED(4)
#define CFG_TUSB_DEBUG        0
#define CFG_TUSB_OS           OPT_OS_FREERTOS

/* 描述符字符串(与 sdkconfig.defaults 保持一致) */
#ifndef CONFIG_TINYUSB_DESC_MANUFACTURER_STRING
#define CONFIG_TINYUSB_DESC_MANUFACTURER_STRING "Canto Mk.6"
#endif
#ifndef CONFIG_TINYUSB_DESC_PRODUCT_STRING
#define CONFIG_TINYUSB_DESC_PRODUCT_STRING "Canto SD Card Reader"
#endif
#ifndef CONFIG_TINYUSB_DESC_SERIAL_STRING
#define CONFIG_TINYUSB_DESC_SERIAL_STRING "CANTO-MSC"
#endif

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
