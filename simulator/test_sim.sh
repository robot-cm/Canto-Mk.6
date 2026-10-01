#!/usr/bin/env bash
# ==============================================================================
# CantoMk6OS 桌面模拟器：一键配置 + 构建 + headless 冒烟/回归自检
#
# 用法(Windows: Git Bash / MSYS2;Ubuntu: 普通 bash)：
#   bash simulator/test_sim.sh
#
# 交互式手动运行（需桌面 GUI 弹 SDL 窗口）：
#   Windows: cd simulator/build && ./cantomk6os_sim.exe
#   Ubuntu : cd simulator/build && ./cantomk6os_sim
#
# 平台差异：
#   * Windows : 用 conda elenixos 环境(含 MinGW 工具链 + SDL2.dll)。
#               $CONDA_PREFIX/Library/bin 必须在 PATH 上，否则找不到 SDL2.dll
#               (报 0xC0000135 / exit -1073741515)。本脚本已前置该路径。
#   * Ubuntu  : 用系统 cmake/gcc 与 libsdl2-dev。若 python 不可达，用 python3
#               替代(jerryscript 的 tools/version.py 由 PYTHON 调用)。
#   * 自检全部走 SDL_VIDEODRIVER=dummy（无窗口），逐项打印退出码 + 断言摘要。
# ==============================================================================
set -euo pipefail

ok()  { echo "  [OK]   $1"; }
bad() { echo "  [FAIL] $1"; }

# ---- 平台检测 ----
case "$(uname -s)" in
  Linux*)               PLATFORM=linux ;;
  MINGW*|MSYS*|CYGWIN*) PLATFORM=windows ;;
  Darwin*)              PLATFORM=macos ;;
  *)                    PLATFORM=unknown ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM_DIR="$SCRIPT_DIR"
BUILD_DIR="$SIM_DIR/build"

# Windows 路径(I:/x/y) → MSYS 路径(/i/x/y)：Git Bash 把 PATH 条目里的 ':' 当分隔符
win_to_msys() {
  local p="${1//\\//}"
  if [[ "$p" =~ ^([A-Za-z]):/(.*)$ ]]; then
    local d="${BASH_REMATCH[1]}"
    echo "/${d,,}/${BASH_REMATCH[2]}"
  else
    echo "$p"
  fi
}

CONFIGURE_ARGS=()
if [ "$PLATFORM" = windows ]; then
  EXE="./cantomk6os_sim.exe"
  # conda 前缀：优先取 $CONDA_PREFIX，否则退回常见默认位置(I 盘 Anaconda)
  CONDA_WIN="${CONDA_PREFIX:-I:/Anaconda3/envs/elenixos}"
  CONDA_WIN="${CONDA_WIN//\\//}"
  CONDA_MSYS="$(win_to_msys "$CONDA_WIN")"
  export PATH="$CONDA_MSYS/Library/bin:$CONDA_MSYS/Library/x86_64-w64-mingw32/bin:$CONDA_MSYS:$PATH"

  # jerryscript configure 通过 `python` 调用 tools/version.py；确保 python 可达
  if ! command -v python >/dev/null 2>&1; then
    for p in "/i/Anaconda3" "/c/Users/Administrator/AppData/Local/Programs/Python/Python312"; do
      if [ -x "$p/python.exe" ]; then export PATH="$p:$PATH"; break; fi
    done
  fi
  PY_EXE="$(command -v python || echo python)"
  CMAKE="$CONDA_WIN/Library/bin/cmake.exe"
  CONFIGURE_ARGS=(
    -G "MinGW Makefiles"
    -DCMAKE_C_COMPILER="$CONDA_WIN/Library/bin/x86_64-w64-mingw32-gcc.exe"
    -DCMAKE_CXX_COMPILER="$CONDA_WIN/Library/bin/x86_64-w64-mingw32-g++.exe"
    -DCMAKE_AR="$CONDA_WIN/Library/bin/x86_64-w64-mingw32-ar.exe"
    -DCMAKE_RANLIB="$CONDA_WIN/Library/bin/x86_64-w64-mingw32-ranlib.exe"
    -DCMAKE_MAKE_PROGRAM="$CONDA_WIN/Library/bin/mingw32-make.exe"
    -DPYTHON="$PY_EXE"
  )
