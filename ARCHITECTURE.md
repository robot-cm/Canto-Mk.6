# Canto Mk.6 (CantoMk6) — Coding Agent 架构概览

> 本文档面向需要修改本仓库代码的 coding agent。
> 阅读顺序建议：先读「第 0 章 关键约束」，再按需查阅后面章节。
> 仓库历史命名 `CantoMk6`，新项目名 `Canto Mk.6`；代码中仍以 `COS_*` / `cantomk6` 前缀为主。

---

## 0. 关键约束（动手前必读）

1. **先搜索，再修改。** 不要猜测 GPIO / SPI / I2C / LCD 引脚 / LVGL API / Plugin API。
   硬件与驱动信息以 `port/esp32s3/main/*.c` 与 `src/` 现有符号为准。
2. **Core 与 Script Engine 强耦合。** `cos_core.c`、`apps`、`widgets`、`basic_widgets` 均
   直接 `#include script_engine_core.h → jerryscript.h`。**不能**单独剔除脚本引擎。
3. **可安装的 UI App 是 JavaScript**，经 JerryScript 引擎在 C 侧用 SNI 桥接调用 LVGL；
   资源（`.js` + `manifest.json` + `icon.bin`）打包为 `.eapk`，存于 Flash 内 SPIFFS
   （挂载为 `/sdcard`；默认是内部 SPIFFS，**插入物理 SD 卡时 `board_sd` 会覆盖挂载为真实 SD 卡**，
   无卡时退回 SPIFFS。详见 `agent-experience.md` §17）。
   **但本系统同时包含若干内置原生 C App**（Texthub、Album、Control Center 等），它们直接编译进
   Flash（`COS_NATIVE_APP_*` 枚举，`src/framework/app/cos_app_list.h`），**不是** `.eapk`，
   图标走 `/sdcard/theme/icons/*.bin`。详见 `agent-experience.md` §9（Texthub 改回原生 C）、§12（Album 改回原生 C）。
4. **Shell 属于 Core，不依赖 SD/脚本。** 即使脚本引擎崩溃，底层 Shell 仍可用。
5. **ESP-IDF v5.3.1**，目标 `XIAO ESP32-S3`（ESP32-S3R8：8MB Flash / 8MB PSRAM）。
   普通版，**无** Sense 摄像头/麦克风/板载 SD。

---

## 1. Toolchain / SDK 版本

| 项 | 值 | 来源 |
|---|---|---|
| 构建系统 | ESP-IDF CMake (`idf.py`) | `port/esp32s3/CMakeLists.txt` |
| ESP-IDF | **v5.3.1** | `port/esp32s3/build.esp32-bak/.../project_description.json` (`"git_revision":"v5.3.1-dirty"`) |
| MCU | ESP32-S3R8 (Xtensa LX7, 240MHz, 8MB Flash, 8MB PSRAM) | `partitions.csv` 注释 |
| C 标准库 | newlib (ESP-IDF VFS) | jerry-port 注释 |
| 脚本引擎 | **JerryScript**（内部堆模式，512KB 外部 context） | `main/CMakeLists.txt` 编译宏 |
| UI 库 | **LVGL**（v9.x API：`lv_display_*`） | `cos_dev_display_gc9a01.c` |
| 构建主机 | Linux (Bash)，`idf.py -p /dev/ttyACM0 build flash monitor` | README |
| 打包脚本 | Python 3 (`scripts/cos_pkg_builder.py`) 生成 `.eapk` | BUILD.md |
| 模拟器构建（可选） | CMake + MinGW + SDL2（WASM / Native 分支） | BUILD.md |

### JerryScript 编译宏（来自 `port/esp32s3/main/CMakeLists.txt`）

