# Spotify App（USB UAC 耳机音乐播放器）— 改动影响面分析报告

> 交付批次：Batch 1（分析与骨架）前置报告
> 目标硬件：XIAO ESP32-S3（8MB Flash / 8MB PSRAM）+ 1.28" 240×240 圆屏
> 构建系统：ESP-IDF v5.x + TinyUSB（`espressif__tinyusb` 托管组件）
> 结论：**本 App 完全自包含，不触碰 Alarm / Album / Texthub / 电源管理 / 启动流程。**

---

## 0. 关键前置发现（必须先说清楚）

| 项目 | 现状 | 影响 |
|---|---|---|
| `resources/images/icon/eos_icon_spotify.c` | **不存在** | 需新增（Batch 1 占位，Batch 3 正式图） |
| `resources/images/icon/eos_icon_earphone_err.c` | **不存在** | 需新增（Batch 1 必需，失败页依赖） |
| TinyUSB Host UAC 类 | **未启用** | `tusb_config.h` 当前几乎是 Device-only，需新增 `CFG_TUH_ENABLED` 与 `CFG_TUH_AUDIO` |
| `CFG_TUH_*` 宏 | **未定义** | 需在 `tusb_config.h` 内新增 |
| MP3 解码器 | **仓库内无**（无 minimp3 / helix） | 需自带源码（Batch 3） |
| `eos_app_list.c` 原生 App 注册表 | 已有稳定机制 | 直接复用，只加 1 个枚举 + 3 处数组元素 |
| USB-Serial-JTAG 与 USB-OTG PHY 复用 | 已有成熟方案（USB MSC 已实现） | 直接复用同一套 PHY 抢占 / 归还逻辑 |
| SD 释放 / 重挂载 | 已有 `board_sd_release()` / `board_sd_acquire()` | 直接复用，无需新写 |

> **重要修正**：任务书中要求的 `CONFIG_USB_MSC_APP_ENABLE` 已存在且当前为 **开启**。
> 本 App 复用该宏作为"可回退性"开关是合理的，但**不建议复用**——因为关闭它会让 USB MSC App 一起消失。
> **建议新增独立宏 `EOS_USB_UAC_APP_ENABLE`**（默认 1），理由见 §6。

---

## 1. 涉及 / 新增 / 修改文件清单

### 1.1 新增文件（App 主体，全部自包含）

| 文件 | 作用 | 批次 |
|---|---|---|
| `src/apps/spotify/eos_spotify.h` | App 对外入口 `eos_spotify_enter()` | B1 |
| `src/apps/spotify/eos_spotify.c` | App 主体：状态机 / UI / 手势 / 退出恢复 | B1–B4 |
| `src/apps/spotify/eos_spotify_board.h` | 板级能力声明（USB 占用探测 / 电源保持） | B1 |
| `src/apps/spotify/eos_spotify_uac.h` | UAC 层错误码 + 对外 API | B2 |
| `src/apps/spotify/eos_spotify_uac.c` | TinyUSB **Host** UAC 初始化 / 枚举 / 等时端点 / 去初始化 | B2 |
| `src/apps/spotify/eos_spotify_audio.h` | 解码器抽象（MP3/WAV → PCM） | B3 |
| `src/apps/spotify/eos_spotify_audio.c` | WAV 解析 + MP3 接入，PCM 环形缓冲 | B3 |
| `src/apps/spotify/eos_spotify_mp3.c` / `.h` | 内嵌轻量 MP3 解码器（minimp3 单文件） | B3 |
| `src/apps/spotify/eos_spotify_lrc.c` / `.h` | .lrc 解析 + 时间戳检索 | B3 |
| `src/apps/spotify/eos_spotify_tree.c` / `.h` | SD 懒加载文件树（参考 Texthub FM） | B3 |
| `src/apps/spotify/eos_spotify_arc.h` / `.c` | 音量弧 / 速度弧控件（120°，长按 1s/3s） | B4 |
| `resources/images/icon/eos_icon_spotify.c` | App 图标（36×36 ARGB8888） | B1 占位 |
| `resources/images/icon/eos_icon_earphone_err.c` | 失败页图标（94×94 ARGB8888） | B1 |

