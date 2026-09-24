# Canto Mk.6

基于 **Seeed Studio XIAO ESP32-S3** + **1.28" 圆形触摸显示屏 (240×240, GC9A01)** 的轻量级微型设备系统。Core 稳定、系统服务统一提供能力、App 可插件化（`.eapk` 置于 SD 卡）。

## 硬件支持
- XIAO ESP32-S3 (ESP32-S3R8, 8MB Flash / 8MB PSRAM)
- 1.28" 圆形触摸 LCD (GC9A01, SPI)
- 电容触摸
- microSD/TF 卡
- RTC
- 电池充电

## 系统功能 (Core + Services)
- **控制分中心 (Control Center)**：WiFi, BT, PowerSave, BeastMode 快速开关, Settings 和 FlashLight 入口；亮度/蓝牙/WiFi/电源模式设置落盘 SD（无卡回退 cfg.json），待机前写入、开机早于 `cos_init()` 恢复
- **锁屏 (Lock)**：`cos_lock_page`，支持密码解锁
- **通知系统 (Notification)**：`cos_wos_notification`
- **系统 Shell (USB/UART)**：`cos_shell` — 底层调试入口，独立于 SD 卡，可查看 mem/ps/wifi/sd/apps/log 等
- **网络服务 (Network)**
  - Wi-Fi：扫描 / 连接 / 历史记忆 / 自动重连（`cos_net_wifi`）
  - 蓝牙 BLE（`cos_net_bt`）
  - **WIREGUARD 客户端代理**（`cos_net_proxy`，配置 `proxy.*`，密码不出现在明文日志）
  - SNTP 联网自动校时
- **时间服务 (RTC + NTP)**：`cos_service_time`，墙钟 + 时区 + 网络校时
- **电源管理 (PM)**：`cos_service_pm` — 熄屏 / Light Sleep / Deep Sleep 状态机，深睡 UI 快照恢复，双击唤醒
- **省电 / 睡眠**：`cos_service_power_save` — L2 自动待机 15 分钟 / 5 击恢复，熄屏关闭 GC9A01 面板，DFS 80-160MHz 调频
- **USB MSC (U 盘)**：`COS_NATIVE_APP_USB_MSC` — SD 卡作为 U 盘暴露给 PC（`CONFIG_USB_MSC_APP_ENABLE` 默认开）
- **野兽模式 (Beast Mode)**：性能模式切换
- **电池服务**：`cos_service_battery`（电量/充电检测）
- **传感器服务**：`cos_service_sensor`（加速度/计步等）
- **触觉反馈 (Haptic)**：`cos_service_haptic`
- **音频播放**：`cos_audio_player`
- **插件管理 (Plugin Manager)**：扫描/安装/卸载 SD 卡 `.eapk` App（`cos_plugin_manager`）
- **输入法 (IME/Pinyin)**：`cos_pinyin` 中文拼音输入
- **包管理 (Package Manager)**：`.eapk` / `.ewpk` 安装卸载（`cos_pkg_mgr`）
- **Launcher**：圆形弧形 App 网格（`cos_launcher_v1` + `cos_arclist`）
- **Watchface**：内置表盘 + JS 表盘支持（`cos_watchface_builtin` / `cos_watchface_js`）
- **动画框架**：液体玻璃、物理动效、卡片分页等统一 UI 组件

## 内置 App
| App | 功能 |
|-----|------|
| **Settings** | 系统设置：Wi-Fi、蓝牙、代理(SOCKS5)、时间、显示、语言、省电、锁屏、野兽模式等 |
| **FlashLight** | 手电筒，48 色板选色，SD 记忆光色 |
| **Dictionary** | 英汉词典，模糊前缀搜索、结果列表、详情页（音标/释义/词性/变形） |
| **Album** | 图片浏览（JPEG 解码 `tjpgd`），SD 卡相册，文件名中文正常显示 |
| **Calculator** | 计算器 |
| **GlobalTime** | 世界时钟 |
| **Stopwatch** | 秒表 |
| **Timer** | 倒计时 |
| **Alarm** | 闹钟 |
| **Calendar** | 日历 |
| **Breach** | 工具类 App（见 manifest） |
| **TextHub** | 文本/文件查看与工具（支持子页面内部导航），文件名中文正常显示 |
| **USBMSC** | SD 卡作为 U 盘暴露给 PC（默认开启） |

## 刷机说明
1. 下载 3 个 bin 文件（bootloader / partition-table / cantomk6os_esp32s3）
2. 使用 `esptool` 或 Chrome ESP32 烧录工具，按 `build/flash_project_args` 偏移写入
3. SD 卡示例文件见 `examples-sdcard-files.7z`, `README` 参考 `CantoMk6-fork/examples-sdcard`
4. 从源码构建：

```
    cd /.../CantoMk6-fork/port/esp32s3 && idf.py -p /dev/ttyACM0 build flash monitor
```

5. 从源码构建 js app 文件 eapk：

```
python3.11 scripts/icon/sync_app_icons.py

python3.11 scripts/cos_pkg_builder.py apps/alarm eapk-target/alarm.eapk --type app 

python3.11 scripts/cos_pkg_builder.py apps/breach eapk-target/breach.eapk --type app 

python3.11 scripts/cos_pkg_builder.py apps/calculator eapk-target/calculator.eapk --type app 

python3.11 scripts/cos_pkg_builder.py apps/calendar eapk-target/calendar.eapk --type app 

python3.11 scripts/cos_pkg_builder.py apps/globaltime eapk-target/globaltime.eapk --type app 

python3.11 scripts/cos_pkg_builder.py apps/stopwatch eapk-target/stopwatch.eapk --type app 

python3.11 scripts/cos_pkg_builder.py apps/timer eapk-target/timer.eapk --type app 
```

## 硬件兼容性提醒
- 仅测试过普通 **XIAO ESP32-S3**（非 Sense 非 Plus版），不使用摄像头/麦克风/板载 SD
- 测试硬件链接：
    https://shop.seeedstudio.com.cn/Seeed-Studio-XIAO-ESP32S3-Pre-Soldered-p-6334.html
    https://shop.seeedstudio.com.cn/1-28-Round-Touch-Display-for-Seeed-Studio-XIAO-ESP32.html
