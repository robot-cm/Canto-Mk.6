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

## 模拟器编译说明

在 PC 上用 SDL2 窗口 1:1 复刻真机（240×240 圆屏、表冠、侧键、假电池/时间/传感器），
无需硬件即可跑完整 UI / JS 回归。「让真机代码能在 PC 上编译运行」这件事**不需要改动
`src/`**，全部适配位于 `simulator/`（含头文件垫片、编译宏、LVGL/SDL 接线）。

> 注意：`src/apps/` 下确实有改动，但那是 issue #3 报告的**真机固件 bug 修复**
> （EqSolver float32 精度与求解日志、ProfMonitor 持久化绝对路径），真机同样受影响，
> 与模拟器适配无关。

### 1. 前置条件

- **Windows**：conda 环境 `elenixos`（含 cmake / gcc / make / SDL2）

```
I:\Anaconda3\Scripts\conda.exe create -n elenixos cmake gcc gxx ninja make sdl2 -c conda-forge -y
```

- **Ubuntu / Debian**：

```
sudo apt install build-essential cmake python3 libsdl2-dev fonts-noto-cjk
```

（`fonts-noto-cjk` 提供中文 TinyTTF 字体，缺失时中文显示为方框。）

### 2. 一键构建 + 自检（推荐）

Windows 在 Git Bash / MSYS2 下、Ubuntu 在任意 bash 下运行：

```
bash simulator/test_sim.sh
```

脚本自动识别平台，执行「配置 → 构建 → 逐项跑全部 headless 自检并打印结论」。
输出分两段：每项探针的**退出码**，以及末尾的**断言级验证**（逐条 `[OK]` / `[FAIL]`）。

> 注意：断言级验证依赖串口日志，因此脚本会显式以 **DEV 模式**（`CMAKE_BUILD_TYPE=`）配置构建。
> 若用 `-DCMAKE_BUILD_TYPE=Release` 配置过同一 build 目录，根 CMakeLists 会定义
> `COS_LOG_DISABLE=1` 让所有 `COS_LOG_*`（含 JS 侧 `cos.console.log`）变成空操作，
> 断言将全部失效。脚本每次配置都会把 build type 写回空值以避免该缓存陷阱。

### 3. 手动构建

Windows（cmd，需先把 conda 工具链加入 PATH）：

```
set CONDA_PREFIX=I:\Anaconda3\envs\elenixos
set PATH=%CONDA_PREFIX%\Library\bin;%CONDA_PREFIX%\Library\x86_64-w64-mingw32\bin;%CONDA_PREFIX%;%PATH%

cmake -S simulator -B simulator\build -G "MinGW Makefiles" ^
    -DCMAKE_C_COMPILER=%CONDA_PREFIX%\Library\bin\x86_64-w64-mingw32-gcc.exe ^
    -DCMAKE_CXX_COMPILER=%CONDA_PREFIX%\Library\bin\x86_64-w64-mingw32-g++.exe ^
    -DCMAKE_AR=%CONDA_PREFIX%\Library\bin\x86_64-w64-mingw32-ar.exe ^
    -DCMAKE_RANLIB=%CONDA_PREFIX%\Library\bin\x86_64-w64-mingw32-ranlib.exe ^
    -DCMAKE_MAKE_PROGRAM=%CONDA_PREFIX%\Library\bin\mingw32-make.exe

cmake --build simulator\build -j 8
```

Ubuntu / Debian：

```
cmake -S simulator -B simulator/build
cmake --build simulator/build -j 8
```

### 4. 运行

Windows 上 `SDL2.dll` 所在目录**必须**在 `PATH` 上，否则报 `0xC0000135` / 退出码 `-1073741515`：

```
set PATH=%CONDA_PREFIX%\Library\bin;%PATH%
simulator\build\cantomk6os_sim.exe
```

Ubuntu 无需额外设置：

```
./simulator/build/cantomk6os_sim
```

### 5. 交互方式

模拟器窗口出现后**先点一下窗口**让 SDL 拿到焦点，然后：

| 硬件 | 鼠标 / 键盘操作 |
| --- | --- |
| 触摸屏 | 按住左键拖动 = 触摸滑动；单击 = 点按 |
| 表冠旋转 | 滚轮上/下（或滚轮左右） |
| 表冠按下 | 鼠标中键 |
| 侧键 | 鼠标 X1（后退键） |