else
  EXE="./cantomk6os_sim"
  CMAKE="${CMAKE:-cmake}"
  # jerryscript 默认调用 `python`；Ubuntu 上通常只有 python3
  if command -v python >/dev/null 2>&1; then
    CONFIGURE_ARGS+=(-DPYTHON="$(command -v python)")
  elif command -v python3 >/dev/null 2>&1; then
    CONFIGURE_ARGS+=(-DPYTHON="$(command -v python3)")
  fi
fi

# ---- headless 运行器：run_headless <flag> <log> ----
# 返回 exe 退出码；输出重定向到 <log>。（必须在 BUILD_DIR 下调用）
run_headless() {
  local flag="$1"; local log="$2"
  set +e
  SDL_VIDEODRIVER=dummy "$EXE" "$flag" > "$log" 2>&1
  local rc=$?
  set -e
  return $rc
}

echo "==> [1] Configure ($PLATFORM)"
"$CMAKE" -S "$SIM_DIR" -B "$BUILD_DIR" "${CONFIGURE_ARGS[@]}"

echo "==> [2] Build"
"$CMAKE" --build "$BUILD_DIR" -j 8

cd "$BUILD_DIR"

# ── 平台/框架层 ────────────────────────────────────────────────
echo "==> [3] Shell framework self-test (--shell-test: Shell/App/WiFi/BT/Proxy/Plugin/IME)"
LOG="$BUILD_DIR/shelltest_log.txt"
if run_headless --shell-test "$LOG"; then ok "Shell 自检退出码 0"; else bad "Shell 自检退出码非 0"; fi

echo "==> [4] Keyboard self-test (--keyboard-test: EN/ZH IME)"
KLOG="$BUILD_DIR/keyboardtest_log.txt"
if run_headless --keyboard-test "$KLOG"; then ok "键盘自测退出码 0"; else bad "键盘自测退出码非 0"; fi

echo "==> [5] UI Framework self-test (--ui-framework-test: 54 项自适应布局/ArcList/物理/动画)"
UIFW="$BUILD_DIR/uifwtest_log.txt"
if run_headless --ui-framework-test "$UIFW"; then ok "UI 框架自测退出码 0"; else bad "UI 框架自测退出码非 0"; fi

echo "==> [6] UI Framework rendering demo (--ui-framework-demo: 4 屏 P8 ArcList/Grid/LiquidGlass)"
UIDEMO="$BUILD_DIR/uifwdemo_log.txt"
if run_headless --ui-framework-demo "$UIDEMO"; then ok "UI 框架渲染层 demo 退出码 0"; else bad "UI 框架渲染层 demo 退出码非 0"; fi

echo "==> [7] Launcher migration test (--launcher-test: 真实 Plugin-Manager 列表 × 4 屏)"
LAUNCHER="$BUILD_DIR/launchertest_log.txt"
if run_headless --launcher-test "$LAUNCHER"; then ok "Launcher 迁移测试退出码 0"; else bad "Launcher 迁移测试退出码非 0"; fi

echo "==> [8] UI-JS bridge self-test (--ui-js-test: cos.ui.* 自适应 Home × 4 屏)"
UIJS="$BUILD_DIR/uijstest_log.txt"
if run_headless --ui-js-test "$UIJS"; then ok "UI-JS 桥接自测退出码 0"; else bad "UI-JS 桥接自测退出码非 0"; fi

echo "==> [9] Notes/Draw platform probe (--notes-probe: fs.write string+Uint8Array / fs.read 二进制安全 / ime.open)"
NPROBE="$BUILD_DIR/notesprobe_log.txt"
if run_headless --notes-probe "$NPROBE"; then ok "笔记/画图平台探针退出码 0"; else bad "笔记/画图平台探针退出码非 0"; fi

# ── 内置 app 层 ────────────────────────────────────────────────
echo "==> [10] Alarm app state-machine regression (--alarm-test)"
ALARM="$BUILD_DIR/alarmtest_log.txt"
if run_headless --alarm-test "$ALARM"; then ok "闹钟状态机退出码 0"; else bad "闹钟状态机退出码非 0"; fi

echo "==> [11] Breach Protocol app probe (--breach-launch: boot + 结构 / Boot->Hack 转场)"
BLAUNCH="$BUILD_DIR/breachlaunch_log.txt"
if run_headless --breach-launch "$BLAUNCH"; then ok "入侵协议启动退出码 0"; else bad "入侵协议启动退出码非 0"; fi

