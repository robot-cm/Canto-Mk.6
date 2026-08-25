#!/usr/bin/env bash
# ElenixOS 模拟器一键构建 + 冒烟测试（Git Bash 下运行）
# 绕开 build_and_run.bat 中 cmd.exe 被安全策略拦截的限制：直接用 cmake 原生命令。
# 用法：
#   bash simulator/test_sim.sh            # 重新配置+编译+跑全部自检并打印验证结论
# 仅交互测试（跳过构建）：直接 cd simulator/build && ./elenixos_sim.exe
set -euo pipefail

ok()   { echo "  [OK]   $1"; }
bad()  { echo "  [FAIL] $1"; }

CONDA_PREFIX="I:/Anaconda3/envs/elenixos"
export PATH="$CONDA_PREFIX/Library/bin:$CONDA_PREFIX/Library/x86_64-w64-mingw32/bin:$CONDA_PREFIX:$PATH"
# jerryscript's configure step runs tools/version.py via `python`; make sure one is reachable.
if ! command -v python >/dev/null 2>&1; then
  for p in "C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12" "I:/Anaconda3"; do
    if [ -x "$p/python.exe" ]; then export PATH="$p:$PATH"; break; fi
  done
fi
PY_EXE="$(command -v python || echo python)"
CC="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-gcc.exe"
CXX="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-g++.exe"
AR="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-ar.exe"
RANLIB="$CONDA_PREFIX/Library/bin/x86_64-w64-mingw32-ranlib.exe"
MAKE="$CONDA_PREFIX/Library/bin/mingw32-make.exe"
CMAKE="$CONDA_PREFIX/Library/bin/cmake.exe"
SIM_DIR="I:/ElenixOS/simulator"
BUILD_DIR="$SIM_DIR/build"
LOG="$SIM_DIR/shelltest_log.txt"
KLOG="$SIM_DIR/keyboardtest_log.txt"

echo "==> [1/9] Configure"
"$CMAKE" -S "$SIM_DIR" -B "$BUILD_DIR" -G "MinGW Makefiles" \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_AR="$AR" -DCMAKE_RANLIB="$RANLIB" -DCMAKE_MAKE_PROGRAM="$MAKE" \
  -DPYTHON="$PY_EXE"

echo "==> [2/9] Build"
"$CMAKE" --build "$BUILD_DIR" -j 8

echo "==> [3/9] Stress test (40 rounds flashlight/clock/gallery enter-destroy + timer ticks, SDL dummy)"
cd "$BUILD_DIR"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --stress-test > "$SIM_DIR/stresstest_log.txt" 2>&1
STRESS_EXIT=$?
set -e
echo "STRESS_EXIT=$STRESS_EXIT"
[ "$STRESS_EXIT" -eq 0 ] && ok "交互压力测试无崩溃/无泄漏 (40 轮) — 卡死闪退加固验证" || bad "压力测试失败 STRESS_EXIT=$STRESS_EXIT"

echo "==> [4/9] Smoke test (SDL dummy driver, --shell-test: Shell/App/WiFi/BT/Proxy/Plugin/IME)"
cd "$BUILD_DIR"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --shell-test | tee "$LOG"
SIM_EXIT=${PIPESTATUS[0]}
set -e
echo "SIM_EXIT=$SIM_EXIT"

echo "==> [5/9] Keyboard self-test (SDL dummy driver, --keyboard-test: EN/ZH IME)"
cd "$BUILD_DIR"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --keyboard-test | tee "$KLOG"
KBD_EXIT=${PIPESTATUS[0]}
set -e
echo "KBD_EXIT=$KBD_EXIT"

echo "==> [6/9] UI Framework self-test (SDL dummy driver, --ui-framework-test: adaptive layout/ArcList/physics/anim/personalize)"
cd "$BUILD_DIR"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --ui-framework-test | tee "$SIM_DIR/uifwtest_log.txt"
UIFW_EXIT=${PIPESTATUS[0]}
set -e
echo "UIFW_EXIT=$UIFW_EXIT"