```
JERRY_VM_HALT=1        # 支持 abort/超时保护
JERRY_LINE_INFO=1      # 报错带行号
JERRY_ERROR_MESSAGES=1
JERRY_LOGGING=1        # 断言失败打印文件/行号（便于定位 fatal OOM）
JERRY_GLOBAL_HEAP_SIZE=512   # 512KB JS 堆
JERRY_SYSTEM_ALLOCATOR=0     # 内部堆模式
JERRY_EXTERNAL_CONTEXT=1     # 512KB 堆走 jerry_port_context_alloc（放入 PSRAM）
JERRY_CPOINTER_32_BIT=1     # 要求 8 字节对齐
```

> 注意：`malloc/free/realloc/calloc` 被 `--wrap` 重定向到 `cos_mem_align.c`
> 提供的 8 字节对齐实现（ESP-IDF 默认仅 4 字节对齐，不满足 JerryScript 要求）。
> **不要移除这些 `--wrap` 链接选项。**

### LVGL 配置（`lv_conf.h` **不参与编译**，`sdkconfig` 才是真正来源）

- `LV_COLOR_16_SWAP` 相关：GC9A01 大端 RGB565，flush 时自行做字节交换（不依赖宏）。
- LVGL 池与 draw buffer 优先放 PSRAM（见第 3 章）。
- **`CONFIG_LV_CONF_SKIP=y`**（`port/esp32s3/sdkconfig`）：`lv_conf.h` 整个被跳过，
  `lv_conf_internal.h` 直接吃 Kconfig 默认值 + `sdkconfig` 覆盖。改 LVGL 配置先
  `grep CONFIG_LV_CONF_SKIP sdkconfig` 确认，再去改 `sdkconfig`/`Kconfig`，**不要改 `lv_conf.h`**
  （改了也不生效，见 `agent-experience.md` §15 / §27 Watchface 崩溃复盘）。
- 对应 `lv_conf.h` 里的开关在 sdkconfig 中是 `CONFIG_LV_*` 形式，例如
  `CONFIG_LV_FONT_FMT_TXT_LARGE_LIMIT`、`CONFIG_LV_USE_BIDI`、`CONFIG_LV_USE_ARABIC_PERSIAN_CHARS`、
  `CONFIG_LV_DRAW_SW_ASM_NONE`、`CONFIG_LV_USE_OWN_POSIX`。

---

## 2. 目录结构

```
CantoMk6-fork-stable/
├── port/esp32s3/                # 板级（真机）专属，非 Core
│   ├── CMakeLists.txt           # 全量编译 src/ + JerryScript + 字体
│   ├── partitions.csv           # 分区表（无 OTA，单 factory）
│   └── main/
│       ├── main.c               # ESP-IDF 入口、外设初始化、cos_init/loop
│       ├── cos_dev_display_gc9a01.c   # GC9A01 SPI LCD + LVGL 注册
│       ├── cos_dev_touch_chsc6x.c     # 电容触摸
│       ├── cos_dev_rtc_bm8563.c       # RTC
│       ├── cos_net_wifi_esp32.c       # Wi-Fi 后端（esp_wifi）
│       ├── cos_bt_esp32.c            # NimBLE 后端
│       ├── cos_mem_align.c           # --wrap malloc 8字节对齐
│       └── lv_conf.h
├── src/                         # Core（平台无关，除 port/ 子目录）
│   ├── kernel/
│   │   ├── core/cos_core.c      # 系统初始化 + 主循环 + activity 根
│   │   ├── memory/              # cos_mem.h/c（分配器抽象、追踪）
│   │   └── ...
│   ├── framework/
│   │   ├── app/                 # App 生命周期（cos_app.h/c）
│   │   └── activity/            # Activity 栈/控制器（cos_activity.h）
│   ├── script_engine/
│   │   ├── core/script_engine_core.h   # 脚本引擎核心（SEC）
│   │   ├── sni/                 # Script Native Interface（JS↔C 桥）
│   │   └── spm/spm.h/c          # Script Program Manager（生命周期闸门）
│   ├── services/                # Wi-Fi / 存储 / 插件 / 网络 / 代理
│   │   └── plugin/              # cos_plugin_manager（扫描 eapk）
│   ├── ui/                      # System UI 组件（基于 LVGL）
│   ├── shell/                   # 底层 USB/UART Shell
│   ├── port/memory/mem_mgr.h/c  # PSRAM 大块分配管理器
│   └── ...
├── apps/                        # 预置 App 源码（.js + manifest.json + icon.bin）
│   ├── calculator/  alarm/  timer/  stopwatch/  calendar/
│   └── globaltime/  texthub/  breach/
├── resources/                   # 编译进 Flash 的字体/图标/图片（C 数组）
│   └── font/  images/  ...
├── third_party/                 # lvgl / jerryscript / cJSON（源码编入）
├── scripts/                     # cos_pkg_builder.py 等打包工具
└── eapk-target/  examples-sdcard/   # 打包产物与示例
```

