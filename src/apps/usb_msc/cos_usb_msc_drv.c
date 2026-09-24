/**
 * @file cos_usb_msc_drv.c
 * @brief USB MSC 底层驱动实现(TinyUSB MSC + SD 裸扇区)。
 *
 * 交付批次:①底层驱动层。
 *
 * 线程模型 / 任务与中断:
 *   - 本文件不创建任何 FreeRTOS 任务;TinyUSB 协议栈内部自身的 usb 任务
 *     由 tusb_init() 创建,这是使用 TinyUSB 的固有代价(唯一新增任务)。
 *   - MSC 回调 tud_msc_read10_cb / write10_cb 在 TinyUSB 任务上下文执行,
 *     经 sdmmc_read_sectors()/sdmmc_write_sectors() 访问 SD(带 SDMMC 内部锁),
 *     不与 LCD SPI 中断、触摸中断冲突。
 */
#include "cos_usb_msc_drv.h"

#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "sdmmc_cmd.h"

#include "cos_usb_msc_board.h"

/* ── 方案 B:USB 口动态接管(USJ 控制台 ⇄ USB-OTG/MSC) ─────────────
 * ESP32-S3 上 USB-Serial-JTAG(调试控制台)与 USB-OTG(TinyUSB)复用同一个
 * 内部 FSLS PHY,由 RTCCNTL.usb_conf.sw_usb_phy_sel 二选一:
 *   0 → 内部 PHY 归 USJ(console 可用,PC 看到调试串口)
 *   1 → 内部 PHY 归 USB Wrap(OTG 可用,PC 看到本 MSC 设备)
 *
 * espressif__tinyusb 的 DWC2 port 对 PHY 只做上下拉设置,PHY 本体
 * "managed by ESP-IDF"(见该组件 dwc2_esp32.h),因此必须由应用侧经
 * usb_phy 管理器申请,才能把 PHY 从 USJ 抢到 OTG。 */
#include "esp_private/usb_phy.h"

/* 会话结束还原:usb_del_phy() 只解除 pull/pad 覆盖,不会自动把
 * sw_usb_phy_sel 拨回 USJ,故直接调用 IDF 的 LL(等价于
 * usb_serial_jtag_hal_phy_set_external(hal,false))把 PHY 还给 console。 */
#include "hal/usb_serial_jtag_ll.h"

/* FreeRTOS:自建 TinyUSB 事件泵任务(xTaskCreate / tud_task 循环) */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* TinyUSB(MSC device 类) */
#include "tusb.h"
#include "class/msc/msc_device.h"

#define TAG "UsbMscDrv"

/* SD 扇区固定 512B(FATFS/MSC 一致) */
#define COS_USB_MSC_SECTOR_SIZE 512

static sdmmc_card_t *s_card = NULL;          /* 当前绑定的 SD 卡句柄 */
static uint32_t      s_block_count = 0;      /* 暴露给 PC 的 512B 扇区总数 */
static bool          s_active = false;        /* MSC 会话是否进行中 */
static bool          s_sd_released = false;   /* FATFS 是否已被释放 */
static uint8_t      *s_bounce = NULL;         /* DMA 安全的中转缓冲(1 扇区) */
static usb_phy_handle_t s_phy_hdl = NULL;     /* USB-OTG 内部 PHY 句柄(接管自 USJ 控制台) */

/* ── TinyUSB 设备栈事件泵(必须自建) ───────────────────────────────
 * tusb_init()(0.21 起是宏 → tud_rhport_init)只做 dcd_init()+使能中断,
 * 真正的 SETUP/控制传输/枚举处理全部发生在 tud_task() 中,而
 * espressif__tinyusb 组件本身不会创建任何任务(只有 esp_tinyusb 封装层才会)。
 * 缺了本任务:设备 D+ 会正常上拉(PC 能看到"有设备接入"),但没人应答主机
 * 的枚举请求 → Ubuntu 识别不到 → tud_mounted() 始终 false → 4s 后 204。
 * 约束(见 device/usbd.h):tud_rhport_init()/tud_deinit() 必须与 tud_task()
 * 处于同一任务上下文,因此 tusb_init()/tud_deinit() 都放在本任务内执行。 */
#define COS_USB_MSC_TUSB_TASK_STACK   6144   /* 内含 dcd_init/esp_intr_alloc 与 MSC 回调 */
#define COS_USB_MSC_TUSB_TASK_PRIO    5
#define COS_USB_MSC_TUSB_IDLE_MS      20     /* 空闲时最多阻塞 20ms,之后回查退出标志 */

static TaskHandle_t  s_tusb_task = NULL;      /* 事件泵任务句柄(0/NULL = 未运行) */
static volatile bool s_tusb_run  = false;     /* 事件泵循环退出标志 */
static volatile int  s_tusb_init = 0;         /* 任务内 tusb_init() 结果:0=进行中 1=成功 -1=失败 */