键盘热键：

| 键 | 作用 |
| --- | --- |
| `C` | 开/关控制中心 |
| `L` | 打开应用列表（Launcher） |
| `H` | 直接回到表盘 |
| `R` | 返回上一级（等同手势返回） |
| `W` | 切换 WOS 演示 App |
| `Esc` | 退出模拟器 |

启动时会打印上述操作提示。模拟器默认**关闭自动熄屏**（把熄屏超时拉到 1e6 秒），
避免放着不动就黑屏、单击又唤不醒（真机 SLEEP 态需双击亮屏）。

### 6. 模拟 SD 卡 / 系统盘

根文件系统重定向到 `simulator/build/fs/`（构建时自动铺设），结构对应真机分区：

```
simulator/build/fs/
├── sdcard/                 # 真机 SD 卡
│   ├── apps/               #   .eapk 插件（构建时自动布防内置 App）
│   ├── album/ 、history/ …  #   相册 / 历史 / 词典等运行数据
└── .sys/                   # 系统分区
    ├── app/apps/<id>/      #   已安装 App（manifest.json / main.js / icon.bin）
    ├── app/app_data/<id>/  #   App 私有配置 config.json（模拟器启动时按需补种）
    ├── res/img、res/font   #   图标 / 字体资源
    └── wf/faces            #   表盘
```

把真机 SD 卡内容拷进 `fs/sdcard/` 即可加载相册、词典（`ecdict.dat`）、`.eapk` 等数据；
示例数据目录结构参考 `examples-sdcard/`。

### 7. headless 自检（无需桌面）

单个探针可直接跑（`SDL_VIDEODRIVER=dummy` 表示不弹窗口）：

```
cd simulator/build
SDL_VIDEODRIVER=dummy ./cantomk6os_sim.exe --<开关>   # Windows
SDL_VIDEODRIVER=dummy ./cantomk6os_sim --<开关>        # Ubuntu
```

| 开关 | 覆盖内容 |
| --- | --- |
| `--shell-test` | Shell / App / WiFi / BT / Proxy / Plugin / IME |
| `--keyboard-test` | 键盘控件 EN / ZH 输入法 |
| `--ui-framework-test` | 自适应布局系统 54 项断言（4 屏） |
| `--ui-framework-demo` | 渲染层 demo：ArcList / Grid / Liquid Glass / 动画 |
| `--launcher-test` | 真实 Plugin-Manager 应用列表 × 4 屏 |
| `--ui-js-test` | `cos.ui.*` JerryScript 桥接 × 4 屏 |
| `--notes-probe` | `cos.fs.write/read`（字符串+Uint8Array）/ `cos.ime.open` |
| `--alarm-test` | 闹钟：空态 / 列表 / 向导 / 持久化 / 响铃层 / OFF |
| `--breach-launch` | Breach Protocol：Boot + 结构 / Boot→Hack 转场 |
| `--breach-result` | Breach：真实点击启动倒计时 → 超时 → Result overlay |
| `--watchface-gesture-test` | 表盘全屏 CLICKABLE 手势 catcher |
| `--home-gesture-sim-test` | 主页左/右/上滑动路由（PRESS/RELEASE） |
| `--home-gesture-indev-test` | 主页滑动真实事件路径（`lv_indev_read` 注入） |
| `--cards-page-test` | 小卡片页容器 / 种子卡 / chrome 注册 |
| `--cards-api-test` | 小卡片 App 注册接口（register/unregister/dedup） |
| `--stress-test` | 40 轮生命周期压力测试（卡死闪退加固） |
| `--screen-snap` | 截图诊断（watchface / app 页 / 控制中心） |
| `--pixel-probe` | 读取 SDL 纹理逐行亮度诊断 |
| `--render-check` | 导出实际呈现像素（含 overlay）为 PPM |
| `--profmonitor-probe` | 原生 ProfMonitor：启动 / UI / CPU 读数 / SRAM / 持久化 |
| `--eqsolver-test` | EqSolver：单/多变量求解与结果格式化 |

### 8. 常见问题

- **`0xC0000135` / 退出码 `-1073741515`**：Windows 找不到 `SDL2.dll`，把
  `%CONDA_PREFIX%\Library\bin` 加进 `PATH`。