**构建事实**：`port/esp32s3/main/CMakeLists.txt` 用 `file(GLOB_RECURSE src/**/*.c)`
全量编译 `src/`，并递归编译 `third_party/jerryscript`，仅排除模拟器专属文件
（`cos_sim_hw_mock.c` / `cos_virtual_display.c` / `cos_net_sock_sim.c`）。

---

## 3. 内存管理方式

### 3.1 分配器分层

```
应用/Core
   │
   ├─ cos_malloc / cos_free / cos_realloc / cos_calloc   (cos_mem.h)
   │     └─ `cos_mem_auto.c`：每次分配前置 8 字节头(`COS_MEM_HEADER_MAGIC=0xE5A0`)；
   │        `cos_free` 校验 magic，foreign pointer(非 cos 分配)直接拒绝并泄漏(计数见
   │        `agent-experience.md` §3)；可开追踪(COS_MEM_TRACK_ENABLE)
   │        与 LVGL stdlib 替换(COS_OVERRIDE_LVGL_STDLIB_MALLOC_ENABLE)
   │
   ├─ mem_mgr_alloc / mem_mgr_free   (port/memory/mem_mgr.h)
   │     └─ 大块分配：heap_caps_malloc(MALLOC_CAP_SPIRAM)
   │        PSRAM 耗尽时回退 internal RAM（不失败）
   │
   └─ jerry_port_context_alloc / jerry_port_malloc
         └─ JerryScript 512KB 堆走外部 context，分配在 PSRAM
```

### 3.2 三层内存职责（ESP32-S3R8）

| 区域 | 用途 | 备注 |
|---|---|---|
| **Internal RAM / DRAM** | 实时任务栈、DMA 缓冲、高频结构、Core 关键对象 | UI 任务栈 48KB（internal）；**绝不能让 largest-free-block 跌破 ~50KB**，否则 `ui_task` 创建失败 |
| **PSRAM (8MB)** | LVGL draw buffer、图片/动画缓存、JerryScript 512KB 堆、App 运行时大对象 | S3 SPI2 IDMA 原生支持 PSRAM 地址 |
| **Flash (8MB)** | Core + LVGL + JerryScript + 编译进 Flash 的字体/图标（XIP 映射，不占 PSRAM/RAM） | 见分区表 |
| **SPIFFS (1MB)** | `/sdcard` 挂载点：config / state / eapk 源+解包 / 系统资源。插入物理 SD 卡时 `board_sd` 覆盖挂载为真实 SD 卡 | 逻辑“SD”；无卡时为内部 SPIFFS |

### 3.3 PSRAM 分配陷阱（历史踩坑，务必遵守）

- **LVGL draw buffer**：1/6 屏（40 行）双缓冲，优先 `MALLOC_CAP_SPIRAM`，
  **不要**组合 `MALLOC_CAP_DMA`（`CONFIG_SPIRAM_DMA_CAPABLE` 未开时必失败回退 internal，
  57.6KB 会挤掉 `ui_task` 的 48KB 栈 → UI 全灭）。回退顺序见 `cos_dev_display_gc9a01.c:882`。
- **JerryScript 堆**：必须用 `JERRY_EXTERNAL_CONTEXT=1` + `SYSTEM_ALLOCATOR=0`，
  使 512KB 走一次大分配进 PSRAM。若用 `SYSTEM_ALLOCATOR=1`，小对象会碎片化 DMA RAM
  （largest 30KB→32B，是 SD/FATFS 写失败的根因）。