static void _tusb_task(void *arg)
{
    (void)arg;

    /* 必须与 tud_task() 同任务上下文(usbd.h 显式要求) */
    if (!tusb_init()) {
        s_tusb_init = -1;
        s_tusb_run  = false;
        s_tusb_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    s_tusb_init = 1;

    while (s_tusb_run) {
        /* 有事件立即处理;空闲时最多阻塞 20ms,以便 end() 能及时让本任务退出 */
        tud_task_ext(COS_USB_MSC_TUSB_IDLE_MS, false);
    }

    /* 收尾同样在本任务内(与 init/tud_task 同上下文) */
    tud_disconnect();
    tud_deinit(0);
    s_tusb_task = NULL;
    vTaskDelete(NULL);
}

/* ── 探测 ─────────────────────────────────────────────────── */

bool cos_usb_msc_drv_sd_available(void)
{
    return board_sd_is_real() && (board_sd_get_card() != NULL);
}

bool cos_usb_msc_drv_jtag_connected(void)
{
    return board_usb_serial_jtag_connected();
}

const char *cos_usb_msc_strerror(cos_usb_msc_err_t e)
{
    switch (e) {
        case COS_USB_MSC_OK:
            return "OK";
        case COS_USB_MSC_ERR_NO_SD:
            return "No SD card detected.";
        case COS_USB_MSC_ERR_JTAG_BUSY:
            return "USB port busy: debug console in use.";
        case COS_USB_MSC_ERR_TUSB_INIT:
            return "Failed to start USB stack.";
        case COS_USB_MSC_ERR_NOT_ENUM:
            return "No PC detected. Check the USB cable.";
        case COS_USB_MSC_ERR_UNMOUNT:
            return "Failed to release the SD card.";
        case COS_USB_MSC_ERR_REMOUNT:
            return "Failed to re-mount the SD card.";
        default:
            return "Unknown error.";
    }
}

/* ── SD 扇区读写(供 TinyUSB MSC 回调调用) ─────────────────── */

/** 目标缓冲是否可直接做 DMA(地址属于 DMA 能力内存且 4 字节对齐)。 */
static bool _dma_ok(const void *p)
{
    return (p != NULL) && esp_ptr_dma_capable(p) && (((uintptr_t)p & 0x3u) == 0u);
}

/**
 * @brief 通用扇区传输(支持任意 offset/bufsize,自动 bounce)。
 * @param write true=写卡,false=读卡
 * @return 成功返回实际传输字节数(>0),失败返回 -1。
 */
static int32_t _xfer(bool write, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (!s_active || s_card == NULL || buffer == NULL) {
        return -1;
    }
    if (bufsize == 0u) {
        return 0;
    }
    if (s_bounce == NULL) {
        return -1;
    }

    uint8_t *p = (uint8_t *)buffer;
    uint32_t done = 0u;

    while (done < bufsize) {
        uint32_t byte_abs = offset + done;
        uint32_t cur_lba = lba + (byte_abs / COS_USB_MSC_SECTOR_SIZE);
        uint32_t cur_off = byte_abs % COS_USB_MSC_SECTOR_SIZE;
        uint32_t chunk = COS_USB_MSC_SECTOR_SIZE - cur_off;
        if (chunk > (bufsize - done)) {
            chunk = bufsize - done;
        }

        /* 快路径:整扇区 + 目标可直接 DMA → 零拷贝 */
        if (cur_off == 0u && chunk == COS_USB_MSC_SECTOR_SIZE && _dma_ok(p + done)) {
            esp_err_t e = write ? sdmmc_write_sectors(s_card, p + done, cur_lba, 1)
                                : sdmmc_read_sectors(s_card, p + done, cur_lba, 1);
            if (e != ESP_OK) {
                ESP_LOGE(TAG, "%s lba=%u failed: %s",
                         write ? "write" : "read", (unsigned)cur_lba, esp_err_to_name(e));
                return -1;
            }
            done += chunk;
            continue;
        }

        /* 慢路径:经 bounce 缓冲完成(读改写) */
        if (write) {
            esp_err_t e = sdmmc_read_sectors(s_card, s_bounce, cur_lba, 1);
            if (e != ESP_OK) {
                ESP_LOGE(TAG, "rmw read lba=%u failed: %s", (unsigned)cur_lba, esp_err_to_name(e));
                return -1;
            }
            memcpy(s_bounce + cur_off, p + done, chunk);
            e = sdmmc_write_sectors(s_card, s_bounce, cur_lba, 1);
            if (e != ESP_OK) {
                ESP_LOGE(TAG, "rmw write lba=%u failed: %s", (unsigned)cur_lba, esp_err_to_name(e));
                return -1;
            }
        } else {
            esp_err_t e = sdmmc_read_sectors(s_card, s_bounce, cur_lba, 1);
            if (e != ESP_OK) {
                ESP_LOGE(TAG, "bounce read lba=%u failed: %s", (unsigned)cur_lba, esp_err_to_name(e));
                return -1;
            }
            memcpy(p + done, s_bounce + cur_off, chunk);
        }
        done += chunk;
    }

    return (int32_t)bufsize;
}

/* ── TinyUSB MSC 回调 ─────────────────────────────────────── */

/* PC 读 SD(TinyUSB → 本机) */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;
    return _xfer(false, lba, offset, buffer, bufsize);
}