### 1.2 修改文件（仅 3 个，改动点全部用宏包裹）

| 文件 | 改动类型 | 具体改动 | 风险 |
|---|---|---|---|
| `src/framework/app/eos_app_list.h` | **修改** | `EOS_NATIVE_APP_*` 枚举新增 `EOS_NATIVE_APP_SPOTIFY` | 极低（仅枚举追加） |
| `src/framework/app/eos_app_list.c` | **修改** | 3 处数组追加 1 个元素 + `#include` + `extern` 图标 | 低（纯追加，不进刷新循环） |
| `port/esp32s3/main/CMakeLists.txt` | **修改** | 新增 `EOS_USB_UAC_APP_ENABLE` 分支，加图标源 + `-u` 链接选项 | 低（新分支，不改原分支） |
| `port/esp32s3/main/tusb_config.h` | **修改** | 追加 `CFG_TUH_ENABLED` / `CFG_TUH_AUDIO` 等 Host 宏 | **中**（见 §7 风险） |
| `port/esp32s3/main/main.c` | **修改** | 追加 `board_usb_otg_in_use_by_debug()` 等 2 个板级查询函数 | 低（纯新增函数） |

### 1.3 仅引用（不改）

- `src/services/storage/eos_service_storage.h`（`eos_storage_dir_open/read`、`eos_storage_file_open_read`）
- `src/ui/system/eos_round_clip.h`（圆屏裁剪）
- `src/ui/font/eos_font.h`（`eos_label_set_font_size`，中文歌词走 jbm_13→han_sans_13 链）
- `src/ui/widgets/image/eos_image_resuorces.h`
- `src/framework/activity/eos_activity.h`（生命周期 / swipe_back）
- `src/ui/symbol/eos_icon.h`（RemixIcon：播放 / 暂停 / 上下曲）

### 1.4 未触及（确认）

| 模块 | 是否改动 |
|---|---|
| Alarm (`services/alarm`) | ❌ 否 |
| Album / Gallery / Files / Dictionary / FlashLight / Settings | ❌ 否 |
| Texthub | ❌ 否 |
| 电源管理（Light Sleep / Deep Sleep / wake） | ❌ 否 |
| 启动流程（`eos_core.c` boot 顺序） | ❌ 否 |
| USB MSC App 业务逻辑 | ❌ 否（仅共享 PHY 归还逻辑，各自独立实现） |
| LVGL 配置 / 字体链 | ❌ 否（复用现有字号档） |

---

## 2. 解答任务书 §8 的 6 个问题

### Q1. 改动影响面分析报告

→ **本文件全文**。核心结论：新增 15 个文件，修改 5 个文件共 7 个改动点，全部宏包裹，关闭宏时产物与改动前一致。

### Q2. `idf.py monitor` 时该 App 的实际行为？

**结论：`idf.py monitor` 占用 USB PHY 时，该 App 自动抢占 PHY，不会失败退出（推荐方案 B，与 USB MSC 保持一致）。**

- ESP32-S3 只有一个 USB 控制器 + 一个内部 PHY，由 `RTCCNTL.usb_conf.sw_usb_phy_sel` 二选一。
- `board_usb_serial_jtag_connected()`（→ `usb_serial_jtag_is_connected()`）返回 `true` 说明 PC 正在用 JTAG 串口。
- **USB MSC 已验证过的做法**：不拒绝，而是主动 `usb_new_phy(USB_PHY_CTRL_OTG, USB_OTG_MODE_HOST)` 把 PHY 从 USJ 抢到 OTG，退出时 `usb_serial_jtag_ll_phy_enable_external(false)` 把 PHY 还给 console。
- 因此本 App 采用 **方案 B（主动抢占）**，同时**保留**任务书要求的 `errcode 0x02` 错误页：仅当 `usb_new_phy()` **真的失败**（PHY 被其他控制器硬占用）时显示：

```text
errcode: 0x02
errmsg : USB port in use by debug console (USB-Serial-JTAG).
         Disconnect idf.py monitor to use USB audio.
```

- **monitor 期间日志会静默**（PHY 已交给 OTG）。退出 App 后自动恢复。这与 USB MSC 行为完全一致。
- 充电场景（无数据线/仅供电）走"未枚举到 UAC 设备"错误码，不误判为"耳机已连接"。

