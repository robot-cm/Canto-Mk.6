# CantoMk6OS 桌面模拟器（Windows / Ubuntu）

在 **Windows 与 Ubuntu** 上用 SDL2 窗口 1:1 复刻 CantoMk6 硬件（240×240 圆形触摸屏、
表冠、侧键、电池、时间、传感器），完整运行 CantoMk6OS Core + JerryScript 脚本引擎。
「让真机代码在 PC 上编译运行」不需要改动 `src/`、`port/`、`third_party/` 与根
`CMakeLists.txt`，适配全部在 `simulator/`。`src/apps/` 下的改动属于 issue #3 的真机
固件 bug 修复（EqSolver 精度/日志、ProfMonitor 绝对路径），与模拟器适配无关。

> 两个平台的差异仅在构建系统层：Windows 走 conda `elenixos` 环境的 MinGW 工具链 +
> 该环境自带 SDL2；Ubuntu 走系统 `gcc`/`cmake` + `libsdl2-dev`。C 源码通过
> `#ifdef _WIN32` 分支适配（控制台、休眠、网络 socket），其余完全一致。

## 前置条件

### Windows

- Anaconda3 安装在 `I:\Anaconda3`
- 已创建 conda 环境 `elenixos`（cmake、gcc、g++、make、sdl2）

如果环境不存在，先创建：

```bash
I:\Anaconda3\Scripts\conda.exe create -n elenixos cmake gcc gxx ninja make sdl2 -c conda-forge -y
```

### Ubuntu / Debian

```bash
sudo apt install build-essential cmake python3 libsdl2-dev fonts-noto-cjk
```

（`fonts-noto-cjk` 提供中文 TinyTTF 字体；缺失时中文会显示成方框。）

## 一键构建 + 自检（推荐）

Windows 在 Git Bash / MSYS2 下、Ubuntu 在任意 bash 下运行：

```bash
bash simulator/test_sim.sh
```

脚本会：配置 → 构建 → 逐项跑全部 headless 自检并打印验证结论。平台自动识别
（Windows 用 MinGW Makefiles，Ubuntu 用系统默认生成器）。

> 脚本显式以 **DEV 模式**（`-DCMAKE_BUILD_TYPE=`）配置构建。脚本末尾的「断言级验证」
> 依赖串口日志，而 Release 模式下根 `CMakeLists.txt` 会定义 `COS_LOG_DISABLE=1`，
> 使全部 `COS_LOG_*`（含 JS 侧 `cos.console.log` 的 `[OK]` / `[FAIL]`）变成空操作，
> 断言将全部失效。手动构建时若要看日志，请勿传 `-DCMAKE_BUILD_TYPE=Release`。

## 手动构建

### Windows

```bat
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

### Ubuntu / Debian

```bash
cmake -S simulator -B simulator/build
cmake --build simulator/build -j 8
```

### 运行（Windows 重要）

Windows 运行 exe 时 `%CONDA_PREFIX%\Library\bin` **必须**在 `PATH` 上，否则找不到
`SDL2.dll`（报 `0xC0000135` / 进程退出码 `-1073741515`）。Ubuntu 无需额外设置。

```bat
set PATH=%CONDA_PREFIX%\Library\bin;%PATH%
simulator\build\cantomk6os_sim.exe
```

## headless 自检（SDL_VIDEODRIVER=dummy，无需桌面）

```bash
cd simulator/build
SDL_VIDEODRIVER=dummy ./cantomk6os_sim.exe --<flag>   # Windows
SDL_VIDEODRIVER=dummy ./cantomk6os_sim --<flag>       # Ubuntu
```

| flag | 覆盖内容 |
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
| `--eqsolver-test` | EqSolver：单 / 多变量求解与结果格式化 |

## 目录结构

```
simulator/
├── CMakeLists.txt          # 顶层构建配置（引用主项目 + LVGL，注入 COS_SIMULATOR=1）
├── main.c                  # 模拟器入口：SDL 窗口/输入接线 + 全部 --flag 自检分支
├── lv_conf.h               # LVGL 配置（SDL2 显示、单线程 LV_OS_NONE、widget 全开）
├── cos_platform_config.h   # 平台覆盖（240x240、POSIX FS、C 字体），优先于 port/esp32s3/main/
├── esp_shim/               # ESP-IDF 垫片（esp_heap_caps、esp_sntp_shim、
│                           #   freertos_shim.c + freertos/，供 ProfMonitor 采样）
├── tools/version.py        # jerryscript 版本号桩（CMAKE_SOURCE_DIR 指向 simulator/ 时用）
├── test_sim.sh             # 一键构建 + headless 自检脚本
└── build/                  # 构建输出（含 fs/ 模拟文件系统，git 忽略）
```

## 运行时行为

模拟器启动时会：

1. `lv_init()` 初始化 LVGL（软件渲染）
2. 创建 SDL2 240×240 窗口作为显示（`lv_sdl_window_create`）
3. 注册鼠标输入设备（左键 = 触摸）与键盘输入设备
4. 安装 SDL 事件过滤器：滚轮 = 表冠、中键 = 表冠按下、X1 = 侧键、字母键 = 热键
5. 调用 `cos_init()` 完整启动 CantoMk6OS（`COS_SIMULATOR=1` 时注册假硬件
   time / battery / power / sensor）
6. 布防内置 app 插件，进入主循环驱动 LVGL 事件、动画与 Shell

## 已知限制

- 目标文件系统重定向到 `simulator/build/fs/`（即 `COS_SYS_ROOT_DIR`）
- 无真实 RTC / 电池 / 传感器硬件，时间与电量由假硬件提供
- 网络走 socket 模拟后端（Windows 链接 `ws2_32`，Linux 走 POSIX socket）
- 中文显示依赖宿主 CJK 字体：Windows 取 `C:/Windows/Fonts/simhei.ttf`，
  Linux 取 `fonts-noto-cjk` / 文泉驿 / Droid（CMake 自动探测，找不到则告警）