- **窗口全黑 / 放着不动就黑屏**：模拟器已禁用自动熄屏；若仍异常，确认
  `simulator/lv_conf.h` 里 `LV_SDL_ACCELERATED` 为 `0`（软件渲染）。
- **无桌面环境（CI / SSH）**：加 `SDL_VIDEODRIVER=dummy`，或直接跑
  `bash simulator/test_sim.sh`（全部自检均为 headless）。
- **断言级验证全 `[FAIL]`**：多半是用 Release 配置过同一 build 目录（日志被
  `COS_LOG_DISABLE` 关掉）；删掉 `simulator/build` 重新配置，或用
  `bash simulator/test_sim.sh`（脚本会强制 DEV 模式）。

更深入的细节（运行时行为、已知限制）见 [simulator/README.md](simulator/README.md)。

## 更新日志

### 20261001

**修复**

- **JS App 全部无法打开（严重）** — `spm_start_program()` 在"同类型脚本程序已存活"时仍会再创建一个程序：旧程序不会被 `spm_app_stop()` 命中（它按 `SCRIPT_TYPE_APPLICATION` 只取第一个），也不再被任何路径销毁，于是永久滞留在 `s_program_list` 中。其 realm 与 SNI context（[sni_context.c](src/script_engine/sni/sni_context.c)）随之泄漏 —— 而 SNI context 对脚本创建的每一个 JS/LVGL 对象都持有强引用（`jerry_value_copy`），因此**每次启动泄漏约 16.5 KB JerryScript 堆**。堆被吃满后 `jerry_parse` 对所有脚本一律失败，表现为：

  ```
  [ScriptEngine] Script Parse Error: null
  [ScriptEngine] ENGINE_RUN: returning result=-701
  [SPM] spm_start_program: execution failed ret=-701
  [AppList] Application encounter a fatal error
  ```

  且**不可恢复** —— 此后所有 JS App 都打不开。修复方式是在 [spm.c](src/script_engine/spm/spm.c) 的 `spm_start_program()` 中按类型强制"同类型仅一个存活程序"，复用既有的 `spm_terminate_programs_by_type()`。该不变式本来就是 `spm_app_stop()` / `spm_app_suspend()` / `spm_app_resume()` 三者的前提（它们都用 `spm_get_program_by_type()` 去找"那个"程序）。

  > 归属说明：`db2f5f6`（计时器/闹钟圆角修复 + ProfMonitor）只改动 App 的 JS 与资源，未触碰 `spm.c`、`sni/*`、`script_engine` 核心，因此该泄漏本身是既有缺陷，`db2f5f6` 只是让它更快触发。已在修复版上实测排除 `roundClip` 相关嫌疑。

  **验证**：`--stress-test` 40 轮 80 次启动，修复前 56 次终止（24 个孤儿程序，堆满后必失败）、修复后 79 次终止（仅剩进程退出时仍存活的 1 个），`Script Parse Error` / `ret=-701` 均为 0；16 个 headless 探针（shell / keyboard / ui-framework / launcher / ui-js / notes / breach×2 / watchface-gesture / home-gesture×2 / cards×2 / alarm / render-check / stress）全部通过。

**新增**

- **桌面模拟器（Windows / Linux 双平台）** — 新增 `simulator/`，SDL2 渲染 + 19 个 headless 自检开关，可在 PC 上跑完整 UI / JS 回归，无需真机。
  - Windows：将 `SDL2.dll` 所在目录（如 `...\Library\bin`）加入 `PATH` 后运行 `simulator\build\cantomk6os_sim.exe`
  - Ubuntu：`bash simulator/test_sim.sh`
  - 模拟器目录需自备 SD 卡镜像根 `simulator/build/fs/`，构建时自动铺设

## 硬件兼容性提醒
- 仅测试过普通 **XIAO ESP32-S3**（非 Sense 非 Plus版），不使用摄像头/麦克风/板载 SD
- 测试硬件链接：
    https://shop.seeedstudio.com.cn/Seeed-Studio-XIAO-ESP32S3-Pre-Soldered-p-6334.html
    https://shop.seeedstudio.com.cn/1-28-Round-Touch-Display-for-Seeed-Studio-XIAO-ESP32.html
