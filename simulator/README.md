# ElenixOS Desktop Simulator

ElenixOS 桌面模拟器，使用 SDL2 窗口渲染 240×240 圆形屏幕 UI，完整运行 ElenixOS Core + JerryScript 脚本引擎。

## 快速开始

### 前置条件

- Anaconda3 安装在 `I:\Anaconda3`
- 已创建 conda 环境 `elenixos`（cmake, gcc, g++, ninja, make, sdl2）

如果环境不存在，先创建：

```bash
I:\Anaconda3\Scripts\conda.exe create -n elenixos cmake gcc gxx ninja make sdl2 -c conda-forge -y
```

### 一键构建 & 运行

```bat
simulator\build_and_run.bat
simulator\build_and_run.bat run
```

### 手动构建

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

copy %CONDA_PREFIX%\Library\bin\SDL2.dll simulator\build\
simulator\build\elenixos_sim.exe
```

## 目录结构

```
simulator/
├── CMakeLists.txt          # 顶层构建配置（引用主项目 + LVGL）
├── lv_conf.h               # LVGL 配置（SDL2 显示 + 所有 widget 启用）
├── eos_platform_config.h   # ElenixOS 平台覆盖（240x240、POSIX FS、C 字体）
├── main.c                  # 模拟器入口点
├── build_and_run.bat       # 一键构建脚本
└── build/                  # 构建输出（git 忽略）
```

## 运行时行为

模拟器启动时会：

1. 创建 SDL2 240×240 窗口
2. 初始化 LVGL（软件渲染 + SDL 直接渲染模式）
3. 创建鼠标输入设备（模拟触摸）
4. 调用 `eos_init()` 完整启动 ElenixOS
5. 进入主循环，驱动 LVGL 事件和动画

## 已知限制

- 缺少 `fs/.sys/res/img/logo.bin`：启动 logo 显示为黑屏，不影响功能
- Time device OPS 不可用：模拟器无 RTC 硬件，时间服务报错（预期行为）
- Battery device 不可用：模拟器无电池硬件（预期行为）