### Q3. XIAO ESP32-S3 上 USB-C 口的物理连接，UAC 用哪个控制器？

| 项目 | 值 |
|---|---|
| 物理接口 | **唯一一个 USB-C**，D- = **GPIO19**，D+ = **GPIO20** |
| 内部控制器 | ① **USB-Serial-JTAG**（烧录 / monitor / console）② **USB-OTG** |
| PHY | 二者**共享同一个 FSLS PHY**，寄存器二选一 |
| UAC 应使用 | **USB-OTG（TinyUSB Host 模式）**，`USB_PHY_CTRL_OTG` + `USB_OTG_MODE_HOST` |
| 同一时刻 | **仅一个控制器可驱动 PHY** |

> 驱动 UAC 耳机需 **Host 角色**（ESP32 做主机，耳机做设备）。
> 注意：USB MSC 用的是 `USB_OTG_MODE_DEVICE`（ESP32 做设备），本 App 需 `USB_OTG_MODE_HOST`。

### Q4. 如何区分"充电 / 烧录 / MSC / UAC"四种场景？

| 场景 | 物理连接 | ESP32 角色 | 判定信号 / API |
|---|---|---|---|
| **充电** | 充电头 / PC 仅供电 | 无 | `usb_serial_jtag_is_connected() == false` **且** OTG Host 枚举不到任何设备 |
| **烧录 / monitor** | PC 打开串口 / 运行 monitor | Device(USJ) | `usb_serial_jtag_is_connected() == true` |
| **MSC** | 本机 USB MSC App 运行中 | Device(OTG) | 本系统当前无并发场景；由 `s_active` 标志互斥 |
| **UAC 耳机** | USB-C 耳机 | **Host(OTG)** | OTG 总线枚举到 `TUSB_CLASS_AUDIO` 设备 |

**实现要点**（Batch 1 探测顺序）：

```text
1. eos_storage_is_dir("/sdcard/spotify")  → 无 SD / 无目录 → errcode 0x01
2. usb_serial_jtag_is_connected() == true → 记录"需抢占"，继续（方案 B）
3. usb_new_phy(HOST)                      → 失败 → errcode 0x02
4. tusb_init() + 等待 enum (4s 超时)      → 超时 → errcode 0x03 "No earphone / charging only"
5. 检查枚举结果是否有 UAC 接口            → 无 UAC → errcode 0x04 "Not a USB audio device"
6. 通过 → 进入播放态
```

> 关键：**步骤 2 不能直接否决**。否则 monitor 插着时 App 永远打不开，与 MSC 体验不一致。
> 真正的"耳机是否连接"必须由 **步骤 4/5 的 OTG 枚举结果**判定，而非 USJ 状态。

### Q5. 如何实现 SD 卡访问互斥？

**采用"卸载方案"为主 + 状态标志为辅**，完全复用 USB MSC 已验证的机制：

```text
进入 UAC 前：
  board_sd_release()
    ├─ f_mount(NULL, "0:", 0)              // 卸载 FATFS 卷
    └─ esp_vfs_fat_unregister_path("/sdcard")  // 注销 VFS 挂载点
    → 保留 s_sd_card / SDSPI 设备（供本 App 内部经 VFS 重新自挂）

UAC 激活期间：
  · 本 App 需要读歌词/音频时才重新挂载（见下）
  · 其它 App 访问 /sdcard/* → VFS 无挂载点 → 直接返回 ENOENT / 无设备
    → 等价于"未插卡"，天然互斥，绝不会写坏卡
```

**为什么不用 mutex 独占？**

- mutex 只能防"本进程"竞争，无法阻止其它 App 的 POSIX `open()`。
- 卸载 VFS 才是**内核级**互斥：所有路径访问统一失败。
- 这正是 USB MSC 的处理方式，已被验证不会损伤 FAT（因为卡未被并发写）。

**UAC 期间本 App 自己怎么读 SD？**

关键洞察：**UAC 主机模式与 SD 卡并不冲突**（冲突的是 MSC 场景：PC 与本机同时操作 FAT）。
本 App 是"本机唯一访问者"，因此：