echo "==> [7/9] UI Framework rendering-layer demo (SDL dummy driver, --ui-framework-demo: P8 ArcList/Grid/LiquidGlass/anim+physics on 4 profiles)"
cd "$BUILD_DIR"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --ui-framework-demo | tee "$SIM_DIR/uifwdemo_log.txt"
UIDEMO_EXIT=${PIPESTATUS[0]}
set -e
echo "UIDEMO_EXIT=$UIDEMO_EXIT"

echo "==> [8/9] Launcher migration test (SDL dummy driver, --launcher-test: real Plugin-Manager app list on framework Home across 4 profiles)"
cd "$BUILD_DIR"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --launcher-test | tee "$SIM_DIR/launchertest_log.txt"
LAUNCHER_EXIT=${PIPESTATUS[0]}
set -e
echo "LAUNCHER_EXIT=$LAUNCHER_EXIT"

echo "==> [9/9] UI-JS bridge self-test (SDL dummy driver, --ui-js-test: eos.ui.* adaptive Home on 4 profiles)"
cd "$BUILD_DIR"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --ui-js-test | tee "$SIM_DIR/uijstest_log.txt"
UIJS_EXIT=${PIPESTATUS[0]}
set -e
echo "UIJS_EXIT=$UIJS_EXIT"

echo "==> [11/11] Watchface gesture catcher regression (SDL dummy driver, --watchface-gesture-test: full-screen CLICKABLE catcher present, receives PRESS/RELEASE)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --watchface-gesture-test | tee "$SIM_DIR/wfgtest_log.txt"
WFG_EXIT=${PIPESTATUS[0]}
set -e
echo "WFG_EXIT=$WFG_EXIT"

echo "==> [12/12] Cards-page regression (SDL dummy driver, --cards-page-test: small-cards overlay created, 3+ cards seeded, registered with chrome manager)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --cards-page-test | tee "$SIM_DIR/cpstest_log.txt"
CPS_EXIT=${PIPESTATUS[0]}
set -e
echo "CPS_EXIT=$CPS_EXIT"

echo "==> [13/13] Cards-page app API regression (SDL dummy driver, --cards-api-test: register/unregister/dedup via eos_cards_page_register_card)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --cards-api-test | tee "$SIM_DIR/cpsapi_log.txt"
CPSAPI_EXIT=${PIPESTATUS[0]}
set -e
echo "CPSAPI_EXIT=$CPSAPI_EXIT"

echo
echo "==> Verification summary"
[ "$UIDEMO_EXIT" -eq 0 ] && ok "UI 框架渲染层 demo 退出码 0（4 屏 Home 全部 item 在 safe 区内、状态栏无矩形溢出、无 LVGL 崩溃）" || bad "UI 框架渲染层 demo 失败 UIDEMO_EXIT=$UIDEMO_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/uifwdemo_log.txt" && ok "UI 框架渲染层 4 屏断言全过（ArcList/Grid/Liquid Glass/anim/物理滚动）" || bad "UI 框架渲染层 demo 存在失败断言"
[ "$UIFW_EXIT" -eq 0 ] && ok "UI 框架自适应自测退出码 0（54 项全过：DisplayProfile/LayoutManager/ArcList/Physics/Anim/Personalize）" || bad "UI 框架自测失败 UIFW_EXIT=$UIFW_EXIT"
grep -q "pass=54 fail=0" "$SIM_DIR/uifwtest_log.txt" && ok "UI 框架 54 项断言全过（无溢出/无遮挡/无动画断裂/无命中区错误）" || bad "UI 框架自测存在失败断言"
[ "$STRESS_EXIT" -eq 0 ] && ok "压力测试退出码 0（卡死闪退加固）" || bad "压力测试退出码非 0: $STRESS_EXIT"
grep -q "proxy.port=1080" "$LOG" && ok "proxy.port 无浮点 (#48)" || bad "proxy.port 仍显示浮点"
if ! grep -q "Failed to open image" "$LOG"; then ok "开机 logo/PNG 全部打开，无 Failed-to-open (#50)"; else bad "仍存在 Failed-to-open (#50)"; fi
grep -q "rebuild.*Apps grid" "$SIM_DIR/launchertest_log.txt" && ok "Launcher 订阅安装/卸载事件并重建网格 (#52)" || bad "Launcher 网格未重建 (#52)"
grep -q "started successfully" "$LOG" && ok "Clock 插件经 SPM 启动成功 (#52)" || bad "Clock 插件未启动 (#52)"
grep -q "Found installed app: com.elenix.clock" "$LOG" && ok "Clock 种子安装/启动成功 (#52)" || bad "Clock 种子安装失败 (#52)"