- `MALLOC_ALWAYSINTERNAL=16KB`：ESP-IDF 把 >16KB 请求路由到 PSRAM，小对象留 internal。

### 3.4 malloc 对齐

`cos_mem_align.c` 用 `--wrap=malloc/free/realloc/calloc` 强制 8 字节对齐，
满足 `JERRY_CPOINTER_32_BIT`（JMEM_ALIGNMENT=8）。**链接选项不可删。**

---

## 4. App 生命周期

### 4.1 形态

- **可安装 App** = **JavaScript 脚本** + `manifest.json` + `icon.bin`。
- 打包为 `.eapk`（由 `scripts/cos_pkg_builder.py` 生成），存于 SPIFFS `/sdcard/apps/...`。
- **内置系统 App**（Texthub / Album / Control Center 等）= 原生 C，编译进 Flash
  (`COS_NATIVE_APP_*`)，不打包为 `.eapk`，入口由 C 直接注册（见 `agent-experience.md` §9 / §12）。
- 运行时经 **JerryScript** 执行；JS 通过 **SNI（Script Native Interface）** 调用 C 侧 LVGL/服务。

### 4.2 三层管理架构

```
cos_app (framework/app)         ← 上层业务语义（启动/停止/挂起）
   │
spm (script_engine/spm)         ← 唯一所有者：脚本程序生命周期闸门
   │   - program_list 双向链表
   │   - spm_call() 校验 state==ACTIVE 才放行 JS 调用
   │   - ACTIVE / SUSPENDED / STOPPING / TERMINATED
   │
script_engine_core (SEC)         ← 实际 jerry_* 执行（IDLE/RUNNING 状态机）
   │
jerryscript (third_party)        ← JS VM + 512KB 堆
```

> 架构约束：上层（`cos_app` / `cos_watchface_js`）**只能**用 `spm_app_*` /
> `spm_watchface_*` 便捷 API，禁止直接调 Core。

### 4.3 状态机（`script_program_state_t`）

```
        spm_start_program()
   TERMINATED ───────────────► ACTIVE
                                 │  spm_suspend_program() (WatchFace only)
                                 ▼
                             SUSPENDED ── spm_resume_program() ──► ACTIVE
                                 │
   ACTIVE/SUSPENDED ── spm_terminate_program() ──► STOPPING ──► TERMINATED
```

- **启动**：`spm_start_program()` → 分配 `script_program_t`、克隆 `script_pkg_t`、
  创建 `sni_context`、链入 `program_list`、委托 SEC parse+execute、置 `ACTIVE`。
- **挂起/恢复**：仅 WatchFace 支持；保存/恢复 `realm`，暂停/恢复 SNI 回调。
- **终止**：`spm_terminate_program()` 置 `STOPPING`（异步等 Core 停）→ `TERMINATED`；
  清理顺序：`sni_ctx` 事件与 sweep → `jerry_value_free(realm)` → `_script_free` → `cos_free(prog)`。
- **引擎致命恢复**：SEC 内 `setjmp/longjmp` 恢复块调用 `spm_handle_engine_reset()`，
  在 `jerry_init()` 清空堆**之前**遍历 program_list 释放所有 JS handle 与 C 资源。

### 4.4 App 与 Core 隔离

- App **不**能直接访问 GPIO/SPI/I2C/LCD/Touch/FreeRTOS 内部/LVGL driver/Wi-Fi driver。
- 所有能力经 SNI 桥接的统一 Plugin/Service API。
- **隔离强度**：当前为同进程 JS 沙箱（非独立任务/MMU 隔离）。单个 App 的 C 侧 bug
  仍可能影响系统——不要声称“App 崩溃绝不影响系统”，除非后续引入真正隔离。

---

## 5. LVGL 如何使用

### 5.1 初始化链

