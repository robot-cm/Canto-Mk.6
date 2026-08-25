这是一个只读分析任务。先定位这 12 个 App 的源码分布（C 与 JS 分开找）：
C App 只有 files/flash_light/gallery/settings/test；JS App 有 alarm/album/breach/clock/draw/globaltime/notes/pmdemo/timer。Calculator、Calendar、Stopwatch、Texthub 还没定位。继续搜索：
搜索结果过宽。精确查找这四个 App 的清单与注册信息：
apps/ 目录只有 9 个 App（无 calculator/calendar/stopwatch/texthub）。确认打包清单与 App 注册信息，排除"已实现但不在 apps/ 目录"的可能：
JS App 全集确认。现在精确搜索 Calculator/Calendar/Stopwatch/Texthub 是否存在（含大小写与中文变体），并确认内置 C App 注册表：
关键发现：图标资源曾规划过 calculator/calendar/texthub/stopwatch，但 App 源码只有 9 个 JS + 5 个 C。确认图标引用与缺失 App 情况：
图标库匹配只是 RemixIcon 符号表，确认无 Calculator/Calendar/Stopwatch/Texthub 实现。注意 `timer` 的 manifest 描述是"秒表"。现在开始逐个分析已实现的 App。先读 JS App 源码：
Timer（秒表）与 Album（探针）已分析。继续读 Alarm 与 Notes：
Alarm、Notes 分析完毕。继续读 Breach（赛博朋克小游戏）与 C 版 Flash 灯：
Breach 功能完整（含 autotest）。继续读 GlobalTime（JS 大文件）与 Flash 灯（C）：
GlobalTime 分析完毕（3D 地球仪，池化防闪退，成熟度高）。最后读两个 C App：
Flash Light C 分析完毕。Settings 没有 C LSP provider，用文本方式看它的功能结构：
Settings 功能页很多，确认完整的功能页清单：
分析完成。所有源码已读，以下为只读评估报告。

## 总览：12 个 App 中 8 个已实现，4 个未实现

| App | 类型 | 状态 | 成熟度 |
|---|---|---|---|
| Alarm | JS | ✅ 已实现 | 合格（有局限） |
| Album | JS | ✅ 已实现 | 不合格（仅探针） |
| Breach | JS | ✅ 已实现 | 优秀 |
| Globaltime | JS | ✅ 已实现 | 优秀 |
| Timer | JS | ✅ 已实现 | 优秀 |
| Notes | JS | ✅ 已实现 | 部分合格 |
| Settings | C | ✅ 已实现 | 优秀 |
| Flash | C | ✅ 已实现 | 优秀 |
| Calculator | — | ❌ 未实现 | 仅有规划图标（icon/calculator.webp） |
| Calendar | — | ❌ 未实现 | 仅有规划图标 |
| Stopwatch | — | ❌ 未实现 | 独立 App 不存在；秒表功能由 Timer 覆盖 |
| Texthub | — | ❌ 未实现 | 仅有规划图标 |

JS App 全集为 `apps/` 下 9 个（alarm/album/breach/clock/draw/globaltime/notes/pmdemo/timer）；C App 为 `src/apps/` 下 5 个（files/flash_light/gallery/settings/test）。

---

## 各 App 功能逻辑分析

### 1. Alarm（JS，337 行）— 合格
- **UI**：状态行（待命/已设定）、实时时钟 HH:MM:SS + 日期（100ms tick）；时/分选择器（± 胶囊按钮，24h/60m 回绕）。
- **逻辑**：三态状态机——待命 ⇄ 已设定。设定写 `eos.config`（`alm_h`/`alm_m`/`alm_on`），取消回读，开关翻转。
- **响铃**：时间匹配 → 全屏覆盖层 500ms 闪烁 + **60s 自动停止** + 停止按钮；tick 里每秒检测触发。
- **局限**：单闹钟、无重复/工作日/响铃时长配置；响铃只是 UI 覆盖层，**无任何音频播放调用**（板载蜂鸣器/喇叭未接入）。作为"闹钟"合格，但功能密度低。

### 2. Album（JS，112 行）— 不合格（是探针不是相册）
- 声明为"P1 真窗口探针"：验证 PNG/JPG 解码、FM 文件管理器跳转、`album_pick` 回传轮询。
- **UI**：160×160 图片区 + 三个胶囊按钮（PNG / JPG / FM）。点击分别加载 `/sdcard/ALBUM/0.png`、`/sdcard/ALBUM/test.jpg`、打开文件管理器选图；1s 轮询 `eos.config.getStr("album_pick")` 拿 FM 回传路径，>300KB 护栏跳过。
- **性质**：这是图像解码管线/文件管理器联调的**验证工具**，不是缩略图网格、翻页、删除等正式相册功能。按"相册 App"标准不合格。