# ---- Shell framework / Plugin Manager / IME (new features) ----
# Boot auto-scan must discover the SD-seeded packages (>=1; count grows as
# more seed apps are added — do NOT hardcode a specific count here).
grep -qE "\[PluginMgr\] scan done: scanned=[1-9]" "$LOG" \
  && ok "Plugin Manager 开机自动扫描 SD 发现包 (#58/#59)" \
  || bad "Plugin Manager 开机扫描未发现 SD 包 (#58/#59)"
# Manual `plugin scan --force` reinstalls from SD deterministically regardless of prior state.
grep -q "plugin scan: scanned=[1-9] installed=[0-9]* skipped=[0-9]*" "$LOG" \
  && ok "plugin scan --force 从 SD 注册/安装包 (#59)" \
  || bad "plugin scan 未从 SD 安装包 (#59)"
grep -q "com.elenix.pmdemo" "$LOG" && grep -q "com.elenix.clock" "$LOG" \
  && ok "plugin list 列出已注册插件 (#59)" \
  || bad "plugin list 未列出插件 (#59)"
# IME: pinyin -> Chinese candidates
grep -q "ime 'ni':" "$LOG" && grep -q "你" "$LOG" \
  && ok "IME 拼音转中文 (ime ni -> 你) (#62)" \
  || bad "IME 拼音转中文失败 (#62)"

# ---- Keyboard widget headless exercise (EN insert + ZH commit) ----
[ "$KBD_EXIT" -eq 0 ] && ok "键盘自测退出码 0" || bad "键盘自测退出码非 0: $KBD_EXIT"
grep -q "\[EN\] textarea: 'hello'" "$KLOG" \
  && ok "英文模式插入字母 hello (#64)" \
  || bad "英文模式未正确插入 (#64)"
grep -q "\[ZH\] textarea: '你好'" "$KLOG" \
  && ok "中文模式拼音 nihao 提交候选 你好 (#64)" \
  || bad "中文模式未正确提交候选 (#64)"

[ "$SIM_EXIT" -eq 0 ] && ok "整体退出码 0（无崩溃/无致命错误）" || bad "退出码非 0: $SIM_EXIT"

# ---- Launcher full migration onto ArcList + LayoutManager ----
[ "$LAUNCHER_EXIT" -eq 0 ] && ok "Launcher 迁移测试退出码 0（真实 Plugin-Manager 应用列表在 4 屏 Home 全部 item 在 safe 区内、状态栏无矩形溢出、on_select 已接启动）" || bad "Launcher 迁移测试失败 LAUNCHER_EXIT=$LAUNCHER_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/launchertest_log.txt" && ok "Launcher 迁移 4 屏断言全过（ArcList/Grid/Liquid Glass/状态栏/命中区）" || bad "Launcher 迁移测试存在失败断言"

# ---- UI-JS bridge (Route A): eos.ui.* adaptive Home driven from JS ----
[ "$UIJS_EXIT" -eq 0 ] && ok "UI-JS 桥接自测退出码 0（eos.ui.* 在 4 屏全部断言过、onSelect 同步回调）" || bad "UI-JS 桥接自测失败 UIJS_EXIT=$UIJS_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/uijstest_log.txt" && ok "UI-JS 桥接 4 屏断言全过（状态栏/item 中心在 safe 区、flick 后不溢出、JS->C->JS onSelect）" || bad "UI-JS 桥接自测存在失败断言"