```
main.c (ESP-IDF app_main)
   └─ cos_init()  [src/kernel/core/cos_core.c]
        └─ 外设/显示初始化
             └─ cos_dev_display_gc9a01_lvgl_init()   [port/.../cos_dev_display_gc9a01.c:864]
                  ├─ lv_display_create(240, 240)
                  ├─ lv_display_set_buffers(disp, buf1, buf2,
                  │     240*40 px, LV_DISPLAY_RENDER_MODE_PARTIAL)   // 1/6 屏双缓冲
                  ├─ lv_display_set_flush_cb(disp, display_flush_cb)
                  ├─ lv_display_set_flush_wait_cb(disp, display_flush_wait_cb)
                  └─ lv_display_set_default(disp)
```

### 5.2 显示硬件（GC9A01）

- 1.28" 圆形 240×240，SPI，`SPI_DMA_CH_AUTO`。
- 初始化序列逐字节照搬已验证参数（`cos_dev_display_gc9a01.c:147` `display_init()`）。
- 颜色：RGB565，面板 BGR 线序（`0x36` MADCTL=0x08 补偿），flush 时自行字节交换。
- 异步 flush：`flush_cb` 只把 SPI 事务排队（不调 `lv_display_flush_ready`）；
  完成同步在 LVGL 任务上下文由 `flush_wait_cb` 用 `get_trans_result` 取回在途事务
  ——**避免 ISR 直接调 LVGL timer**。

### 5.3 主循环与 Tick

```c
// cos_core.c:365
uint32_t cos_main_loop(void) {
    ...
    cos_dispatch_tick();     // 事件分发
    return lv_timer_handler(); // LVGL 心跳（必须在 LVGL 任务上下文调用）
}
uint32_t cos_tick_get(void) { return lv_tick_get(); }
```

- `lv_tick` 由 ESP-IDF `esp_timer` 提供（Light Sleep 内保持连续）。
- `cos_main_loop()` 由 `ui_task` 周期性调用；所有 LVGL / JS 回调必须在其任务上下文执行。

### 5.4 UI 组件

- `src/ui/` 提供 System UI 组件（Label / Button / Card / List / Dialog / Slider / Page 等），
  App 应尽量复用，不要各自重造 UI。
- App 的 LVGL 对象由 `spm.c:_lvgl_view_clean()` 在销毁时 `lv_obj_clean(view)` 清理。

---

## 6. 分区与 Flash 预算

`port/esp32s3/partitions.csv`（**方案 A：无 OTA，单 factory**）：

| 分区 | 类型 | Offset | Size | 用途 |
|---|---|---|---|---|
| nvs | data/nvs | 0x9000 | 16KB | 键值配置（`wifi.*` / `proxy.*`） |
| phy_init | data/phy | 0xF000 | 4KB | PHY 校准 |
| factory | app | 0x10000 | **6.94MB** | Core + Service + LVGL + JerryScript + 系统资源 |
| spiffs | data/spiffs | 0x700000 | **1MB** | `/sdcard`：config / state / eapk 源+解包 / 资源 |

- 当前固件约 3.88MB，factory 余量约 2MB（SPIFFS 现为 1MB：512KB→2MB→1MB 演变，详见 partitions.csv 头注释）。
- **已主动删 OTA**：收益是 Core 最大可用空间；代价是固件更新只能 USB 重刷。
  如需恢复 OTA，先评估再改分区表。

---

## 7. 插件 / 脚本加载流程

```
Plugin Manager (src/services/plugin/cos_plugin_manager)
   └─ 扫描 SPIFFS /sdcard/apps/*.eapk
        ├─ 解析 manifest（id/version/permissions/min_api_level）
        ├─ 校验（pkg_id 合法、路径穿越、大小限制；入口恒为 main.js，不存在 entry 字段概念）
        └─ 构建 script_pkg_t
             └─ cos_app_run() → spm_app_run() → spm_start_program()
                  └─ SEC: jerry_parse + jerry_run (512KB 外部堆)
                       └─ 注册 SNI 回调（on_create/on_resume/...）
```

---

## 7.5 USB MSC App（把 SD 卡作为 U 盘暴露给 PC）