echo "==> [12] Breach Protocol Result-page probe (--breach-result: 真实点击启动倒计时 -> 超时 -> Result overlay)"
BRESULT="$BUILD_DIR/breachresult_log.txt"
if run_headless --breach-result "$BRESULT"; then ok "入侵协议 Result 页退出码 0"; else bad "入侵协议 Result 页退出码非 0"; fi

# ── 手势/小卡片 ────────────────────────────────────────────────
echo "==> [13] Watchface gesture catcher (--watchface-gesture-test: 全屏 CLICKABLE catcher)"
WFG="$BUILD_DIR/wfgtest_log.txt"
if run_headless --watchface-gesture-test "$WFG"; then ok "表盘手势 catcher 退出码 0"; else bad "表盘手势 catcher 退出码非 0"; fi

echo "==> [14] Home swipe-navigation (--home-gesture-sim-test: 左/右/上 PRESS+RELEASE 路由)"
HGSIM="$BUILD_DIR/hgtest_log.txt"
if run_headless --home-gesture-sim-test "$HGSIM"; then ok "主页滑动导航退出码 0"; else bad "主页滑动导航退出码非 0"; fi

echo "==> [15] Home swipe REAL-event-path (--home-gesture-indev-test: lv_indev_read 注入 PRESS/drag/RELEASE)"
HGINDEV="$BUILD_DIR/hgitest_log.txt"
if run_headless --home-gesture-indev-test "$HGINDEV"; then ok "主页真实事件路径滑动退出码 0"; else bad "主页真实事件路径滑动退出码非 0"; fi

echo "==> [16] Cards-page regression (--cards-page-test)"
CPS="$BUILD_DIR/cpstest_log.txt"
if run_headless --cards-page-test "$CPS"; then ok "小卡片页退出码 0"; else bad "小卡片页退出码非 0"; fi

echo "==> [17] Cards-page app API regression (--cards-api-test: register/unregister/dedup)"
CPSAPI="$BUILD_DIR/cpsapi_log.txt"
if run_headless --cards-api-test "$CPSAPI"; then ok "小卡片 App 注册接口退出码 0"; else bad "小卡片 App 注册接口退出码非 0"; fi

# ── 稳定性 / 渲染诊断 ─────────────────────────────────────────
echo "==> [18] Stress test (--stress-test: 40 轮生命周期打击，SDL dummy)"
STRESS="$BUILD_DIR/stresstest_log.txt"
if run_headless --stress-test "$STRESS"; then ok "压力测试退出码 0（无崩溃/无泄漏）"; else bad "压力测试退出码非 0"; fi

echo "==> [19] Render diagnostics (--screen-snap / --pixel-probe / --render-check)"
SNAP="$BUILD_DIR/snaptest_log.txt"
if run_headless --screen-snap "$SNAP"; then ok "screen-snap 退出码 0"; else bad "screen-snap 退出码非 0"; fi
PIXEL="$BUILD_DIR/pixeltest_log.txt"
if run_headless --pixel-probe "$PIXEL"; then ok "pixel-probe 退出码 0"; else bad "pixel-probe 退出码非 0"; fi
RCHK="$BUILD_DIR/rendercheck_log.txt"
if run_headless --render-check "$RCHK"; then ok "render-check 退出码 0"; else bad "render-check 退出码非 0"; fi

# ── 断言级验证 ────────────────────────────────────────────────
echo
echo "==> Verification summary"

echo "-- UI Framework --"
grep -q "pass=54 fail=0" "$UIFW" \
  && ok "UI 框架 54 项断言全过（无溢出/无遮挡/无动画断裂/无命中区错误）" \
  || bad "UI 框架自测存在失败断言"
! grep -q "\[FAIL\]" "$UIDEMO" \
  && ok "UI 框架渲染层 4 屏断言全过（ArcList/Grid/Liquid Glass/anim/物理滚动）" \
  || bad "UI 框架渲染层 demo 存在失败断言"

echo "-- Launcher / UI-JS --"
grep -q "pass=40 fail=0" "$LAUNCHER" \
  && ok "Launcher 迁移 40 项断言全过（4 屏 Home item 在 safe 区、状态栏无溢出）" \
  || bad "Launcher 迁移自测存在失败断言"