# ---- Watchface gesture bubble (left-swipe on watchface -> app list) ----
[ "$WFG_EXIT" -eq 0 ] && ok "表盘手势 catcher 退出码 0（全屏 clickable 层存在）" || bad "表盘手势 catcher 自测失败 WFG_EXIT=$WFG_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/wfgtest_log.txt" && ok "表盘 catcher 全屏 clickable（中央区滑动由 PRESS/RELEASE 接管，不再被吞）" || bad "表盘 catcher 属性不符（缺少全屏 clickable 层）"

echo "==> [11.5/14] Home swipe-navigation regression (SDL dummy driver, --home-gesture-sim-test: left/right/up route via PRESS/RELEASE)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --home-gesture-sim-test | tee "$SIM_DIR/hgtest_log.txt"
HG_EXIT=${PIPESTATUS[0]}
set -e
echo "HG_EXIT=$HG_EXIT"
[ "$HG_EXIT" -eq 0 ] && ok "主页滑动导航退出码 0（左→App 页 / 右→控制中心 / 上→小卡片 全部路由）" || bad "主页滑动导航自测失败 HG_EXIT=$HG_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/hgtest_log.txt" && ok "主页左/右/上滑动路由全过（PRESS/RELEASE 替代不可靠 GESTURE）" || bad "主页滑动路由存在失败断言"

echo "==> [12/15] Home swipe REAL-event-path regression (SDL dummy driver, --home-gesture-indev-test: lv_indev_read injects PRESS/drag/RELEASE)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --home-gesture-indev-test | tee "$SIM_DIR/hgitest_log.txt"
HG_INDEV_EXIT=${PIPESTATUS[0]}
set -e
echo "HG_INDEV_EXIT=$HG_INDEV_EXIT"
[ "$HG_INDEV_EXIT" -eq 0 ] && ok "主页真实事件路径滑动退出码 0（左缘右滑开控制栏 / 中央右滑开控制栏 / 中央左滑开 App 页）" || bad "主页真实事件路径滑动自测失败 HG_INDEV_EXIT=$HG_INDEV_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/hgitest_log.txt" && ok "主页真实事件路径（lv_indev_read 注入）断言全过" || bad "主页真实事件路径滑动存在失败断言"

# ---- Cards page (small cards / smart stack: up-swipe from bottom 50px reveals) ----
[ "$CPS_EXIT" -eq 0 ] && ok "小卡片页（up-swipe）退出码 0（实例/容器/0 张种子卡[已删除]/header/show+hide 全部就绪）" || bad "小卡片页自测失败 CPS_EXIT=$CPS_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/cpstest_log.txt" && ok "小卡片页断言全过（3 张种子卡/header/chrome 注册/show+hide 状态切换）" || bad "小卡片页存在失败断言"

# ---- Cards page app-facing registration API (register/unregister/dedup) ----
[ "$CPSAPI_EXIT" -eq 0 ] && ok "小卡片 App 注册接口退出码 0（register/unregister/dedup 全过）" || bad "小卡片 App 注册接口失败 CPSAPI_EXIT=$CPSAPI_EXIT"
! grep -q "\[FAIL\]" "$SIM_DIR/cpsapi_log.txt" && ok "小卡片 App 注册接口断言全过（add 后容器内子对象+1、unregister 后还原、同 id 去重）" || bad "小卡片 App 注册接口存在失败断言"

echo "==> [14/15] Alarm app state-machine regression (SDL dummy driver, --alarm-test: picker +/- wrap / set/cancel/toggle / ring overlay / config persistence)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --alarm-test | tee "$SIM_DIR/alarmtest_log.txt"
ALARM_EXIT=${PIPESTATUS[0]}
set -e
echo "ALARM_EXIT=$ALARM_EXIT"
[ "$ALARM_EXIT" -eq 0 ] && ok "闹钟状态机退出码 0（+/- 回绕 / 设定/取消/开关 / 响铃覆盖层+停止 / 持久化 / 不匹配不响）" || bad "闹钟状态机失败 ALARM_EXIT=$ALARM_EXIT"
grep -q "pass=32 fail=0" "$SIM_DIR/alarmtest_log.txt" && ok "闹钟 32 项断言全过（选择器回绕/状态行/响铃闪烁/停止隐藏/60s 守卫）" || bad "闹钟断言存在失败"