- **推荐方案**：进入 UAC **不卸载** SD，改用**读写互斥锁 + 独占标志**：
  - `s_sd_busy` 原子标志；所有本 App SD I/O 走内部封装 `_sd_read()`；
  - 其它 App 的访问通过**卸载 VFS** 阻断（见上）。
- **权衡**：若 UAC 激活期间也要让其它 App 完全无法访问，则用卸载方案；
  - 卸载后本 App 需自己 `esp_vfs_fat_register` 重新挂载（这就是 `board_sd_acquire()`）。
  - 因此采取：**进入 UAC 时 release（阻断他人）→ 本 App 立即 acquire（自己可用）→ 退出时 release+acquire 各一次确保干净。**

> 最终推荐：**进入前 `board_sd_acquire()` 确保挂载 → 只做 release/acquire 配对用于"短暂让位"**，
> 平时用 `s_sd_mutex`（FreeRTOS mutex）序列化本 App 内部的目录扫描与音频读取（避免同卡并发 seek）。
> 跨 App 互斥依赖 VFS 卸载。详见 Batch 4 实现。

### Q6. 退出恢复的详细步骤顺序

严格按任务书 §3.4，**单次同步调用完成**（`_teardown()`，幂等）：

```text
 1. 停止解码器         : 置 s_decode_run=false，等待读线程边界（LVGL 定时器内即同步返回）
 2. 清空 PCM 环形缓冲   : 读写指针归位，丢弃未发送数据
 3. 停止 UAC 数据发送   : 置 s_uac_streaming=false（等时端点回调见此标志即静默）
 4. 去初始化 TinyUSB    : tusb_deinit() / 释放 tinyusb_config_t
 5. 释放 USB-OTG PHY    : usb_del_phy(s_phy_hdl)
 6. 恢复 Serial-JTAG     : usb_serial_jtag_ll_phy_enable_external(false) + enable_pad(true)
 7. 重挂载 SD           : board_sd_acquire()，最多 3 次、间隔 50ms
                          （失败仅记录 errcode，绝不阻塞退出）
 8. 销毁 LVGL 对象       : 删除本 App 全部 screen/obj（框架 eos_activity 负责 view 回收）
 9. 释放本 App 堆内存    : eos_free 全部分配（目录节点 / 歌词 / 缓冲）
10. 返回 App 启动器      : eos_activity_back()（框架既有"切换 App"机制）
```

**顺序约束（务必遵守）**：

- **1→2→3→4→5 顺序不可乱**：必须先停数据流再拆栈，否则等时端点回调访问已释放资源 → 崩溃。
- **6 必须在 5 之后**：PHY 归还必须在 `usb_del_phy()` 之后，否则归还无效。
- **7 必须在 6 之后**：SD 重挂载需 console 恢复后打日志才可见（且避免与 OTG 争总线）。
- **第 7 步失败不回滚前序**：USB 已还原，SD 失败只显示 errcode 0x05，用户可重开 App。
- **幂等**：`s_teardown_done` 保证 `on_destroy` 与拔线路径重复调用安全。

---

## 3. 无任务 / 中断冲突分析（任务书 §6.3）

### 3.1 新建任务清单

| 任务 | 来源 | 优先级 | 栈 | 说明 |
|---|---|---|---|---|
| TinyUSB Host 内部任务 | `tusb_init()` | 由 TinyUSB 决定（默认 ~5） | 由 `CFG_TUSB_OS` 配置 | **唯一新增任务**，与 USB MSC 完全同构 |
| ~~应用层音频任务~~ | — | — | — | **不新增**：解码在 LVGL 定时器上下文分片执行 |
| ~~歌词刷新任务~~ | — | — | — | **不新增**：复用 `lv_timer_create` |

### 3.2 与既有中断的关系

| 中断 | 是否冲突 | 说明 |
|---|---|---|
| USB 中断 | ❌ 无 | OTG 与 USJ 共享 PHY 但**互斥使用**；同一时刻仅一方注册中断 |
| 触摸中断（CHSC6X I2C） | ❌ 无 | UAC 期间不接管触摸；仅在错误页注册 swipe_back 消费手势 |
| LVGL tick（esp_timer） | ❌ 无 | 解码分片挂在 `lv_timer` 上，与 tick 同源，无额外定时器 |
| SD SPI（SPI3，与 LCD 共享） | ❌ 无 | 复用 IDF `spi_bus_lock`；本 App 所有 SD I/O 经 `eos_storage_*` 封装 |