一个 **native C App**（`COS_NATIVE_APP_USB_MSC`），编译宏 `CONFIG_USB_MSC_APP_ENABLE`
（在 `port/esp32s3/main/CMakeLists.txt` 里 `COS_USB_MSC_APP_ENABLE` 控制；**不是**
Kconfig）。功能本身是运行时由用户在 App 列表打开/关闭。

### 目录

```
src/apps/usb_msc/
├── cos_usb_msc_board.h   板级能力接口(由 main.c 实现)
├── cos_usb_msc_drv.h/.c  TinyUSB MSC + SD 裸扇区 + 互斥/释放
└── cos_usb_msc.h/.c      App 状态机 + 全屏状态页(图标/errcode/errmsg)
```

### 分层与流向

```
App(状态机, ui_task 内 lv_timer 驱动)
  ↓ cos_usb_msc_drv_*()
驱动层(TinyUSB MSC + sdmmc_read/write_sectors)
  ↓ board_sd_release()/board_sd_get_card()/board_sd_acquire()
板级(port/esp32s3/main/main.c:SDSPI 卡句柄 / FATFS 卸载重挂)
```

### TinyUSB 依赖来源(构建关键)

TinyUSB **不是** ESP-IDF 内置组件,通过 IDF Component Manager 从 Espressif
注册表拉取:

- `port/esp32s3/main/idf_component.yml` 声明
  `espressif/tinyusb: ">=0.15.0,<1.0.0"`(注册表全名带命名空间,
  不能用裸名 `tinyusb`,否则 `MAIN_REQUIRES tinyusb` 报"找不到组件")。
- 首次构建需联网:`idf.py reconfigure` 会下载到 `managed_components/`
  并更新 `dependencies.lock`。CI/离线机需提前缓存该目录。
- 用户端文件(`espressif/tinyusb` 组件要求项目提供):
  - `port/esp32s3/main/tusb_config.h` — `CFG_TUD_*` 类开关(MSC 开启,其余关)
  - `port/esp32s3/main/cos_usb_msc_descriptors.c` — 设备/字符串描述符回调
  两者已加入 main 组件 `SRCS`。`sdkconfig.defaults` 不再写 `CONFIG_TINYUSB_*`
  (托管组件不读这些 Kconfig 项,开关在 tusb_config.h)。

### 状态机

`INIT → (SD? JTAG?) → begin() → WAIT_ENUM → ACTIVE → 退出`
失败任一步 → 全屏 `cos_icon_usb_err` + `Error code: NNN` + 英文 errmsg。

errcode：`201` 无 SD / `202` 调试器占用 USB / `203` TinyUSB 失败 /
`204` 未枚举(纯充电) / `205` 释放 SD 失败 / `206` 重挂载失败。

### 关键设计点（改这个模块前必读）

1. **SD 是 SDSPI**（与 LCD 共用 SPI3，CS=GPIO3），绑定的是
   `esp_vfs_fat_sdspi_mount()` 返回的 `sdmmc_card_t`，用
   `sdmmc_read_sectors()/sdmmc_write_sectors()` 做 512B 裸扇区访问（绕过 FATFS）。
2. **SD 互斥**：进入 MSC 前 `board_sd_release()` 卸载 FATFS/VFS（保留卡句柄与
   SPI 设备）；退出 `board_sd_acquire()` 用 `esp_vfs_fat_register()`+`f_mount()`
   重新挂载（不重新初始化 SPI 设备，避免句柄冲突）。期间其它 App 的 SD 访问
   返回错误而非写坏卡。
3. **monitor 场景（方案 B）**：USB-C 是单口，`USB-OTG`(TinyUSB) 与
   `USB-Serial-JTAG`(console/monitor) 复用同一对 D+/D-(GPIO19/20)。
   App 先查 `usb_serial_jtag_is_connected()`：若调试器在线 → 显示
   `Error code: 202` 并**拒绝**进入 MSC（不抢占 monitor）。仅当无调试器时才
   `tusb_init()` 接管 PHY。
