/**
 * @file tusb_config.h
 * @brief TinyUSB 用户端配置(由 espressif/tinyusb 组件要求存在)。
 *
 * 仅启用 USB 设备(Device)侧的 MSC 类;其余类与本机无关,保持关闭以省 Flash。
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

/* ── 设备端总开关 ── */
#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 0   /* 不需要 Host */

/* ── 类使能 ── */
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

/* ── 栈/缓冲参数 ── */
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