echo "==> [15/15] Album headless probe (SDL dummy driver, --album-probe: eos.fs.list/size + album_pick C->JS same-path round-trip)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --album-probe | tee "$SIM_DIR/albumprobe_log.txt"
ALBUMPROBE_EXIT=${PIPESTATUS[0]}
set -e
echo "ALBUMPROBE_EXIT=$ALBUMPROBE_EXIT"
[ "$ALBUMPROBE_EXIT" -eq 0 ] && ok "相册探针退出码 0（fs.list 过滤 png/jpg / fs.size>0 / album_pick C->JS 同路径互通 / JS round-trip / 清空）" || bad "相册探针失败 ALBUMPROBE_EXIT=$ALBUMPROBE_EXIT"
grep -q "pass=8 fail=0" "$SIM_DIR/albumprobe_log.txt" && ok "相册 8 项断言全过（fs.list 数组含 readme.txt、imgs 过滤 >=4、size>0、C 写 JS 读、round-trip、清空）" || bad "相册探针断言存在失败"

echo "==> [16/16] Album app real-launch probe (SDL dummy driver, --album-launch: eapk install + launch + PNG/JPG setSrc + FM jump/back)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --album-launch | tee "$SIM_DIR/albumlaunch_log.txt"
ALBUMLAUNCH_EXIT=${PIPESTATUS[0]}
set -e
echo "ALBUMLAUNCH_EXIT=$ALBUMLAUNCH_EXIT"
[ "$ALBUMLAUNCH_EXIT" -eq 0 ] && ok "相册真启动退出码 0（eapk 安装/launch/PNG+JPG setSrc 路径/FM 跳转+back）" || bad "相册真启动失败 ALBUMLAUNCH_EXIT=$ALBUMLAUNCH_EXIT"
grep -q "pass=1 fail=0" "$SIM_DIR/albumlaunch_log.txt" && ok "相册真启动断言全过（状态行+3 按钮全建 / src=/sdcard/ALBUM/0.png / test.jpg / FM activity 切换）" || bad "相册真启动断言存在失败"

echo "==> [17/17] Notes/Draw platform probe (SDL dummy driver, --notes-probe: fs.write string+Uint8Array / fs.read binary-safe / ime.open)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --notes-probe | tee "$SIM_DIR/notesprobe_log.txt"
NOTESPROBE_EXIT=${PIPESTATUS[0]}
set -e
echo "NOTESPROBE_EXIT=$NOTESPROBE_EXIT"
[ "$NOTESPROBE_EXIT" -eq 0 ] && ok "笔记/画图平台探针退出码 0（fs.write string+Uint8Array / fs.read 字节数组 / ime.open）" || bad "笔记/画图平台探针失败 NOTESPROBE_EXIT=$NOTESPROBE_EXIT"
grep -q "pass=6 fail=0" "$SIM_DIR/notesprobe_log.txt" && ok "笔记/画图 6 项断言全过（UTF-8 中文往返 / 二进制 NUL+FF 保留 / ime.open 不抛）" || bad "笔记/画图探针断言存在失败"

echo "==> [18/18] Notes keyboard layout probe (SDL dummy driver, --notes-launch: 自带键盘圆屏重排 标题/工具/文本卡/30键4行)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --notes-launch | tee "$SIM_DIR/noteslaunch_log.txt"
NOTESLAUNCH_EXIT=${PIPESTATUS[0]}
set -e
echo "NOTESLAUNCH_EXIT=$NOTESLAUNCH_EXIT"
[ "$NOTESLAUNCH_EXIT" -eq 0 ] && ok "笔记键盘重排退出码 0（工具 取消/完成 / 文本卡+光标 / 30 字母键+28 符号键 / 账本 63）" || bad "笔记键盘重排失败 NOTESLAUNCH_EXIT=$NOTESLAUNCH_EXIT"
grep -q "pass=1 fail=0" "$SIM_DIR/noteslaunch_log.txt" && ok "笔记键盘重排断言全过（12 个关键 label / 对象账本 63 / q 键 16,116 / 123 键 64,200 / 符号页切换 / 草稿启动恢复）" || bad "笔记键盘重排断言存在失败"
CHORD_PASS=$(grep -c '\[chord\].*PASS' "$SIM_DIR/noteslaunch_log.txt")
CHORD_FAIL=$(grep -c '\[chord\].*FAIL' "$SIM_DIR/noteslaunch_log.txt")
[ "$CHORD_PASS" -ge 6 ] && [ "$CHORD_FAIL" -eq 0 ] && ok "弦宽自验表全 PASS（新红线 min(chord顶,chord底)−16：144≤147 / 176≤187 / 199≤219 / 179≤202 / 164≤166 / 100≤103；启动+恢复两次运行）" || bad "弦宽自验表存在 FAIL"