/* PC 写 SD(本机 ← TinyUSB) */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    return _xfer(true, lba, offset, buffer, bufsize);
}

/* 容量上报:扇区数 + 扇区大小 */
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    if (block_count) {
        *block_count = s_block_count;
    }
    if (block_size) {
        *block_size = COS_USB_MSC_SECTOR_SIZE;
    }
}

/* PC 询问设备厂商/型号(全英文,固定长度,空格补齐) */
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    memcpy(vendor_id, "CantoMk6", 8);
    memcpy(product_id, "Canto SD MSC   ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

/* 单元就绪:MSC 未激活时返回 false,避免 PC 误判 */
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return s_active;
}

/* 可写 */
bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return s_active;
}

/* START_STOP:接受 PC 的启停请求 */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

/* 处理 TinyUSB 内置列表之外的 SCSI 命令。
 * READ10/WRITE10 由 read10/write10 回调处理;
 * TEST_UNIT_READY/READ_CAPACITY/INQUIRY/START_STOP/MODE_SENSE/REQUEST_SENSE
 * 由 TinyUSB 内置处理;均不会走到这里。
 * 注意:本回调是 espressif__tinyusb 中【无 weak 默认实现】的必需回调之一
 *       (另四个:read10/write10/capacity/test_unit_ready),缺失会导致
 *       链接期 "undefined reference to tud_msc_scsi_cb"。
 * 返回语义:TUD_MSC_RET_BUSY=0 / TUD_MSC_RET_ERROR=-1。 */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;

    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        case SCSI_CMD_READ_10:
        case SCSI_CMD_WRITE_10:
            /* 已由专门回调处理,无需额外数据阶段 */
            return 0;

        default:
            /* 未知/未支持命令:置 sense=Illegal Request,返回 -1 → STALL */
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}

/* ── 会话开始 / 结束 ──────────────────────────────────────── */

