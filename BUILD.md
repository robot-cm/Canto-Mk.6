# CantoMk6 编译指南（Simulator）

> 本文档面向 **Windows 环境下的 Native 模拟器** 构建与测试。
> MCU（ESP-IDF）/ WASM 目标见文末第 8 节。

## 1. 环境要求

| 组件 | 说明 |
|------|------|
| Windows 10/11 + **Git Bash** | 构建/测试脚本均按 bash 编写；cmd.exe 可能被安全策略拦截 |
| **CMake ≥ 3.12** | 生成器用 `MinGW Makefiles` |
| **MinGW-w64** | `gcc` / `g++` / `ar` / `ranlib` / `mingw32-make` |
| **Python 3** | 打包 `.eapk`（`scripts/cos_pkg_builder.py`）与 jerryscript 配置阶段 |
| **SDL2** | 含 dev 头文件/库；本工程按 conda `Library` 布局查找 |

本机已验证的工具链（conda 环境示例，等价工具链亦可）：

```bash
CONDA_PREFIX=I:/Anaconda3/envs/cantomk6os
# Library/bin      → cmake / mingw32-make / SDL2
# Library/x86_64-w64-mingw32/bin → gcc / g++ / ar / ranlib
export PATH="$CONDA_PREFIX/Library/bin:$CONDA_PREFIX/Library/x86_64-w64-mingw32/bin:$CONDA_PREFIX:$PATH"
```

## 2. 一键构建 + 全量自检（推荐）

```bash
bash simulator/test_sim.sh
```

脚本自动完成：重新配置 → 编译 `cantomk6os_sim` → 打包全部 `.eapk` → 21 步 headless 自检（压力/Shell/输入法/各 app 探针），全绿输出 `[OK]` 结论。

## 3. 手动构建

### 3.1 配置（生成 MinGW Makefiles）

```bash
export PATH="$CONDA_PREFIX/Library/bin:$CONDA_PREFIX/Library/x86_64-w64-mingw32/bin:$CONDA_PREFIX:$PATH"
export PY_EXE="$(command -v python)"

cmake -S simulator -B simulator/build -G "MinGW Makefiles" \
  -DCMAKE_C_COMPILER="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-gcc.exe" \
  -DCMAKE_CXX_COMPILER="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-g++.exe" \
  -DCMAKE_AR="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-ar.exe" \
  -DCMAKE_RANLIB="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-ranlib.exe" \
  -DCMAKE_MAKE_PROGRAM="$CONDA_PREFIX/Library/bin/mingw32-make.exe" \
  -DPYTHON="$PY_EXE"
```

> `simulator/CMakeLists.txt` 从 `$CONDA_PREFIX/Library` 定位 SDL2；未设置 CONDA_PREFIX 时走手动 fallback 路径，找不到会报错。

### 3.2 构建

```bash
cmake --build simulator/build -j 8
```

构建产物：

| 产物 | 位置 | 说明 |
|------|------|------|
| 模拟器主程序 | `simulator/build/cantomk6os_sim.exe` | SDL2.dll 已自动拷到同目录 |
| 模拟文件系统 | `simulator/build/fs/` | 资源/字体/样例/eapk 均在此 staging |
| 各 App 包 | `simulator/build/fs/.sys/app/com.cantomk6.*.eapk` | POST_BUILD 自动打包 |
| SD 卡副本 | `simulator/build/fs/sdcard/apps/` | boot 时 Plugin Manager 自动扫描安装 |

## 4. 运行

```bash
# 交互（弹出 SDL 窗口，需要 GUI 桌面）
cd simulator/build && ./cantomk6os_sim.exe

# 无头（headless，供 CI/脚本用）
cd simulator/build && SDL_VIDEODRIVER=dummy ./cantomk6os_sim.exe --<probe>
```

## 5. 只重打某个 App 的 eapk

改动 `apps/<id>/main.js` 等 JS 源码**无需重编 C 代码**，重打对应目标即可（POST_BUILD 自动 stage 到模拟 SD 卡）：

```bash
cmake --build simulator/build --target breach_eapk -j 8   # 示例：入侵协议
```

可用目标：`pmdemo_eapk` `clock_eapk` `timer_eapk` `globaltime_eapk` `alarm_eapk` `album_eapk` `notes_eapk` `draw_eapk` `breach_eapk`

> **注意**：运行实例读的是 `simulator/build/fs/.sys/app/apps/com.cantomk6.<id>/`（boot 时从 eapk 解包）。若改了 JS 但运行仍旧，删除该 app 的安装子目录（约 2 个文件）再启动，boot 会重新安装新包。改图标可跑 `scripts/icon/sync_app_icons.py` 把 `icon/*.webp` 同步为 48px `icon.bin` 并直覆运行实例。

## 6. 常用 headless 探针（main.c 各 --xxx 分支）

| 探针 | 验证内容 |
|------|---------|
| `--stress-test` | 40 轮应用进入/退出 + 定时器 tick，无崩溃/泄漏 |
| `--shell-test` | Shell / App / WiFi / BT / Proxy / Plugin / IME 全链路 |
| `--keyboard-test` | 英文/中文输入法自检 |
| `--alarm-test` | 闹钟模块（历史：pass=32 fail=0） |
| `--notes-launch` / `--draw-launch` / `--breach-launch` / `--breach-result` | 各 app 启动 / 画图 round-trip / 入侵协议启动与结果页 |

## 7. 常见坑

- **SDL2 找不到** → `CONDA_PREFIX` 未设置或 PATH 没带 `Library/bin`，先激活 conda 环境。
- **中文变方块** → POST_BUILD 会自动从 `C:/Windows/Fonts/simhei.ttf`（或 msyh.ttc）拷字体到 `fs/.sys/res/font/`；系统无这些字体时告警但编译成功。
- **cmd.exe 被拦截** → 一律走 Git Bash + 原生 `cmake`（勿用 `.bat`）。
- **改 JS 不生效** → 见第 5 节，删运行实例安装目录强制重装。
- **`cos`/SNI 相关**：JS 运行时按 `cos.*` 桥接（`src/script_engine/sni/`），勿依赖未注册 API（如 `clearFlag`），事件回调内 `cos` 全局不可解析，需闭包捕获。

## 8. 其它构建目标（简介）

- **MCU（ESP-IDF）**：`COS_PLATFORM` 非 `Native`，`fs/` 位于工程根（`COS_SYS_ROOT_DIR` 指向源码 `fs/`），走 ESP-IDF 工具链，非本模拟器流程。
- **WASM**：`COS_IS_WASM=1`，`COS_SYS_ROOT_DIR="/"`，见根 `CMakeLists.txt` 分支。

---

License: Apache-2.0（本仓库为非官方 fork，见 README 顶部声明）