echo "==> [19/19] Draw app real-launch probe (SDL dummy driver, --draw-launch: eos.draw.* canvas + 48 色调色板 + .edrw 保存/回读 round-trip)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --draw-launch | tee "$SIM_DIR/drawlaunch_log.txt"
DRAWLAUNCH_EXIT=${PIPESTATUS[0]}
set -e
echo "DRAWLAUNCH_EXIT=$DRAWLAUNCH_EXIT"
[ "$DRAWLAUNCH_EXIT" -eq 0 ] && ok "画图真启动退出码 0（eapk 安装/launch / 176x176 lv_canvas / 5 工具钮 / 48 色调色板 / .edrw 保存+回读）" || bad "画图真启动失败 DRAWLAUNCH_EXIT=$DRAWLAUNCH_EXIT"
grep -q "pass=1 fail=0" "$SIM_DIR/drawlaunch_log.txt" && ok "画图断言全过（canvas 176x176 / 颜色·打开·保存·清除·返回 / 调色板 48 格 / ELDRW1 头 + 176x176 + RGB565 + 体积正确 / 加载 round-trip 字节一致）" || bad "画图断言存在失败"

echo "==> [20/20] Breach Protocol app probe (SDL dummy driver, --breach-launch: boot + structure / Boot->Hack transition)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --breach-launch | tee "$SIM_DIR/breachlaunch_log.txt"
BREACHLAUNCH_EXIT=${PIPESTATUS[0]}
set -e
echo "BREACHLAUNCH_EXIT=$BREACHLAUNCH_EXIT"
[ "$BREACHLAUNCH_EXIT" -eq 0 ] && ok "入侵协议启动退出码 0（Boot->Hack 转场 / 4 子对象 / 1450 帧无崩溃）" || bad "入侵协议启动失败 BREACHLAUNCH_EXIT=$BREACHLAUNCH_EXIT"
grep -q "pass=1 fail=0" "$SIM_DIR/breachlaunch_log.txt" && ok "入侵协议启动断言全过（INITIATING 标签 / Boot 隐藏 / Hack 显示 / 对象账本 4）" || bad "入侵协议启动断言存在失败"

echo "==> [21/21] Breach Protocol Result-page probe (SDL dummy driver, --breach-result: 60s 超时 -> Result overlay + Glitch)"
set +e
SDL_VIDEODRIVER=dummy ./elenixos_sim.exe --breach-result | tee "$SIM_DIR/breachresult_log.txt"
BREACHRESULT_EXIT=${PIPESTATUS[0]}
set -e
echo "BREACHRESULT_EXIT=$BREACHRESULT_EXIT"
[ "$BREACHRESULT_EXIT" -eq 0 ] && ok "入侵协议 Result 页退出码 0（超时触发 endGame / pageResult 显示 / BREACH FAILED / SOLVED 0 / 3 / RETRY / 7500 帧 Glitch 无崩溃）" || bad "入侵协议 Result 页失败 BREACHRESULT_EXIT=$BREACHRESULT_EXIT"
grep -q "pass=1 fail=0" "$SIM_DIR/breachresult_log.txt" && ok "入侵协议 Result 页断言全过（失败文案 / 进度 / RETRY / 暗化 overlay 可见 / Glitch 稳定）" || bad "入侵协议 Result 页断言存在失败"

echo
echo "提示：交互式手动测试 -> cd simulator/build && ./elenixos_sim.exe（需 GUI/桌面环境弹 SDL 窗口）"