### 3.3 论证不干扰系统

- TinyUSB 任务仅做 USB 协议处理，不访问 LVGL / SD 之外的共享资源；
- 音频 PCM 生产在 LVGL 任务内**分片**（每次 ≤8ms 工作量），不长时间占用 ui_task；
- UAC 等时端点发送在 TinyUSB 任务内，只读环形缓冲（无锁 SPSC 或短临界区）；
- 优先级与 USB MSC 的 `eos_tusb`（prio 5）保持一致，已验证不干扰系统。

---

## 4. 可回退性设计（任务书 §6.4）

```cmake
# port/esp32s3/main/CMakeLists.txt
set(EOS_USB_UAC_APP_ENABLE 1 CACHE STRING "Enable USB UAC audio Spotify App (1=on, 0=off)")
if(EOS_USB_UAC_APP_ENABLE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_USB_UAC_APP_ENABLE=1)
    target_sources(${COMPONENT_LIB} PRIVATE
        "${EOS_ROOT}/resources/images/icon/eos_icon_spotify.c"
        "${EOS_ROOT}/resources/images/icon/eos_icon_earphone_err.c"
    )
endif()
```

- 所有新增/修改点用 `#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE` 包裹；
- `eos_app_list.h` / `eos_app_list.c` 的枚举与数组元素同样包裹；
- **关闭宏时**：不编译任何 Spotify 代码与图标，产物与改动前**功能一致**。
- `tusb_config.h` 的 Host 宏也需包裹（用 `#if CONFIG_USB_UAC_APP_ENABLE`），避免关闭时改变 TinyUSB 行为。

> **为什么不用 `CONFIG_USB_MSC_APP_ENABLE`？**
> 该宏同时控制 USB MSC App。复用它会导致"关掉 Spotify 顺带删掉 U 盘功能"，违反最小影响原则。
> 故新增独立宏。

---

## 5. App 注册方式（任务书 §6.2 末条）

现有原生 C App 注册机制（`eos_app_list.c` 已验证）：

```c
// eos_app_list.h —— 枚举追加
enum {
    EOS_NATIVE_APP_ALBUM = 0,
    EOS_NATIVE_APP_TEXTHUB,
    EOS_NATIVE_APP_DICTIONARY,
#if defined(CONFIG_USB_MSC_APP_ENABLE) && CONFIG_USB_MSC_APP_ENABLE
    EOS_NATIVE_APP_USB_MSC,
#endif
#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE
    EOS_NATIVE_APP_SPOTIFY,          /* ← 新增 */
#endif
    EOS_NATIVE_APP_LAST
};
```

```c
// eos_app_list.c —— 三个数组各追加一项
const char *eos_native_app_id_list[]   = { ..., "com.cantomk6.spotify" };
const char *eos_native_app_icon_list[] = { ..., EOS_IMG_SPOTIFY };
const eos_sys_app_entry_t eos_native_app_entry_list[] = { ..., eos_spotify_enter };
```

- 图标：与 USB MSC 同法，`extern const lv_image_dsc_t eos_icon_spotify;` 编译进 Flash，
  在 `_app_list_resolve_icon()` 内加一个 `if (native_index == EOS_NATIVE_APP_SPOTIFY) return &eos_icon_spotify;`。
- App 页点击 → `eos_app_launch_immediately("com.cantomk6.spotify")` → 命中 `eos_native_app_entry_list` → `eos_spotify_enter()`。
- **无需 JS / manifest / 插件系统**（这是 native C App，与 Album/Texthub 同层）。

---

## 6. 内存策略（任务书 §22）