4. **"冻结 UI"**：App 代码运行在 `ui_task` 内，不能 `vTaskSuspend(ui_task)`
   （自我死锁）。等价做法：全屏不透明页 + 关闭所有 POINTER indev
   (`lv_indev_enable(false)`) + swipe_back 消费手势 + `board_pm_usb_msc_hold(true)`
   禁止 Light Sleep。**仅在 ACTIVE(成功)状态禁用触摸**，错误页保留触摸以便退出。
5. **任务/中断**：不新建 FreeRTOS 任务；唯一新增的是 TinyUSB 内部 usb 任务
   （使用 TinyUSB 的固有代价）。不占用额外中断，不与 SDMMC/触摸/LCD 冲突。
6. **崩溃隔离**：所有失败路径返回 errcode → 显示错误页，绝不 panic。
   MSC 回调在 TinyUSB 任务上下文，经 SDMMC 内部锁访问 SD。
7. **PHY 复用已知限制**：进入 MSC 后 USB console/monitor 输出停止；
   退出 App 后建议重插线或重启以恢复 monitor。

### 完全移除该 App

`port/esp32s3/main/CMakeLists.txt` 中把 `set(COS_USB_MSC_APP_ENABLE 1)` 改成 `0`
（并用 `idf.py reconfigure`）。关闭后所有新增/修改点（app_list 枚举、图标资源、
drv、main.c 板级函数、PM 保持、swipe 等）都不编译，产物与改动前一致。

---

## 8. 给 coding agent 的修改检查清单

1. **加/改内存分配**：大对象走 `mem_mgr_*`（PSRAM）；小对象走 `cos_malloc`。
   不要无脑 malloc，也不要无脑 PSRAM——考虑 DMA / 访问速度 / 生命周期。
2. **加硬件驱动**：放 `port/esp32s3/main/`，不要混入 `src/`（Core 应平台无关）。
   引脚/时序以现有 `cos_dev_*` 为准，禁止猜测。
3. **加/改 App**：写 JS + manifest + icon，用 `scripts/cos_pkg_builder.py` 打包 eapk，
   经 Plugin Manager 加载。不要直接改 Core 编入新 App（除非是 Launcher/Settings/Clock 等核心 App）。
4. **加 LVGL 调用**：必须在 `ui_task` 上下文（`cos_main_loop` 调用链内）；
   不要扩大 draw buffer 或改 PSRAM 分配策略而不重新验证 internal largest-free-block。
5. **改构建**：编辑 `port/esp32s3/main/CMakeLists.txt`；
   字体（~2MB+）编进 Flash，留意 factory 余量。
6. **Shell 命令**：加到底层 Shell（`src/shell/`），保证不依赖 SD/脚本即可运行。

---

## 9. 关键文件速查

| 关注点 | 文件 |
|---|---|
| 系统启动/主循环 | `src/kernel/core/cos_core.c` |
| 内存分配器 | `src/kernel/memory/cos_mem.h`, `src/port/memory/mem_mgr.h` |
| App 生命周期 | `src/framework/app/cos_app.c`, `src/script_engine/spm/spm.c` |
| 脚本引擎 | `src/script_engine/core/script_engine_core.h`, `third_party/jerryscript` |
| JS↔C 桥 | `src/script_engine/sni/` |
| LVGL 注册/显示 | `port/esp32s3/main/cos_dev_display_gc9a01.c`, `main/lv_conf.h` |
| 分区 | `port/esp32s3/partitions.csv` |
| 构建 | `port/esp32s3/main/CMakeLists.txt`, `port/esp32s3/CMakeLists.txt` |
| 插件管理 | `src/services/plugin/cos_plugin_manager.h` |
| USB MSC App | `src/apps/usb_msc/cos_usb_msc*.c/.h` |
| USB MSC 板级支持 | `port/esp32s3/main/main.c`（`board_sd_*` / `board_pm_usb_msc_*`） |
| 底层 Shell | `src/shell/` |
| 打包 | `scripts/cos_pkg_builder.py` |
