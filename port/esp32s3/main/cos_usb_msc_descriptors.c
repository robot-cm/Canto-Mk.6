/**
 * @file cos_usb_msc_descriptors.c
 * @brief USB 设备描述符 / 配置描述符 / 字符串描述符(espressif/tinyusb 组件要求用户提供)。
 *
 * 设备只暴露一个 MSC 接口(单一逻辑单元 = 一张 SD 卡),无 CDC/HID。
 * 不依赖 Espressif BSP(board_api.h),串口号用固定字符串。
 * 描述符字符串内容取自 tusb_config.h 的 CONFIG_TINYUSB_DESC_*。
 */
#include "tusb.h"

/* ── 端点编号(ESP32-S3 USB-OTG/DWC2,无单端点方向限制) ── */
enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

#define EPNUM_MSC_OUT   0x03
#define EPNUM_MSC_IN    0x83

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

/* ── 设备描述符 ── */
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MSC,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCAFE,   /* 占位 VID,正式发布应申请;与 cdc_msc 示例同风格 */
    .idProduct          = 0x0001,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* ── 配置描述符(全速) ── */
static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 4, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

/* ── 字符串描述符 ── */
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MSC_INTERFACE,
};

static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },                       /* 0: English (0x0409) */
    CONFIG_TINYUSB_DESC_MANUFACTURER_STRING,             /* 1: Manufacturer */
    CONFIG_TINYUSB_DESC_PRODUCT_STRING,                  /* 2: Product */
    CONFIG_TINYUSB_DESC_SERIAL_STRING,                   /* 3: Serial */
    "Canto SD Card Reader MSC",                          /* 4: MSC Interface */
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
        case STRID_LANGID:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;
        default:
            if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
                return NULL;
            }
            const char *str = string_desc_arr[index];
            chr_count = strlen(str);
            size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_count) {
                chr_count = max_count;
            }
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