| 用途 | 区域 | 理由 |
|---|---|---|
| PCM 环形缓冲（约 32–64KB） | **Internal RAM + DMA** | 供 TinyUSB 等时端点 DMA 读，必须 DMA-capable |
| MP3 解码器状态 / 帧缓冲 | PSRAM | 非 DMA，生命周期长 |
| 歌词解析结果（时间戳 + 文本行） | PSRAM | 体积随文件增长 |
| SD 文件树节点（懒加载，仅展开目录） | PSRAM | 参考 Texthub，按需分配按需释放 |
| LVGL 标签 / 弧控件 | LVGL 堆（已在 PSRAM） | 复用现有 LVGL buffer |
| 目录扫描临时缓冲 | 栈（小） | 单次 readdir 名称缓冲 |

> 原则：**DMA 走内部 RAM，其余大对象走 PSRAM**，与 AGENTS.md §22 一致。

---

## 7. 现存风险与待确认项

| # | 风险 | 等级 | 处理 |
|---|---|---|---|
| 1 | `tusb_config.h` 新增 Host 宏可能影响现有 Device 行为 | **中** | 用宏包裹；Batch 2 前后各编译一次对比 `idf.py size` |
| 2 | ESP-IDF `espressif__tinyusb` 是否内置 UAC **Host** 类 | **中** | Batch 2 第一步验证 `CFG_TUH_AUDIO` 是否存在；不存在则需自带 `audio_host.c` |
| 3 | MP3 解码 CPU 占用（240MHz 单核） | 中 | Batch 3 实测；必要时降采样或限定 CBR ≤192kbps |
| 4 | TinyUSB Host 与 Device 模式 PHY 配置差异（`USB_OTG_MODE_HOST`） | 中 | 参考 dcd/dwc2 host 初始化，Batch 2 实测枚举 |
| 5 | 圆屏弧控件 120° 触控热区与 swipe_back 手势冲突 | 中 | 弧外区域响应手势，弧内长按 1s 才进入调节（Batch 4） |
| 6 | `eos_icon_spotify.c` / `eos_icon_earphone_err.c` 不存在 | 低 | Batch 1 生成占位图（1×1 或纯色）保证可编译 |
| 7 | 歌词中文渲染字体档 | 低 | 复用 jbm_13 → han_sans_13 链（已验证支持 GB2312 一级字） |

---

## 8. 交付顺序确认

| 批次 | 内容 | 状态 |
|---|---|---|
| **B1** | 分析报告 + App 注册点 + 空壳 App + USB 模式检测 + 失败 UI | 进行中 |
| B2 | TinyUSB Host 初始化/去初始化 + UAC 枚举 + USJ 互斥恢复 + 错误码表 | 待开始 |
| B3 | SD 文件树 + MP3/WAV 解码 → PCM → UAC 推送 + LRC 三行滚动 | 待开始 |
| B4 | 音量弧 / 速度弧 + 退出恢复全流程 + 拔线自动退出 + 回退测试 | 待开始 |

---

## 9. Batch 1 骨架设计摘要

### 9.1 App 状态机

```text
INIT
 ├─ SD 无 /sdcard/spotify  → ERR(0x01) → DONE(可滑动退出)
 ├─ drv_begin() 失败        → ERR(0x02) → DONE
 ├─ 4s 内未枚举 UAC         → ERR(0x03) → DONE
 ├─ 枚举到非 UAC 设备       → ERR(0x04) → DONE
 └─ 成功                   → 文件树界面 → 播放态 → (拔线/手势) → TEARDOWN → 回 Launcher
```

### 9.2 错误码表（全英文）

| errcode | errmsg |
|---|---|
| `0x01` | `Music folder not found. Create /sdcard/spotify and add .mp3 or .wav files.` |
| `0x02` | `USB port in use by debug console (USB-Serial-JTAG). Disconnect idf.py monitor to use USB audio.` |
| `0x03` | `No earphone connection detected. Please connect a USB-C earphone.` |
| `0x04` | `Connected USB device is not an audio device.` |
| `0x05` | `Failed to re-mount the SD card after audio playback.` |
| `0x06` | `Failed to start the USB audio stack.` |

### 9.3 Batch 1 编译验证目标

- `EOS_USB_UAC_APP_ENABLE=1`：App 页出现 Spotify 图标，点击进入后显示探测中/失败页，可滑动退出。
- `EOS_USB_UAC_APP_ENABLE=0`：Spotify 完全消失，`idf.py size` 与改动前一致。