grep -q "pass=24 fail=0" "$UIJS" \
  && ok "UI-JS 桥接 24 项断言全过（cos.ui.* 4 屏 / onSelect 同步回调）" \
  || bad "UI-JS 桥接自测存在失败断言"

echo "-- Shell framework / IME --"
grep -q "Loaded 8 installed apps" "$LOG" \
  && ok "开机加载 8 个内置 app（com.cantomk6.*）" \
  || bad "内置 app 加载数量异常"
grep -qE "\[PluginMgr\] scan done: scanned=[1-9]" "$LOG" \
  && ok "Plugin Manager 开机扫描模拟 SD 发现 .eapk 包" \
  || bad "Plugin Manager 开机扫描未发现 SD 包"
grep -q "plugin scan: scanned=[1-9] installed=[1-9]" "$LOG" \
  && ok "plugin scan --force 从 SD 注册/安装包" \
  || bad "plugin scan --force 未从 SD 安装包"
grep -q "proxy.port=1080" "$LOG" \
  && ok "proxy.port 无浮点（整数 1080）" \
  || bad "proxy.port 显示浮点"
grep -q "Failed to open image" "$LOG" \
  && bad "存在 Failed-to-open 图片资源" \
  || ok "开机 logo/PNG 全部打开，无 Failed-to-open"
grep -q "ime 'ni':" "$LOG" && grep -q "你" "$LOG" \
  && ok "IME 拼音转中文（ime ni -> 你）" \
  || bad "IME 拼音转中文失败"

echo "-- Keyboard widget --"
grep -q "\[EN\] textarea: 'hello'" "$KLOG" \
  && ok "英文模式插入字母 hello" \
  || bad "英文模式未正确插入"
grep -q "\[ZH\] textarea: '你好'" "$KLOG" \
  && ok "中文模式拼音 nihao 提交候选 你好" \
  || bad "中文模式未正确提交候选"

echo "-- Platform bridge --"
grep -q "pass=6 fail=0" "$NPROBE" \
  && ok "笔记/画图平台 6 项断言全过（UTF-8 中文往返 / 二进制 NUL+0xFF 保留 / ime.open 不抛）" \
  || bad "笔记/画图平台探针存在失败断言"

echo "-- Built-in apps --"
grep -q "pass=30 fail=0" "$ALARM" \
  && ok "闹钟 30 项断言全过（空态/列表 ON-OFF/向导步进/SAVE/假时钟触发响铃/OFF 关闭/不匹配不响）" \
  || bad "闹钟探针存在失败断言"
grep -q "breach-launch pass=1 fail=0" "$BLAUNCH" \
  && ok "入侵协议启动断言全过（INITIATING 标签 / Boot 隐藏 / Hack 显示 / 对象账本 ≥4）" \
  || bad "入侵协议启动断言存在失败"
grep -q "breach-result pass=1 fail=0" "$BRESULT" \
  && ok "入侵协议 Result 页断言全过（超时->pageResult 显示 / BREACH FAILED / //DAEMON_1 FAILED / TAP TO RETRY）" \
  || bad "入侵协议 Result 页断言存在失败"

echo "-- Gestures / cards --"
grep -q "pass=3 fail=0" "$WFG" \
  && ok "表盘 catcher 全屏 CLICKABLE（中央区滑动由 PRESS/RELEASE 接管）" \
  || bad "表盘 catcher 属性不符"
grep -q "pass=3 fail=0" "$HGSIM" \
  && ok "主页左/右/上滑动路由全过（PRESS/RELEASE）" \
  || bad "主页滑动路由存在失败断言"
grep -q "pass=16 fail=0" "$HGINDEV" \
  && ok "主页真实事件路径 16 项断言全过（lv_indev_read 注入）" \
  || bad "主页真实事件路径存在失败断言"
grep -q "pass=10 fail=0" "$CPS" \
  && ok "小卡片页 10 项断言全过（容器/种子卡/header/chrome 注册/show+hide）" \
  || bad "小卡片页存在失败断言"
grep -q "pass=9 fail=0" "$CPSAPI" \
  && ok "小卡片 App 注册接口 9 项断言全过（add/unregister/dedup）" \
  || bad "小卡片 App 注册接口存在失败断言"

echo
echo "提示：交互式手动测试 -> cd simulator/build && $EXE（需 GUI/桌面环境弹 SDL 窗口）"