### 3. Breach（JS，812 行 + core.js）— 优秀
- **玩法**：赛博朋克 2077 黑客小游戏移植。三页结构：Boot 打字机动画 → Hack 页 → Result overlay。
- **Hack 逻辑**：5×5 矩阵（55/BD/1C/E9/7A 五码）、5 缓冲槽、3 条目标序列（2-4 长度）、**60s 倒计时**、行列交替选择、扫描线高亮、`isSubsequence` 子序列匹配；`mulberry32` 种子随机 + 构造式保证矩阵可解。
- **三种结束路径**：缓冲槽满 / 超时 / 全解，分别进 Result（BREACH SUCCESS/FAILED + SOLVED n/3 + RETRY + Glitch 故障特效）。
- **工程化**：所有 timer 回调 try/catch 隔离（`safeTimer`）、内置 `autotest` 自动连玩 N 局（胜/败/超时全覆盖）、audit 自检（对象账本、布局盒相交断言、矩阵文本审计）。质量很高。

### 4. Globaltime（JS，约 1200 行）— 优秀
- **渲染**：3D 地球正交投影 + 聚焦切面投影，5 城市（香港/东京/北京/巴黎/纽约）；每城市独立 DOTS 数据（全球 400 点加密 + 各国轮廓点集）。
- **状态机**：FOCUS → ROTATE → IDLE（6s 无操作自动旋转）；缩放动画 easeOutCubic、旋转 easeInOut、5 组呼吸动画常驻、声纳特效、Glitch、HUD（城市 + UTC 偏移时间）。
- **防闪退**：点池 400 / 声纳盘池 3 全部复用、**运行期零新建对象**、账本自查、自验表（弦宽约束）。
- 典型的高强度嵌入式 JS，工程严谨度顶级。

### 5. Timer（JS，420 行）— 优秀（秒表）
- **功能**：开始 / 暂停 / **计次** / 重置；HH:MM:SS.X 显示（100ms tick）。
- **持久化**：后台续计时——`eos.config` 存基准时间戳，重进按时间差补算（秒表可跨 app 存活）；计次也持久化（3 行显示）。
- **动画**：状态点呼吸、按钮按压、计次行 fadeIn、重置闪烁；待命时计次/重置按钮淡出。
- 它就是"Stopwatch"的完整实现。

### 6. Notes（JS，327 行）— 部分合格
- 自带圆屏系统键盘（字母页 + 123/符号页切换）、文本卡 + 光标 + 占位符、按压反馈（PRESSED/RELEASED 配对）。
- 完成 → 存 `eos.config` 草稿（`notes.draft`）+ "已存"状态闪示；取消 → 回滚 baseline；重进恢复草稿。
- **关键不符**：manifest 声称"保存/读取 `/sdcard/Notes/*.txt`"，但源码**只用了 config 存草稿，没有任何 `eos.fs` 文件读写**。按 manifest 标准未达标（无多笔记列表、无 SD 文件落盘）。

### 7. Settings（C，1150+ 行）— 优秀
功能页非常完整：
- **蓝牙**：开关
- **WiFi**：开关、AP 扫描列表（SSID/锁标/信号 dBm）、点击弹键盘输密码连接、连接状态显示
- **SOCKS5**：开关 + server/port/username/password 四项输入
- **显示**：亮度滑块（-/+ 微调）、AOD 开关、抬腕唤醒、唤醒时长单选（15s/70s）
- **通知**、**声音与触感**（静音、音量滑块、触感强度 Off/Normal/Intense）
- **Apps 管理**：卸载、清数据、应用详情（appid/author/version/SDK/描述）
- 另有权限管理、锁屏密码（SHA256 + 数字键盘）、语言、测试入口等（include 佐证）。

### 8. Flash（C，1158 行）— 优秀
- **双入口**：控制中心 overlay 快捷版（swipe panel 上滑，点击/下滑关闭、亮度拉满）+ 完整 Activity 版（card pager 横滑 2 页：发光页 / 48 色块色板，8 色相 × 6 亮度）。
- **HSV 色相环**：PSRAM ARGB8888 一次性渲染 164×164、360° 连续色相 + 径向饱和度、中心白色；PRESsed/PRESSING/RELEASED 实时选色，松手保存。
- **持久化**：光色存 `/flash/color.txt`，重开恢复。
- **细节**：沉浸模式切换、手势误判过滤（tap 位移阈值）、UAF 延迟删除、缓冲释放保护。
- 注：这是"彩光手电筒"（全屏发光 + 调色），**未使用硬件闪光灯**（板载无闪光灯 IC，合理）。

---

## 结论

- **合格且高质量（6 个）**：Breach、Globaltime、Timer、Settings、Flash，以及单闹钟场景够用的 Alarm。
- **部分合格（1 个）**：Notes —— 草稿可用，但 SD 多笔记文件保存与 manifest 声称不符。
- **不合格（1 个）**：Album —— 实质是解码/FM 联调探针，无正式相册功能。
- **未实现（4 个）**：Calculator、Calendar、Stopwatch（功能并入 Timer）、Texthub —— 仅有图标资源规划，无任何源码。

如果你的目标是"12 个 App 全部可用"，缺口是：4 个完全未实现（Calculator/Calendar/Texthub + 独立 Stopwatch 可选），1 个需补正式相册功能（Album），1 个需补 SD 文件保存（Notes）。需要我后续按优先级实现哪些，可以告诉我。