cos_usb_msc_err_t cos_usb_msc_drv_begin(void)
{
    if (s_active) {
        return COS_USB_MSC_OK;
    }

    if (!board_sd_is_real()) {
        return COS_USB_MSC_ERR_NO_SD;
    }

    s_card = board_sd_get_card();
    if (s_card == NULL) {
        return COS_USB_MSC_ERR_NO_SD;
    }

    /* 1) 容量:在 board_sd_release() 之前读,不依赖"释放后 card 仍有效"这一前提。
     *    csd.capacity 对 SDHC/XC 即为 512B 扇区数(现代 µSD 均为 SDHC/XC) */
    s_block_count = (uint32_t)s_card->csd.capacity;
    if (s_block_count == 0u) {
        ESP_LOGE(TAG, "invalid card capacity");
        s_card = NULL;
        return COS_USB_MSC_ERR_NO_SD;
    }

    /* 2) 准备 DMA 安全的中转缓冲(内部 RAM,1 扇区)。
     *    同样放在 release 之前:失败时无需回滚已卸载的 FATFS */
    if (s_bounce == NULL) {
        s_bounce = (uint8_t *)heap_caps_malloc(COS_USB_MSC_SECTOR_SIZE,
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_bounce == NULL) {
            ESP_LOGE(TAG, "bounce buffer alloc failed");
            s_card = NULL;
            return COS_USB_MSC_ERR_TUSB_INIT;
        }
    }

    /* 3) 让 FATFS 让位:PC 与手表不能并发访问同一张卡。
     *    从这里开始是不可逆操作,后续任何失败都要 board_sd_acquire() 回滚 */
    esp_err_t err = board_sd_release();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board_sd_release failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return COS_USB_MSC_ERR_UNMOUNT;
    }
    s_sd_released = true;

    /* 4) 置位后再启动栈,保证枚举瞬间回调即可服务 */
    s_active = true;

    /* 5a) 申请【内部 USB PHY】并路由给 USB-OTG —— 即把 USB 口从
     *     USB-Serial-JTAG 控制台"抢"过来(方案 B)。成功后 console(monitor)
     *     静默到 end() 为止,但 USB 设备身份改由本 MSC 接管,PC 可正常识别为 U 盘。
     *     必须在 tusb_init() 之前调用(espressif__tinyusb 自身不配置 PHY)。
     *     注:若此处失败(如 PHY 被占用),回滚到"未启动"状态,由 App 显示错误页。 */
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target     = USB_PHY_TARGET_INT,
        .otg_mode   = USB_OTG_MODE_DEVICE,
        .otg_speed  = USB_PHY_SPEED_UNDEFINED,
    };
    esp_err_t perr = usb_new_phy(&phy_conf, &s_phy_hdl);
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "usb_new_phy failed: %s", esp_err_to_name(perr));
        s_active = false;
        board_sd_acquire();
        s_sd_released = false;
        s_card = NULL;
        return COS_USB_MSC_ERR_TUSB_INIT;
    }

    /* 5b) 起 TinyUSB 设备栈任务(tusb_init + tud_task 必须同任务上下文)。
     *     枚举完全由该任务内的 tud_task_ext() 驱动,缺它则永远 204。 */
    s_tusb_init = 0;
    s_tusb_run  = true;
    if (xTaskCreate(_tusb_task, "cos_tusb", COS_USB_MSC_TUSB_TASK_STACK, NULL,
                    COS_USB_MSC_TUSB_TASK_PRIO, &s_tusb_task) != pdPASS) {
        ESP_LOGE(TAG, "create tusb task failed");
        s_tusb_run = false;
        usb_del_phy(s_phy_hdl);
        s_phy_hdl = NULL;
        s_active = false;
        board_sd_acquire();
        s_sd_released = false;
        s_card = NULL;
        return COS_USB_MSC_ERR_TUSB_INIT;
    }

    /* 等任务内的 tusb_init() 出结果(正常 <10ms),保留原有的错误上报语义 */
    for (int i = 0; i < 40 && s_tusb_init == 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (s_tusb_init != 1) {
        ESP_LOGE(TAG, "tusb_init failed");
        s_tusb_run = false;
        for (int i = 0; i < 20 && s_tusb_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_tusb_task != NULL) {
            vTaskDelete(s_tusb_task);
            s_tusb_task = NULL;
        }
        usb_del_phy(s_phy_hdl);
        s_phy_hdl = NULL;
        s_active = false;
        board_sd_acquire();
        s_sd_released = false;
        s_card = NULL;
        return COS_USB_MSC_ERR_TUSB_INIT;
    }

    ESP_LOGI(TAG, "MSC started, %u sectors (%u MB)",
             (unsigned)s_block_count, (unsigned)((uint64_t)s_block_count * 512u / (1024u * 1024u)));
    return COS_USB_MSC_OK;
}

bool cos_usb_msc_drv_mounted(void)
{
    return s_active && tud_mounted();
}

cos_usb_msc_err_t cos_usb_msc_drv_end(void)
{
    cos_usb_msc_err_t ret = COS_USB_MSC_OK;

    if (s_active) {
        /* 先断开事件通路,再让事件泵任务自行收尾退出。
         * 收尾(tud_disconnect + tud_deinit)在该任务内完成:
         * usbd.h 要求 tud_deinit 必须与 tud_task 同任务上下文。
         * 注意:s_active=false 后 MSC 回调立即 STALL,避免退出瞬间还在读 SD。 */
        s_active = false;
        s_tusb_run = false;
        for (int i = 0; i < 100 && s_tusb_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_tusb_task != NULL) {
            /* 兜底:回调卡在 SD 上导致任务未能自行退出时强制回收 */
            ESP_LOGW(TAG, "tusb task busy, force delete");
            vTaskDelete(s_tusb_task);
            s_tusb_task = NULL;
        }

        /* 释放 USB-OTG 内部 PHY;并把复用开关拨回 USB-Serial-JTAG,
         * 否则退出 App 后 console(monitor)会一直静默、无法恢复日志。
         * usb_del_phy() 只解除 pull/pad 覆盖,不会自动还原 sw_usb_phy_sel。 */
        if (s_phy_hdl != NULL) {
            usb_del_phy(s_phy_hdl);
            s_phy_hdl = NULL;
        }
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        usb_serial_jtag_ll_phy_enable_external(false);
        usb_serial_jtag_ll_phy_enable_pad(true);
#endif
    }

    if (s_sd_released) {
        esp_err_t err = board_sd_acquire();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "board_sd_acquire failed: %s", esp_err_to_name(err));
            ret = COS_USB_MSC_ERR_REMOUNT;
        }
        s_sd_released = false;
    }

    s_card = NULL;
    s_block_count = 0u;
    ESP_LOGI(TAG, "MSC stopped (ret=%d)", (int)ret);
    return ret;
}

#endif /* CONFIG_USB_MSC_APP_ENABLE */
