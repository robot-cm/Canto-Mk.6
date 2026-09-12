# Agent 经验总结：ElenixOS 内存泄漏 Bug 排查与修复

> 来源于一次真实的内存泄漏排查（基于 `memlog report every 1s.txt` 与 `eos_mem_auto.c` 源码分析）。
> 目标：沉淀可复用的排查套路与根因结论，避免下次踩同样的坑。

---

## 一、Bug 一句话结论

**分配器不匹配（allocator mismatch / cross-heap free）**：同一块内存「在哪分配就在哪释放」被破坏。
具体表现为 **strdup 等 libc 堆分配的内存，被 ElenixOS 的 `eos_free` 走 eos 分配器释放**，eos 不认识该指针 → 拒绝释放 → 泄漏。

在内存调试语境里也叫 **foreign pointer（外来指针）释放**，对应 `eos_mem_auto.c` 中的日志：
```text
[ERROR] [MemAuto] Free: foreign pointer 0x3c4f80fc magic=0x0002 - ignored(leak)
```

---

## 二、现象与数据（证据链）

### 1. 内存走势（45 条 MemReport，来自真实设备 ESP32-S3）
| 阶段 | internal free | PSRAM free | DMA free |
|---|---|---|---|
| 开机稳态 | 71.5 KB | 7.51 MB | 64 KB |
| 首次打开应用后 | 24.7 KB（-47K） | 7.49 MB | 17 KB |
| 每次打开应用反复 | 跌到 9~12K，回升到 21~23K | 波动下行 | 1~5K |
| 结束时 | 10.3 KB | 7.34 MB（-173K） | 2.8 KB（largest 仅 36B） |

- 内部 RAM 累计掉 **61KB**；DMA 连续块几乎耗尽（仅剩 1~4KB）。
- 每次「打开应用→运行→返回」净漏约 **1~2KB**，内存底线持续下移。

### 2. 核心错误日志特征
```text
[ERROR] [MemAuto] Free: foreign pointer 0xXXXXXX magic=0xYYYY - ignored(leak)
```
- 日志点：`src/port/memory/eos_mem_auto.c` 的 `eos_free_core()` 与 `eos_realloc_core()`。
- 本项目累计触发 **81 次** foreign pointer 释放。

### 3. magic 值分析（关键判定技巧）
被 free 的指针头部 magic **没有一个是 eos 的 magic（`0xE5A0`）**，包括：
- `0x3f3f`：ESP-IDF 释放后的填充值 = **已被 free 过**（double-free 残留）。
- ASCII 数字串（`0x3132…`）：字符串数据覆盖。
- `0x0002`：恰好等于 LVGL 的 `LV_EVENT_CLICKED`(=2) 事件码 → foreign 指针很可能是 `lv_event_dsc_t`。
  - 结合 SNI-Context 大量 `ADD_RESOURCE: type=LV_EVENT_DSC(34)`，说明 LVGL 事件描述符被错误地交给 eos 释放。

**经验**：magic 不等于本分配器魔数 ≈ 100% 判定为「外来指针」，不要对其执行 free，否则会破坏系统堆（tlsf）。

---

## 三、根因

ElenixOS 的自动内存层（`eos_mem_auto.c`）用 **魔数 header** 包裹每块内存（8 字节对齐）：
```c
typedef struct {
    uint16_t magic;  // = EOS_MEM_HEADER_MAGIC (0xE5A0)
    uint8_t  type;   // FAST(DRAM) / LARGE(PSRAM)
    uint8_t  pad;
    size_t   size;
} eos_mem_header_t;
```
- `eos_malloc` / `eos_free` 在前/后 8 字节读写这个 header。
- 但项目里**混用了多个分配器**：
  - `heap_caps`（ESP-IDF，`--wrap malloc`）
  - `jerry`（JS 引擎 context heap）
  - `cJSON`、`LVGL`（用 stdlib `malloc`/`strdup`/`free`）
  - ElenixOS 自己的 `eos_malloc`/`eos_free`
- 当某个模块的字符串用 `strdup`（libc `malloc`）分配，却被另一个模块当成 eos 指针 `eos_free` 时，header 里没有 `0xE5A0`，触发 foreign pointer 防护 → 拒绝并泄漏。

---

## 四、修复方案（已在 `eos_mem_auto.c` 落地）

### 1. free 路径：外来指针防护
```c
void eos_free_core(void *ptr) {
    EOS_CHECK_PTR_RETURN(ptr);
    eos_mem_header_t *hdr = (eos_mem_header_t *)ptr - 1;
    if (hdr->magic != EOS_MEM_HEADER_MAGIC) {
        EOS_LOG_E("Free: foreign pointer %p magic=0x%04x - ignored(leak) caller=%p",
                  ptr, hdr->magic, __builtin_return_address(0));
        // 真实设备打印 backtrace，桌面模拟器逐层 __builtin_return_address
        return;  // 绝不 free 错位地址
    }
    // ... 正常释放
}
```

### 2. realloc 路径：优先信任 magic，而非 type 字节
```c
if (old_hdr->magic != EOS_MEM_HEADER_MAGIC ||
    (old_hdr->type != FAST && old_hdr->type != LARGE)) {
    EOS_LOG_E("Realloc: foreign pointer ... - alloc new, leak old");
    return eos_malloc_core(new_size);  // 分配新块保活，宁可泄漏旧块也不破坏双池
}
```
> 教训：外部块 header 的 type 字节可能**巧合等于 0/1**，所以不能只看 type，必须校验 magic。

### 3. 真实设备增加 backtrace
```c
#if EOS_PLATFORM_ESP32
    esp_backtrace_print(6);
#else
    for (int i = 1; i <= 5; i++) {
        void *ra = __builtin_return_address(i);
        if (!ra) break;
        EOS_LOG_E("  caller[%d]=%p", i, ra);
    }
#endif
```
> 注意：桌面模拟器没有 `heap_caps` 后端，`periodic memory report` 不可用，backtrace 只能靠 `__builtin_return_address` 逐层取。

### 4. DRAM 紧张时的回退策略（避免 NULL 传播）
- `eos_malloc_core` / `eos_malloc_zeroed_core`：DRAM 不足时**回退到 PSRAM 池**，而不是返回 NULL（NULL 向上游 LVGL/SNI 传播会破坏内存，表现为 JerryScript assert fatal code=120）。
- `eos_realloc_core`：in-place 失败时用 alloc+copy+free 兜底，同样不返回 NULL。

---

## 五、可复用排查套路（SOP）

1. **大日志不要硬读**：`memlog report every 1s.txt` 很长，用 `grep` / `.py` 脚本抽取 MemReport 序列，先画内存走势表，定位「哪个阶段开始掉」。
2. **看错误日志模式**：统计 `foreign pointer` 出现次数、magic 分布、caller 地址，归类「是什么类型的外来指针」。
3. **magic 反查类型**：magic 值若命中某个库的常量（如 `LV_EVENT_CLICKED=2`、`0x3f3f` 释放填充值），反推该指针来自哪个分配器。
4. **交叉比对资源登记**：对照 SNI-Context 的 `ADD_RESOURCE` / `RELEASE_RESOURCE` 日志，找「登记了但没按正确路径释放」的资源类型（如 `LV_EVENT_DSC`）。
5. **确认分配/释放是否成对同源**：找到 strdup / `malloc` 的调用点，确认释放点是否走同一分配器。
6. **在分配器边界加防护**：统一通过 magic header 识别本池指针，对外部指针拒绝操作 + 日志 + backtrace，既防崩溃又留痕。
7. **修复时宁漏勿崩**：实在无法安全回收的外来指针，记录泄漏并跳过 free，比破坏双池（tlsf/jerry heap）更可接受。

---

## 六、后续建议（未做，按需）

- 统一项目内存 API：明确「eos 模块只用 `eos_malloc/free`，LVGL/cJSON/jerry 用自己的分配器」，并封装适配层，杜绝混用。
- 给 LV_EVENT_DSC 等 LVGL 资源加 eos 包装，或显式用 `lv_..._remove_event_cb` 正确释放，而非交给 eos。
- 桌面模拟器也实现 `heap_caps` 桩，使 periodic memory report 可对比真实设备。

---

# Agent 经验总结：Canto Mk.6 UI 字号 / 圆屏布局 / JS APP 开发

> 来源于多轮 UI 调整（Alarm / Timer / Calendar / Breach 四款 JS APP + 系统键盘 C 组件）的真实开发。
> 目标：沉淀「240×240 圆屏字号体系、marquee 滚动、z 序/圆界、字体接入」的可复用套路。

---

## 一、字号体系（必读，避免瞎设数字）

### 1. 字号不是任意整数,必须落在 8 档区间
`src/ui/font/eos_font.h` 定义枚举,`src/ui/font/backend/eos_font_c_multi.c` 的 `_select_font()` 按区间映射：
```
size ≥ 30 → jbm_30
26..29   → jbm_26
22..25   → jbm_22
20..21   → jbm_20
18..19   → jbm_18
16..17   → jbm_16
13..15   → jbm_13
 ≤ 12    → jbm_10
```
**结论**：JS APP 里 `setFontSize(17)` 与 `setFontSize(16)` 渲染完全一样（都落 jbm_16）。想明显放大,必须跨档（如 16→18）。

### 2. 中文走 fallback 链,不是每个 jbm 都含汉字
- `jbm_10/13/16/18/20/22/26/30` 只含 ASCII；中文 fallback：
  - jbm_13 → `han_sans_13`（GB2312 一级 3755 字,RLE ~0.54MB flash）
  - jbm_16 → `han_sans_16`（177 常用字,小）
  - jbm_18/20/22 → `han_sans_22`（1MB 全量 GB2312）
  - jbm_26/30 → `eos_font_icon`（只有图标,无汉字！）
- **结论**：纯数字/拉丁文本（时间 `12:34`、序列 `1C 55`、终端 `ALL DAEMONS`）可用任意 jbm 档；含中文的 UI 文本别用 26/30 档,会不显示汉字。
- 字体源全部在 `resources/font/`,已显式接入 `port/esp32s3/main/CMakeLists.txt`（jbm_10/13/16/18/20/22/26/30 + han_sans_13/16/22 + icon）。

### 3. 用户口语「字再大/再小」的实际含义
- 「大一点」通常指**跨一档**（如 18→22）,不是 18→19（19 还是 jbm_18,看不出变化）。
- 反复出现的模式：Alarm 时间 22→26、Timer 时间 18→22、Calendar 日期 10→13、键盘 26→20、16。`setFontSize` 一律给 **档位边界值**（10/13/16/18/20/22/26）最稳。

---

## 二、圆屏布局红线（240×240,圆心 120,120,半径 120）

### 1. 任意 (x,y) 须验证在圆内
距离圆心 `d = sqrt((x-120)² + (y-120)²)`,且对象右/下边界也要满足 `d ≤ 120`。
- 关键行：y=206（底部）时圆界仅 `x ∈ [36, 204]`,左右各只有 36px 余量。
- 目标序列胶囊条 `x44..204`、宽 160 是贴底安全上限。
- **经验**：对象放在 y≥185 时,x 必须收缩到圆界内（如 36~44 起、右缘 ≤204）。

### 2. LVGL 默认裁剪溢出子对象
父对象（如终端窗口 `termBox`、节日条 `festBox`）默认 `OVERFLOW` 裁剪,子对象超出会被切掉。
- 滚动 label 直接挂 `pageHack`（full-screen,无裁剪）才不会被父容器裁。
- 注意父对象内的「底部提示」（如 `TAP TO RETRY`）y 坐标别超出父对象高,否则被裁。

### 3. 圆角边缘的视觉贴边
即使理论上在圆内（d<120）,贴边 0~10px 时电容屏显示会被圆边啃掉。目标序列最初 `TGT_X0=36` 用户反馈「最左序列出屏」,右移 8px 到 `44` 后解决。**结论**：圆屏元素左右各留 ≥8px 安全距。

---

## 三、z 序（覆盖关系）坑

### 1. LVGL 绘制顺序 = 父先于子,同父兄弟按创建顺序
后创建的对象盖在先创建的上面。
- **坑**：想让「状态文本 INSTALLED/FAILED」盖在「序列格子」上,但文本若作为胶囊条 `tb` 的子对象（tb 创建早于格子）,会被后创建的格子盖住。
- **修法**：状态文本改为 `pageHack` 直接子对象,且在格子循环**之后**创建（确保它在 last 兄弟之上）。

### 2. label 默认 CLICKABLE 吃掉父按钮点击
- 按钮内放 label,label 默认 `OBJ_FLAG_CLICKABLE` 会截获触摸事件,父按钮 `EVENT_PRESSED` 不触发。
- **修法**：`label.removeFlag(lv.OBJ_FLAG_CLICKABLE)`（本项目 `EVENT_CLICKED` 损坏,统一用 `EVENT_PRESSED`）。

---

## 四、Marquee 滚动（节日/长文本）

### 1. 错误方案：整字符窗口 + 重设文本
```js
pos++; festLbl.setText(full.substring(pos, pos+win));   // 90ms/tick
```
- 中文 13px → 144px/s 太快,且每 tick 重设文本只有 ~11fps 跳变,观感卡顿。

### 2. 正确方案：像素级整体平移 + 多段回绕
```js
var sep = "     ";
var segW = estW(s) + estW(sep);            // 13px 档:中文/全角 13px,ASCII 7px
festLbl.setWidth(3 * segW);
festLbl.setText(s + sep + s + sep + s);    // 3 段拼接,回绕后窗口仍有内容
var x = 6;
festMarq = new lv.timer(function () {
    x -= 1;                                // 1px/40ms ≈ 25px/s,顺滑
    if (x <= 6 - segW) x += segW;          // 滚完一段回绕,视觉无缝
    festLbl.setPos(x, 1);
}, 40, null);
```
- **要求**：`festLbl` 挂 `pageHack`（无裁剪）,且 `setLongMode(LABEL_LONG_CLIP)` 防换行。
- **用户偏好**：无论长短都滚（不要加「短文本静态」分支,实测会被判「又不滚了」）。

### 3. 终端窗口/状态行换行坑（重要）
- LVGL label 默认 `long_mode = WRAP`,文本超宽会换行,第二行被 label 高度裁掉。
- 现象：`resProgress` 显示 `ALL DAEMONS UPLOADED`,13px+字距2 ≈188px 超 184px → 换行,只看到 `ALL DAEMONS`,`UPLOADED` 被裁。
- **修法**：`setLongMode(LABEL_LONG_CLIP)` + 减小字距 + 必要时上移 y 坐标。

---

## 五、系统键盘字号（C 组件）

`src/ui/widgets/keyboard/eos_round_keyboard.c`：
- 字母/数字/符号：`EOS_FONT_SIZE_LARGE_MINUS`(22) → `EOS_FONT_SIZE_SMALL`(20)
- 中文候选 / 模式键：`EOS_FONT_SIZE_TALL`(18) → `EOS_FONT_SIZE_EXTRA_SMALL`(16)
- 改 C 组件后需重新编译固件（不是 JS,不能热更）。

---

## 六、验证清单（每次 UI 改动后）

1. `node --check apps/<app>/main.js`（JS 语法）。
2. `read_lints` 看 JS/C 报错。
3. 圆屏元素：手算 `(x-120)²+(y-120)² ≤ 120²` 且边界也满足。
4. 字号值是否落在 8 档区间;含中文则避开 jbm_26/30。
5. 覆盖文本是否创建在正确 z 序（挂在无裁剪父对象、晚于被盖对象创建）。
6. 长文本 label 是否 `setLongMode(CLIP)`（防换行裁切）。
7. C 改动（系统组件）需走 ESP-IDF 完整编译,桌面模拟器无 heap_caps 后端,内存日志不可比。

---

## 七、用户反馈模式（沟通经验）

- 用户会反复微调字号（「再大一点」「不够大」）→ 默认给**跨档**值,而非微调边界内数字。
- 圆屏贴边 / 被裁 / 不滚 → 多数根因是：圆界超界、父对象裁剪、label 默认 WRAP、z 序错。先查这四类,不要急着重排布局。
- 参考外部仓库（如 `third_party/cyberpunk2077-breach-protocol-for-view` 的 `MatrixFinish/consts.ts`、`Sequence.tsx`）时,要**照抄其状态机语义**（左对齐+offset 缩进、逐序列锁定、混合终端颜色）,而不是简化成「一律成功/失败」。

---

# Agent 经验总结：JS APP（`.eapk`）编写 / 打包 / 注册 / 加载 / 挂载 / 图标展示

> 来源于对 `scripts/eos_pkg_builder.py`、`src/services/plugin/eos_plugin_manager.c`、`src/framework/app/eos_app.c`、`src/ui/launcher/eos_launcher_v1.c`、`src/framework/app/eos_app.h` 的源码梳理。
> 目标：沉淀「一个 JS APP 从源码到出现在 Launcher 并能点击运行」的完整链路,避免每次改 APP 都重新摸索。

---

## 一、APP 源码三件套（如何编写）

每个 APP 是一个**目录**,固定三件套（宏见 `src/framework/app/eos_app.h`）：

```
apps/<name>/
├── manifest.json   # 必填: id / name / version / minApiLevel / targetApiLevel
├── main.js         # 固定入口 EOS_APP_SCRIPT_ENTRY_FILE_NAME
└── icon.bin        # 48×48 RGBA PNG 图标 EOS_APP_ICON_FILE_NAME
```

- `manifest.json`：`id` 用域名式（`com.cantomk6.alarm`）；`minApiLevel`/`targetApiLevel` 当前样例都填 `0`；打包器 `read_manifest()` 校验这 5 个字段缺一则报错。无 `entry` 字段概念,入口恒为 `main.js`。
- 逻辑用 **JerryScript**（JS 引擎）执行,不支持 `entry` 自定义。
- 示例 manifest 见 `apps/alarm/manifest.json`。

## 二、打包（如何生成 `.eapk`）

`scripts/eos_pkg_builder.py` 把目录打成带 `EAPK` magic 的二进制包：header(name/id/version/apiLevel) + 文件表 + 文件体。

```bash
python3.11 scripts/eos_pkg_builder.py apps/alarm eapk-target/alarm.eapk --type app
```

- `--type app` → `EAPK`;`watchface` → `EWPK`。
- **模拟器**：CMake 为每个 app 定义 `<id>_eapk` 目标,POST_BUILD 自动把 `.eapk` 复制到模拟 SD 卡 `sdcard/apps/`,只改 JS 无需重编 C：
  ```bash
  cmake --build simulator/build --target alarm_eapk -j 8
  ```
- **真机 ESP32-S3**：需手动把 `.eapk` 复制到 SD 卡根目录 `apps/` 文件夹,开机或 `plugin scan` 自动安装。

## 三、路径常量（关键,来自 `eos_storage_paths.h`）

| 环节 | 路径 |
|---|---|
| SD 安装源（模拟器自动 / 真机手动） | `/sdcard/apps/*.eapk` |
| 安装解包目录（运行实例实际读取） | `/.sys/app/apps/<pkg_id>/`（含 manifest+main.js+icon.bin） |
| APP 数据目录 | `/.sys/app/app_data/<pkg_id>/` |
| Launcher 图标 | `<安装目录>/icon.bin`（48×48 RGBA PNG） |

> 运行实例读的是**解包目录**,不是 SD 上的 `.eapk`。改了 JS 重打包后若运行没变：删除该 app 的安装子目录再启动（boot 重装）,或 `plugin scan --force`（BUILD.md §5）。

## 四、注册（Plugin Manager 扫描）

`src/services/plugin/eos_plugin_manager.c`：boot 时 `eos_core.c` → `eos_plugin_manager_init()` → `scan(false)`：

1. 打开 `EOS_SD_APPS_DIR`,筛 `.eapk`/`.ewpk`;**SD 缺失不算错误**（Shell 仍可用,这是 Core 救援入口原则）。
2. 读包头 `eos_pkg_read_header()` 取 `pkg_id` 去重：`eos_app_list_contains(id)` 且安装目录内 `manifest.json` 存在 → skip（幂等）。
3. 否则 `eos_app_install(full)` → `eos_pkg_mgr_unpack()` 解包到 `/.sys/app/apps/<pkg_id>/`。

Shell 手动触发：
```
plugin scan            # 只装新包
plugin scan --force    # 强制重装(用于更新后的包)
app list / app start <id> / app stop <id> / app disable <id>
```

## 五、加载与运行（`eos_app.c`）

`eos_app_install()`：

- 校验 `pkg_id` 合法（`eos_storage_is_valid_filename`）、`min_api_level ≤ ELENIX_OS_API_LEVEL`,否则返回 `EOS_ERR_SDK_VERSION`（`eos_app.c:537`）。
- 解包到 `EOS_APP_INSTALLED_DIR/<pkg_id>`,创建 `app_data/<pkg_id>`,加入 order 列表后 `_eos_app_list_refresh()`（同时并入系统内置 app `EOS_SYS_APP_*` 与原生 C app `EOS_NATIVE_APP_*`）。
- 发 `EOS_EVENT_APP_INSTALLED` 事件 → Launcher 收到后重建。
- 运行时：Launcher 经 `eos_app_launch_immediately(id)` → script engine 从 `<安装目录>/main.js` 用 JerryScript 执行。

## 六、挂到 APP 页 & 展示图标（Launcher）

`src/ui/launcher/eos_launcher_v1.c`（默认 v2 走 `eos_launcher_build_home`,逻辑相同）：

- 收集：`eos_app_get_installed()` + `eos_app_list_get_id(i)` 遍历,加入 `s_apps[]`（`kind = LAUNCHER_APP_KIND_PLUGIN`,带 `app_id`）；系统 Gallery/Album/Files 为本地 native。
- 图标：对每个 plugin 拼 `EOS_APP_INSTALLED_DIR "<id>/" EOS_APP_ICON_FILE_NAME`,`eos_storage_is_file` 存在则作为该格图标,否则 `NULL`（占位白格）。
- 渲染：`eos_framework_home_create(parent, profile, names, icons)` 生成网格;卡片点击 → `eos_app_launch_immediately(id)`。
- 实时重建：安装/卸载事件触发 `eos_launcher_rebuild()`。

**图标生成**：`scripts/icon/sync_app_icons.py` 把 `resources/images/icon/*.webp`(512px 透明) 缩到 **48×48 RGBA PNG** 写入各 app `icon.bin`,并同步到模拟器运行 FS。
> 重要：LVGL fork 只能解 PNG（lodepng）,**不能解 webp**。图标必须是 `icon.bin`(PNG),不是 webp。

## 七、验证清单（每次改 APP 后）

1. `node --check apps/<app>/main.js`（JS 语法）。
2. 重新打包：`python3.11 scripts/eos_pkg_builder.py apps/<app> eapk-target/<app>.eapk --type app`。
3. 模拟器走 `<id>_eapk` 目标（自动复制到 SD 卡）；真机手动拷 `.eapk` 到 SD `apps/`。
4. 运行实例读的是解包目录 `/.sys/app/apps/<id>/`,改包后务必 `plugin scan --force` 或删安装目录重启,否则旧包仍生效。
5. 图标不显示：确认 `icon.bin` 是 48×48 RGBA **PNG**（非 webp）,且路径在 `<安装目录>/icon.bin`、`eos_storage_is_file` 为真。
6. 启动失败：查 `minApiLevel` 是否 ≤ `ELENIX_OS_API_LEVEL`;manifest 5 字段是否齐全。

## 八、一句话总结

`eos_pkg_builder.py` 打包 JS 目录为 `.eapk` → 放到 SD 卡 `apps/`（模拟器自动、真机手动）→ 开机或 `plugin scan` 自动解包安装到 `/.sys/app/apps/<pkg_id>/` → Launcher 列出并点击运行 `main.js` → 图标从同目录 `icon.bin`(48×48 PNG) 读取。

## 九、原生 C APP 重写经验（Texthub，2026-08）

### 9.1 为什么 texthub JS app 会 OOM 崩溃

旧版 `apps/texthub`（JS/JerryScript）启动即遍历 `/sdcard/texthub/` 下**几万个文件**，
同步建数组塞进固定 **512KB JerryScript heap**，日志先是大量 `[MemAuto]` 分配，随后
`JERRY_FATAL_OUT_OF_MEMORY`。教训：**JS runtime 的堆是固定大小的，禁止在 JS 里做大目录全量枚举**；
数据量大、需要低延迟 IO 的 APP 直接用 C 重写（参考 album：JS → C 的成功先例）。

### 9.2 原生 C APP 注册方式（三处，缺一不可）

1. `src/framework/app/eos_app_list.h`：枚举加 `EOS_NATIVE_APP_TEXTHUB`（`EOS_NATIVE_APP_LAST` 自动 +1）。
2. `src/framework/app/eos_app_list.c`：三数组同步加
   `eos_native_app_id_list[] = {..., "com.cantomk6.texthub"}`、
   `eos_native_app_icon_list[] = {..., EOS_IMG_TEXTHUB}`、
   `eos_native_app_entry_list[] = {..., eos_texthub_enter}`（数组大小 = `EOS_NATIVE_APP_LAST`）。
3. `src/ui/widgets/image/eos_image_resuorces.h`（注意拼写 resuorces）：
   `#define EOS_IMG_TEXTHUB "/sdcard/theme/icons/texthub.bin"` —— **图标从 SD 卡读**，不是 Flash。

**编译**：`EOS_CORE_SRCS` 是 `GLOB_RECURSE src/**/*.c`，新 C app 丢进 `src/apps/<name>/` 自动编译，**不用改 CMake**。

### 9.3 图标生成

`scripts/icon/webp2bin.py` 的 `SYSTEM_ICONS` 加 `"texthub": ("texthub", 48)` →
`python3 scripts/icon/webp2bin.py texthub --force` 生成 `resources/images/icon/bin/texthub.bin`
（LVGL 二进制格式：12B 头 + ARGB8888）。**烧录时需手动把该 .bin 拷到 SD 卡 `/sdcard/theme/icons/`**。

### 9.4 关键 API 确认（均已编译验证）

- 存储：`eos_storage_file_open_read/close/seek(fp,uint32_t)/read(fp,buf,size)→ssize_t/size(fp,&u32)`、
  `eos_storage_dir_open/read(dir,buf,size)/close`、`eos_storage_is_dir/is_file`、
  `eos_storage_mkdir_recursive`、`eos_storage_read_file/write_file`、`EOS_FILE_INVALID`（eos_fs_port.h）。
- 内存：`eos_malloc/eos_malloc_zeroed/eos_strdup/eos_realloc/eos_free`（eos_mem.h）。
- 常量：`EOS_FS_PATH_MAX=256`、`EOS_FS_NAME_MAX=256`（eos_config_defaults.h）。
- 圆屏：`eos_round_clip(lv_obj_t*)`。
- 字体：**直接 `&eos_font_jbm_13` 会编译错**（jbm 字体在 eos_font_c_multi.c 才 `LV_FONT_DECLARE`），
  一律用 `eos_label_set_font_size(lbl, EOS_FONT_SIZE_MICRO/TINY)`（13px=MICRO 档 jbm_13、10px=TINY 档 jbm_10，自带 han_sans 中文 fallback）。
- 图标字体：`EOS_FONT_ICON` 是 `lv_font_t` 结构体，**必须 `&EOS_FONT_ICON`** 传给 `lv_obj_set_style_text_font`。

### 9.5 踩坑记录

- `lv_obj_scroll_to_bottom()` **不存在**（LVGL9），滚到底用 `lv_obj_scroll_to_y(obj, LV_COORD_MAX, LV_ANIM_OFF)`（内部 clamp）。
- `lv_obj_add_event_cb` 的 user_data 是 `void*`，回调签名参数不要写 `const void*`。
- Xtensa 上 `uint32_t` = `long unsigned int`，snprintf 用 `%u` 要 cast `(unsigned)`。
- LVGL9 事件冒泡：子对象默认无 `LV_OBJ_FLAG_EVENT_BUBBLE`，弹层 panel 点击不会冒泡到下方 mask，无需手动拦事件。
- label 要可点击响应父级时：`lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE)`，否则子 label 截走父按钮的点击。

### 9.6 Texthub C 版设计（懒加载 + 分块）

- **FM 弹层**：树节点按需加载，只对展开的目录 `eos_storage_dir_open`；收起/关闭时 `eos_free` 子树。
  根目录固定在 `/sdcard/texthub/`，可递归展开任意子目录（深度上限 6、单目录 300 条上限防爆）。
- **大文件分块**：≤128KB 一次读入；更大按 16KB 段（行对齐）随滚动加载，
  `LV_EVENT_SCROLL_END` 检测 `scroll_y >= scroll_bottom-6` 翻段、顶部 `<=0` 回段。
- **历史**：打开文件即写 `/sdcard/history/texthub/latest.txt`，启动先读并校验 `以 /sdcard/texthub/ 开头 && 是文件`，否则打开 FM。
- UI 全英文、控件疏松（行高 36）；文件名超长用 `LV_LABEL_LONG_MODE_SCROLL_CIRCULAR` 跑马灯；
  目录行右侧箭头用 `RI_ARROW_RIGHT_S_LINE/DOWN_S_LINE`，关闭用 `RI_CLOSE_FILL`（均在 eos_font_icon.c 子集，**不要在 C 里用子集外码点**，会显示空白）。

## 十、LVGL 显示性能优化（参考 Seeed lvgl_workshop，2026-08）

参考:wiki.seeedstudio.com/round_display_animation_workshop(7-9FPS→30FPS) + 本地
third_party/lvgl_workshop-for-view(仅参考,不改)。Canto Mk.6 已实施的项:

### 10.1 已实施(全部编译验证)

1. **CPU 240MHz**:sdkconfig `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`
   (原 160MHz,渲染是 CPU 密集型)。CPU 频率改 sdkconfig 即可,IDF 自动重编。
2. **编译优化 -Og→PERF(-O2)**:`CONFIG_COMPILER_OPTIMIZATION_PERF=y`。
   **副作用**:-O2 开启 `-Werror=format-truncation` 等优化类警告,原项目大量
   snprintf 截断(UI 场景可接受)会编译失败——在 `port/esp32s3/main/CMakeLists.txt`
   的警告抑制列表追加 `-Wno-error=format-truncation/-Wno-error=array-bounds/
   -Wno-error=stringop-overflow/-Wno-error=maybe-uninitialized`(遵循该项目
   "不改原代码、用编译选项抑制"的既有约定)。
3. **异步 DMA flush**(最大收益):`spi_device_queue_trans` + `post_cb`。
   - 同步 `spi_device_transmit` 让 CPU 在 flush 期间空转,双缓冲形同虚设;
     异步后渲染 buffer B 与 buffer A 的 DMA 传输并行。
   - `spi_transaction_t` 必须用**静态数组**(按 slot 索引),不能是栈上局部
     (queue_trans 异步,事务结构在传输完成前必须有效)。
   - `post_cb`(设备级,SPI 驱动任务上下文,非 ISR)里调
     `lv_display_flush_ready((lv_display_t*)t->user)`(t->user 存 disp)。
   - 双 slot 轮换:LVGL 双缓冲最多 2 个 flush 在途,2 块 internal DMA 缓冲恰好匹配。
   - 失败回退:queue 失败时同步 transmit + flush_ready,保证画面不丢。
4. **SWAR 字节交换**:GC9A01 需大端 RGB565,flush 前交换。32-bit 一次换 2 像素:
   `((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)`,8 路展开,比逐 16-bit 快数倍
   (Xtensa 无 SIMD 指令集扩展)。
5. **内部 DMA 缓冲双 slot**:`s_spi_flush_buf[2][19200]`(DRAM_ATTR, aligned(4)),
   共 38.4KB internal DRAM(原 19.2KB,净增 19.2KB)——因 SPI DMA 不认 PSRAM 地址,
   LVGL PSRAM buffer 必须 memcpy 到 internal 缓冲(直接当 tx_buffer 会让驱动内部
   临时 malloc 失败 ESP_ERR_NO_MEM)。

### 10.2 明确不做/放弃的

- **LTO:IDF v5.3 已移除官方 LTO 支持**(无 Kconfig、无文档),sdkconfig 手写
  `CONFIG_COMPILER_OPTIMIZATION_LTO=y` 会被 reconfigure 静默清除。手动 -flto
  是实验性的,与 IDF 链接系统(--wrap=malloc 等)兼容性风险高,放弃。
- **SPI 频率 26→40/80MHz**:教程 Phase 2 推荐 80MHz,但本项目 LCD 与 SD 卡
  共用 SPI3 总线,提频影响 SD 时序稳定性,属硬件时序改动,需真机验证后才可动。
- **LV_COLOR_16_SWAP**:曾造成双重交换纯色错乱,维持 flush_cb 手动交换(现在
  是 SWAR 版本,开销已很低)。
- **ESP32-S3 SIMD 补丁(lvgl_s3_simd_patch)**:教程配套,需第三方 patch,
  本地 third_party 无此补丁,不引入。

### 10.3 验证结果

- 固件 5.47MB(< 7.44MB 工厂分区),LTO/-O2 后体积略减。
- 编译产物确认:`build/config/sdkconfig.h` 有 CPU 240 + PERF;
  elf 的 `s_spi_flush_buf` 0x9600(双 slot)在 internal DRAM。
- 预期:渲染与传输并行 + CPU 提速 1.5x,动画帧率显著提升(真机 FPS 待验证)。

### 10.4 真机踩坑:异步 flush 与同步命令混用崩溃(2026-08-27 修复)

**症状**:烧录后 `assert failed: spi_device_transmit spi_master.c:1266
(ret_trans == trans_desc)`,崩溃栈在 flush_cb → display_set_window →
display_send_cmd → spi_device_transmit。

**根因**(IDF esp_driver_spi v5.x):
- `spi_device_transmit`(同步)内部是"queue 一份栈上拷贝 + get_trans_result
  取回第一个完成的事务 + 断言取回的就是自己"。它**隐含假设队列里没有
  其他 pending 事务**。
- 异步 flush(queue_trans 的像素事务还挂在队列/正在传输)时,flush_cb 再调
  同步 transmit 发窗口命令 → get_trans_result 先取回异步像素事务 → 断言失败。

**教训/方案**(照 IDF esp_lcd_panel_io_spi 的 lcd_spi_trans_descriptor_t 重写):
1. **所有事务(含窗口命令)都用 queue_trans 排队**,FIFO 保证命令在像素前;
   绝不在有在途事务时用同步 spi_device_transmit。
2. **事务级区分**:IDF v5.x 的 spi_transaction_t 没有事务级 pre_cb/post_cb,
   只有设备级(spi_device_interface_config_t,每事务都触发,ISR 上下文)。
   做法:自定义描述符,spi_transaction_t 作为第一个成员,pre_cb 里
   container_of 还原描述符,读 dc_level 位切换 DC 引脚(异步安全,无队列竞态)。
3. **flush 完成同步不用 ISR 里的 lv_display_flush_ready**——LVGL 的
   wait_for_flushing 支持 flush_wait_cb(lv_display_set_flush_wait_cb):
   在 LVGL 任务上下文用 `spi_device_get_trans_result` 循环取回在途事务
   (pending 计数),LVGL 随后自动清 disp->flushing。避开 ISR 调 LVGL
   timer resume 的断言/竞态风险,且渲染下一帧与上一帧 DMA 并行(收益保留)。
4. 在途事务计数(volatile int)仅 LVGL 任务单线程访问,无需原子。

**性能验证**:LVGL 9 双缓冲 partial 模式,每次 draw_buf_flush 前都调
wait_for_flushing → flush_wait_cb;渲染 B 与 A 的 DMA 并行,wait 只在
渲染完成后检查(若 A 已完则零等待),总帧时 ≈ max(render, xfer) + xfer。

### 10.5 真机踩坑:check_trans_valid 会改写描述符的 rxlength(2026-08-27 修复)

**症状**:稳定复现 `spi_master: check_trans_valid(1039): rx length > tx length
in full duplex mode` + `GC9A01: flush queue failed (5/6 queued)`。6 个事务中
只有最后 1 个(像素,length 每帧变化)被拒,命令事务(length 固定)正常。

**根因**(esp_driver_spi/src/gpspi/spi_master.c check_trans_valid,~line 1063):
```c
//In Full duplex mode, default rxlength to be the same as length, if not filled in.
if (trans_desc->rxlength == 0 && !is_half_duplex) {
    trans_desc->rxlength = trans_desc->length;   // 直接改写传入的描述符!
}
```
全双工下驱动把 rxlength==0 自动改写为 length(MISO 相位需等长)。若复用
描述符且每次只重写 length:上一帧的大 length 残留在 rxlength 里,本帧 partial
区域变小时 `rxlength > length` → 1039 行拒绝。

**修复**:每帧整体 memset 描述符(清零 rxlength),再重设 length/tx_buffer/
dc_level。注意 check_trans_valid 在 queue 时与 ISR 执行时都会改描述符,
对复用描述符这是确定性副作用,不是偶然内存破坏。

**通用教训**:复用 spi_transaction_t 时,任何被驱动读取/改写的字段都要每帧
重建;不要只改自己关心的字段。queue 前描述符的 length/rxlength/flags
必须是干净的"新建"状态。

## 十一、Texthub 文件树看不到文件的 Bug（2026-08-27 修复）

### 11.1 症状

进入 Texthub（原生 C app,`src/apps/texthub/eos_texthub.c`）后,FM 文件树弹层能显示
目录层级(`texthub/` → `Global Text Archive...` → `03_FINISHED...` → `Art/`),
但**目录里的 `.txt`/`.md` 文件永远不出现在列表里**,看起来"看不到文件"。

### 11.2 排查过程（与 SPI 显示问题无关）

- 一开始怀疑 SD 挂载/读取问题,但日志 `[LVGL_FS] open path: /sdcard/theme/icons/album.bin`
  成功,说明 SD 挂载与 `eos_storage_*` 读取链路正常。
- `EOS_FS_NAME_MAX=256`、`EOS_FS_PATH_MAX=256`(eos_config_defaults.h),
  长目录名/长路径不会截断;目录树最深 4 层,未超 `TH_FM_DEPTH_MAX=6`,路径长度也未超 256。
- 排除显示层:同一时间点没有 `flush queue failed` 日志,UI 标题渲染正常 → 与第 10.4/10.5
  的 SPI 问题是两个独立维度(一个是驱动丢帧,一个是应用数据层内容缺失)。

### 11.3 根因（一个被忽略的过滤条件）

`_fm_build_items()` 把**所有非目录节点**直接从可见列表排除:

```c
static void _fm_build_items(th_node_t *n, int depth, int *cnt)
{
    if (*cnt >= TH_FM_MAX_VISIBLE) return;
    if (!n || !n->is_dir) return;          // ← bug:文件节点直接 return,进不了 s_fm_items
    ...
}
```

而 `_fm_rebuild()` 的渲染循环里其实已经写好了文件行分支(白色文件名、点击 `_open_path`),
但文件永远不被加入 `s_fm_items`,那个分支成了死代码。
`_fm_node_load()` 的 pass1/pass2 分别 `eos_storage_dir_open/close`,目录枚举本身正确
(子目录和 `.txt/.md` 都保留,其余文件按 `_is_text_ext` 过滤),问题仅出在"可见列表构建"这一层。

**修复**:让文件节点也进入可见列表,只有目录才递归展开子节点:

```c
static void _fm_build_items(th_node_t *n, int depth, int *cnt)
{
    if (*cnt >= TH_FM_MAX_VISIBLE) *cnt >= TH_FM_MAX_VISIBLE) return;
    if (!n) return;
    s_fm_items[*cnt].node = n;
    s_fm_items[*cnt].depth = depth;
    (*cnt)++;

    /* only directories recurse (files are leaves: click opens them) */
    if (n->is_dir && n->expanded && n->children) {
        for (int i = 0; i < n->child_count && *cnt < TH_FM_MAX_VISIBLE; i++) {
            _fm_build_items(&n->children[i], depth + 1, cnt);
        }
    }
}
```

### 11.4 通用教训

1. **"渲染循环有、收集循环没有"是典型死代码陷阱**:先确认数据是否进了被遍历的数组,
   再调渲染细节。两层逻辑(`build_items` 收集 / `rebuild` 渲染)不一致时,优先查收集层。
2. **懒加载树的设计语义**:本文件树是一次展开一层(`expanded` 才递归),不是一次性全量
   扫描。这与第 9.1 的教训一致——JS 全量枚举几万文件会 OOM,所以改成 C + 懒加载。
   用户说"不会递归目录吗"时,要区分"设计上不递归(每次只读一层)"与"递归了但文件没显示"。
3. **目录过滤 vs 显示过滤要分开**:枚举时(`_fm_node_load`)按 `_is_text_ext` 过滤非文本文件是合理的;
   但"显示在树上"的过滤(`_fm_build_items`)不应再加一道 `!is_dir` 的硬排除——否则叶子节点消失。
4. **逐层展开交互**:修复后需用户手动点击目录展开;若需要"进来直接平铺所有 txt",应另加
   扁平浏览模式,而不是把懒加载改成全量(会回到 OOM 老路)。

### 11.5 验证清单（树/列表类 UI）

- 渲染前有 `items` 计数是否 > 0:用 `EOS_LOG_I("fm items=%d", s_fm_item_count)` 打点,
  确认收集层真的产出了条目(而非渲染层没画出来)。
- 区分两类"看不到":空数据(收集返回 0) vs 空渲染(数据有但没画)。先打印数据量再查绘制。
- 混合"容器展开 + 叶子操作"的树,展开回调只改 `expanded` 标记然后 `_fm_rebuild()`,
  不要在展开回调里直接手动增删行,,统一走重建路径最稳。

---

## 十二、Album C 重写：懒加载文件管理器 + 历史记录 + 字母键盘密码（2026-08）

> 目标：把 Album 从「单目录平铺 + 无导航」改造成「仿 Texthub 文件管理器 + 历史恢复 + 单目录浏览」，
> 同时把关屏密码（settings/pwd 与 lock page）从数字键盘升级为共享字母键盘（任意字符）。
> 来源：真实改写 `src/apps/album/eos_album.c`、`src/apps/settings/eos_settings.c`、`src/ui/pages/lock/eos_lock_page.c`。

### 12.1 总体改动范围（含一处 app 外修改）

| 文件 | 归属 | 改动 |
|---|---|---|
| `src/apps/album/eos_album.c` | app 内 | 新增懒加载文件管理器、history、单目录扫描 |
| `src/apps/settings/eos_settings.c` | app 内 | 密码输入改用 `eos_input_page` 共享键盘（任意字符） |
| `src/ui/pages/lock/eos_lock_page.c` | **app 外** | 锁屏验证键盘换成同一个共享圆键盘（用户已确认） |

**app 外修改原因**：密码改为字母键盘后，锁屏原 numpad 无法输入字母密码，必须同步替换，否则字母密码无法解锁。

### 12.2 Album C 文件管理器设计（复用 Texthub FM 套路）

- **复用而非复制**：Album 的 `_fm_*` 系列（`_fm_node_load` 懒加载、`_fm_build_items` 收集、`_fm_rebuild` 渲染、`_fm_children_free` 释放子树）直接移植自 Texthub，仅做三处定制：
  1. 过滤规则：`_is_image_ext`（`.jpg/.jpeg/.png`），且 `_is_dir || image` 才入树（参考 11.4 教训：**叶子节点必须进可见列表**）。
  2. 根目录固定 `/sdcard/album/`，只浏览相册目录（符合 Album 定位）。
  3. 行高 36px（疏松），目录在前按名排序（`qsort` + `_fm_cmp`：目录优先、同类型 `strcasecmp`）。
- **历史恢复**（仿 Texthub）：打开图片即写 `/sdcard/history/album/history.txt`；启动先读并校验「前缀 `/sdcard/album/` + `eos_storage_is_file`」，合法则直接打开，否则进 FM。删除图片后 history 指向已删文件 → 下次启动校验失败 → 自动进 FM。
- **单目录浏览**：原 Album 是「递归扫描全部图片」，改为「打开某图 → 取其目录 → 扫描该目录图片 → 定位该图」。翻页列表只是当前目录的平铺数组（仍按名排序），不再是全文件系统递归（避免 JS 版 OOM 老路，见 9.1）。

### 12.3 查看器缩放/平移手势（单点触摸硬件）

- 硬件只有单点触摸（CHSC6X），**无 pinch**，所以用「tap 切换 zoom 档 / drag 平移」模拟：
  - `_img_event_cb` 在 `PRESSED` 记录起点；`PRESSING` 超 8px 判为拖拽；`RELEASED` 若无拖拽则切 zoom。
  - zoom 用 `ALBUM_ZOOM_LEVELS[] = {256,384,512,768}`（Q16 倍率），`lv_image_set_scale`；放大后图片溢出圆 clip，拖动改 `lv_obj_set_x/y` 实现 pan。`_apply_view` 里先按 `_fit_scale` 求基准倍率，再乘 zoom，并对 pan 做 clamp 防止露边。
  - **放大时右滑 = 拖图看细节**，故 `_swipe_back_cb` 在 `s_zoom_idx>0` 时返回 `true`（吞掉系统退出手势）；未放大时仍允许右滑退出。

### 12.4 密码改用共享字母键盘（settings + 锁屏）

- `eos_input_page` 是 WiFi/Alarm 共用的输入框页面（圆键盘），API：
  ```c
  typedef enum { EOS_INPUT_TEXT, EOS_INPUT_PASSWORD } eos_input_mode_t;
  lv_obj_t *eos_input_page_enter(const char *title, int max_len, eos_input_mode_t mode,
                                eos_input_result_cb_t cb, const char *init);
  ```
  `@ok`/`@cancel` 键分别发 `LV_EVENT_READY` / LVGL 内置 cancel 事件（已实测）。
- **settings/pwd**（`eos_settings.c`）：移除 Simple(4/6位) 开关；密码可为任意字符；流程 = 三步状态机（`EOS_PWD_STEP_NEW_1/NEW_2` 或 `CHANGE_OLD/NEW_1/NEW_2`）；旧密码错 3 次中止；空密码拒绝；用 `eos_malloc`+`memcpy` 存密码（不要用 `eos_strdup` 混入 libc 分配器，见第一节跨堆教训）。
- **锁屏**（`eos_lock_page.c`）同步换键盘：移除 `eos_numpad` 依赖，改用 `eos_round_keyboard_create` + `lv_textarea`（password mode）；`@ok` 触发验证；错误时 shake（lv_anim）+ 红字 + 清空；布局按 240×240 下半屏定位（键盘 y=120~240，输入框 y<120）。

### 12.5 验证 / 踩坑

- **编译前必查的前向声明**：C 里「使用在定义之前」的函数要显式前向声明，否则 ESP-IDF（`-Werror` 隐含）报 implicit declaration。`_show_current` 被 `_open_image_path` 调用、`_fm_open` 被 `_fm_open_cb` 调用，二者定义均在调用点之后，必须加 `static void xxx(void);` 前向声明（lint 工具不一定报，但编译器会）。
- **图标仅能用 eos_font_icon 子集**：`RI_*_LINE/FILL` 等码点来自 `src/ui/symbol/eos_icon.h`，C 里引用前必须 `search_content` 确认宏存在；用子集外码点会显示空白。本次用到：`RI_FOLDER_3_LINE`、`RI_ARROW_DOWN_S_LINE`、`RI_ARROW_RIGHT_S_LINE`、`RI_CLOSE_FILL`、`RI_DELETE_BIN_5_LINE`、`RI_KEYBOARD_BOX_FILL`。
- **字体**：`lv_font_montserrat_12/14/16` 已在 `port/esp32s3/main/lv_conf.h` 启用（`LV_FONT_MONTSERRAT_12/14 = 1`），可直接 `&lv_font_montserrat_14`；但若不确定，优先用 `eos_label_set_font_size(lbl, EOS_FONT_SIZE_*)` 自动选档（见第二节）。
- **LVGL9 API 确认**：`lv_image_decoder_get_info(path, &hdr)`、`lv_sqrt32`、`lv_obj_set_style_bg_opa(mask, 160, 0)`（0~255 合法）均可用；`lv_textarea_set_placeholder_text` 在 password mode 下显示正常。
- **history 校验**：读文件后必须 `eos_free`（`eos_storage_read_file` 返回 malloc 内存）；路径结尾 `\n` 要 trim，否则前缀比较失败。
- 编译命令：`cd port/esp32s3 && source ~/esp/esp-idf/export.sh && idf.py build`（整固件重编，不能热更）。唯一残留 warning 是无关的原 `_bt_paired_fill` snprintf 截断，非本次改动。

### 12.6 字节级教训

- 「复用既有实现」永远先做最小必要改造：本会话 Album 的 FM 直接复用 Texthub 的两层模型（collect vs render），只改过滤/根/行高，避开 11.4 记录的「叶子节点被硬排除」坑。
- app 内改动与 app 外改动要分清并在总结里单列；底层（锁屏页）改动需用户明确同意，否则优先只改 app 内。

---

## 十三、Album 大图解码失败（TJpgDec scale 语义，2026-08-27 修复）

### 13.1 症状

打开一张 1920×1080 JPEG（如 Cyberpunk 游戏截图）时,Album 提示「解码失败」,退出回 Launcher。日志:

```text
Album: decode /sdcard/album//_1920x1080Corporate_Pl_q70_1920x1080.jpg
Album: jpeg 1920x1079 scale=4 -> 480x269
Album: jd_decomp FAIL rc=5 w=1920 h=1079 scale=4
```

而 ≤1024 宽的小图（scale≤2）能正常显示。

### 13.2 排查过程

1. `jd_prepare` 成功、`MemAuto` 内存分配成功(258248 字节,PSRAM 充足)、`eos_storage` I/O 正常
   → 不是内存、不是 IO、不是文件格式问题。
2. **rc=5 = JDR_PAR(参数错误)**,失败点在 `jd_decomp` 入口,而非解码过程。
3. 查 `tjpgd.c` 的 `jd_decomp` 第一行:
   ```c
   if(scale > (JD_USE_SCALE ? 3 : 0)) return JDR_PAR;
   ```
   TJpgDec 的 `scale` 参数是**右移位数 0~3**:`0=原图 / 1=1/2 / 2=1/4 / 3=1/8`,**最大 3**。
4. 原 Album 的 `_decode_jpeg` 把 scale 当成 "缩小倍数" 从 1 左移(1→2→4→8):
   ```c
   uint8_t scale = 1;
   while (scale < 8 && (w / scale > ALBUM_DECODE_MAX || h / scale > ALBUM_DECODE_MAX)) {
       scale <<= 1;
   }
   ```
   1920×1079 → scale=4 → 超出 3 → `JDR_PAR`。**所有 1025~4096 宽的图都必然失败。**

### 13.3 根因

**API 语义混淆**:把 TJpgDec 的 `scale`(右移位数 0~3)误当成"缩小分母倍"(1/2/4/8)。
这是第三方库(TJpgDec)的约定,不是 Album 自己定义的缩放逻辑,必须先读库源码确认参数语义。

**附带发现**:`_jd_out` 的边界块行距 `src += bw*2` 其实是**对的**——
`tjpgd.c` 的 `jd_mcu_output` 在边界裁剪时已经把输出块"压缩"成紧凑行(每行 rx 像素),
所以不必按整块宽前进。这个怀疑点排除,无需改动。

另外 `_jd_in` 每次解码文件末尾都会读不足 `JD_SZBUF`,原来会打印 `short read`,
这是 EOF 正常行为,非错误,已移除该噪音日志。

### 13.4 修复(`src/apps/album/eos_album.c`)

改为右移位数 0~3:

```c
/* TJpgDec scale = right shift count: 0=full, 1=1/2, 2=1/4, 3=1/8 (max) */
uint8_t scale = 0;
uint16_t ow = w, oh = h;
while (scale < 3 && (ow > ALBUM_DECODE_MAX || oh > ALBUM_DECODE_MAX)) {
    scale++;
    ow >>= 1;
    oh >>= 1;
}
```

1920×1079 → scale=2(1/4) → 480×269 ≤ 512(`ALBUM_DECODE_MAX`) → `jd_decomp` 参数合法。

### 13.5 通用教训

1. **第三方库的"scale / level / mode"类参数,先读库源码确认语义和单位**。库的 scale 常是
   右移位数或枚举,不是自由倍数;本项目自创的 `1/2/4/8` 与库约定不符,属于"想当然"型 bug。
2. **解码失败先打印 `jd_prepare` 与 `jd_decomp` 的返回码**,用 `JRESULT` 枚举反查失败阶段
   (准备 vs 解码 vs 参数),比看像素数据更高效。日志里一句 `rc=5` 直接锁定入口检查。
3. **大图必然走多档 downscale**,scale 上限要受库限制。1/8 仍超 `ALBUM_DECODE_MAX` 的图
   (原始 > 4096px)只能判 "too large",这是设计上限,在日志里明示 "even at 1/8"。
4. **解码末尾的 `short read` 是 EOF 常态**,不应作为 ERROR/WARN 刷屏;只有 `jd_in` 返回 0
   (真实读不出)才需要关注——而 TJpgDec 对 0 已返回 0 触发 `JDR_INP`,会按错误处理。

### 13.6 验证清单(图像解码类)

- 准备/解码分离:分别打印 `jd_prepare` 与 `jd_decomp` 的 `JRESULT`,定位失败阶段。
- `rc` 对照 `JRESULT` 枚举:`JDR_PAR` 多半是 scale 参数、尺寸参数超范围;`JDR_FMT` 是数据格式;
  `JDR_MEM` 是 workbuf 不足;`JDR_INP` 是输入流断开。
- 缩略缩放统一用库的语义(本项目 TJpgDec 右移位数),不要自创比例体系。
- 边界块、最后一行错位通常是解码器已处理(紧凑输出),优先确认库行为再改应用层拷贝逻辑。

---

## 十四、定时关机「设 2 分钟却 2 小时不醒」—— UI 字段单位陷阱

> 来源：XIAO ESP32-S3 + Round Display 1.28″ 定时关机（TIMER 模式）实测。
> 现象：UI 显示 `00:02:00`，用户以为 2 分钟，实际等 2 分钟根本不醒，深睡 + 唤醒机制本身完全正常。

### 14.1 Bug 一句话结论

**UI 字段布局与用户直觉反转**：关机页 TIMER 设置用的是 `DD:HH:MM`（天:时:分），而用户按
`HH:MM:SS`（时:分:秒）去读。于是：
- 显示 `00:02:00` → 用户以为 = 2 分钟 → 实际 = `0天2时0分` = **7200 秒（2 小时）**
- 显示 `00:00:10` → 用户以为 = 10 秒 → 实际 = `0天0时10分` = **600 秒（10 分钟）**

深睡 + RTC 定时器唤醒从头到尾都是好的，只是"定时太长"导致用户以为"根本不开机"。

### 14.2 证据链（串口日志铁证）

UI START 时打印实际换算值，一眼定位：
```text
Wake timer START: 00:02:00 -> 7200 s    ← 2分钟直觉 vs 7200秒实际
Wake timer START: 00:00:10 -> 600 s     ← 10秒直觉 vs 600秒实际
...
I (54259) Board: Power off (timed): one-shot sleep 7200 s
--- Error: device reports readiness to read but returned no data (device disconnected...) ---
--- Waiting for the device to reconnect... -----------------------------------------
```
最后两行是 **深睡成功** 的特征（ESP32-S3 深睡时 USB 串口掉线，monitor 报 device disconnected 并等待重连），
**不是崩溃**。唤醒后 USB 重新枚举，monitor 自动重连。

### 14.3 根因代码（`src/apps/power_off/eos_power_off_page.c`）

```c
static lv_obj_t *s_fld[3] = {NULL, NULL, NULL};  /* DD / HH / MM */
static int      s_val[3]  = {0, 1, 0};           /* 默认 1 小时 */
static const int s_max[3] = {30, 23, 59};        /* 各字段上限 */
...
uint32_t total = (uint32_t)s_val[0] * 86400u   /* DD */
              + (uint32_t)s_val[1] * 3600u      /* HH */
              + (uint32_t)s_val[2] * 60u;       /* MM */
if (total < 60u) total = 60u;   /* 最小 1 分钟 */
```

**三重叠加误导**：
1. 无单位标签（屏幕上只有 `00:02:00`，没有 DD/HH/MM 标注）→ 用户按时钟习惯读成 时:分:秒。
2. `s_max={30,23,59}` 暗示"天"位最多 30，进一步强化"这是天"的语义，但与用户直觉冲突。
3. 最小兜底 `if (total<60) total=60` 把 <1 分钟的设定顶到 1 分钟，进一步掩盖短定时需求。

### 14.4 修复

布局改为 `HH:MM:SS`（时:分:秒），与用户直觉一致；上限 `{99,59,59}`，最小兜底 1 秒：
```c
static lv_obj_t *s_fld[3] = {NULL, NULL, NULL};  /* HH / MM / SS */
static int      s_val[3]  = {0, 1, 0};           /* 默认 1 分钟 */
static const int s_max[3] = {99, 59, 59};        /* 最大 99:59:59 */
...
uint32_t total = (uint32_t)s_val[0] * 3600u    /* HH */
              + (uint32_t)s_val[1] * 60u       /* MM */
              + (uint32_t)s_val[2];            /* SS */
if (total == 0u) total = 1u;   /* 最小 1 秒 */
```
现在 `00:02:00` = 120 秒、`00:00:10` = 10 秒，符合直觉，10 秒快测即可验证。

### 14.5 通用教训

1. **带冒号的数值 UI 一定要显式标注单位**（`HH:MM:SS` 旁边配文字，或字段下方写 `H M S`）。
   没有标注的多段数字，用户必然按最常见的时间格式（时:分:秒）去读，与代码语义（DD:HH:MM、
   MM:SS、度:分:秒……）冲突时就是隐形 bug。
2. **「以为不开机」先别怀疑底层驱动**。串口日志里只要出现 `esp_deep_sleep_start` 前后正确的
   `one-shot sleep N s` 且之后 device disconnected —— 说明深睡已成功，问题在"N 是不是你以为的 N"。
3. **换算 bug 用「直觉值 vs 实际值」并排打印即可秒定位**：在 START/确认入口 log 出
   `显示字符串 -> 换算秒数`（如 `00:02:00 -> 7200 s`），无需动深睡/定时器一行代码就能锁定。
4. **最小兜底要匹配新单位**：原 `if<60→60` 是为"分钟级最小 1 分钟"设计的，改成秒级后必须改 `if==0→1`，
   否则会破坏短定时（10 秒被顶到废逻辑）。改动换算粒度时同步检查所有兜底。
5. **深睡串口掉线是预期行为，不是死机**：ESP32-S3 深睡期间 USB UART 断连，monitor 报
   `device disconnected / waiting to reconnect`，唤醒后自动恢复；区分"深睡掉线"与"panic 死机"
   看掉线前最后一条日志是否定格在 `one-shot sleep … s`（正常）还是卡在断言/错误（异常）。

---

# Agent 经验总结：Watchface 渲染崩溃 `draw_letter_cb` LoadProhibited —— 压缩字库与 `CONFIG_LV_CONF_SKIP` 配置陷阱

> 来源：XIAO ESP32-S3 + Round Display 1.28″，ElenixOS（Canto Mk.6）Builtin watchface 首屏（2026-09-06）。
> 现象：boot 动画结束、进入 watchface 后立即 `Guru Meditation Error: Core 0 panic'ed (LoadProhibited)`，
> backtrace 定格在 `lv_draw_sw_letter.c:179 draw_letter_cb`。

### 15.1 Bug 一句话结论

**不是内存不足，是压缩字库没被解码**：中文字符在 16px 子集字库里查不到 → fallback 到全量
`eos_font_han_sans_22`（`bitmap_format=1`，即 `LV_FONT_FMT_TXT_COMPRESSED`），而 LVGL 编译时
`LV_USE_FONT_COMPRESSED` 实际为 **0**，`lv_font_get_bitmap_fmt_txt()` 命中
`#if !LV_USE_FONT_COMPRESSED` 分支 → `LV_LOG_WARN` + `return NULL` → 渲染线程把 NULL 当位图解引用 → 崩溃。

启用压缩的开关写在了 **`lv_conf.h` 里，但那个文件根本没被编译**（`CONFIG_LV_CONF_SKIP=y`），
LVGL 的真实配置来源是 **sdkconfig 的 Kconfig**。

### 15.2 崩溃现场

```text
[INFO] [WFBuiltin] Builtin watchface: enter
[DEBUG] [AppHeader] Hide app header
Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.
EXCVADDR: 0x00000010
--- 0x420fb882: draw_letter_cb at .../third_party/lvgl/src/draw/sw/lv_draw_sw_letter.c:179
--- 0x420d2cf5: lv_draw_unit_draw_letter at .../lv_draw_label.c:631
--- 0x420f83a8: render_thread_cb at .../lv_draw_sw.c:365
```

两条判读要点：
1. 崩在 **render thread**（`prvRunThread`），不在 UI 线程 → UI 线程打日志覆盖不到崩溃点，
   必须在 UI 线程"模拟渲染调用链"复现（见 15.3）。
2. `EXCVADDR: 0x00000010` 是典型的 **NULL 指针 + 小偏移** 解引用，对应 `glyph_draw_dsc->glyph_data` 为 NULL。

### 15.3 证据链：如何在 UI 线程复现渲染路径

在 `eos_watchface_builtin.c` 加临时探针，逐字符走一遍与崩溃栈相同的 API：

```c
lv_font_glyph_dsc_t gd;
bool ok = lv_font_get_glyph_dsc(ft[fi], &gd, cp, 0);
lv_draw_buf_t *db = lv_draw_buf_create(gd.box_w, gd.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
lv_draw_buf_t *r  = db ? lv_font_get_glyph_bitmap(&gd, db) : NULL;
EOS_LOG_I("[bm] ... db=%p bm=%p d0=%02x d1=%02x d2=%02x", (void *)db, (void *)r, ...);
```

输出（关键四行）：

```text
[bm] jbm 'b' cp=98    gid=67   adv=16 box=12x20 fmt=4 db=0x3fcccda8 bm=0x3fcccda8 d0=bb d1=ff d2=dd  ← 正常
[bm] han '?' cp=27721 gid=2852 adv=22 box=22x21 fmt=4 db=0x3fcccda8 bm=0x0        d0=00 d1=00 d2=00  ← 元凶
[bm]    resolved=0x3c2bf4d0 han16=0x3c3e5880 han22=0x3c2bf4d0                                        ← fallback 命中 han22
[bm]    fdsc bpp=4 bfmt=1 cmap=37 gbmp=0x3c2dd204 gdsc[2852]=0x3c2cdb34 bw=22 bh=21 bi=427412
```

四层排除：
- `db` 非空 → **不是** draw buffer 分配失败（"DRAM 紧张"假说排除）
- `gid=2852`、`box=22x21` 非零 → **不是** cmap 查表错乱 / gid 越界（第一版假说，被此读数推翻）
- `resolved == han22` → 确认走 fallback 链，`'汉'` 不在 han16 的 176 字子集里
- `bfmt=1` + `bm=NULL` → 精准锁定"压缩字库没解码"

### 15.4 根因（三层）

**第一层：为什么会 fallback 到 han22。**
`eos_font_han_sans_16` 是 176 字子集（生成时带 `--lv-fallback eos_font_han_sans_22`），
子集外的汉字全部落到全量 22px 字库；该字库由 `lv_font_conv` **默认压缩**生成 → `bitmap_format=1`。
（对比：`jbm_22/26/30` 生成时显式 `--no-compress`，`bitmap_format=0`，所以它们一直是好的 ——
这正好解释了"只有中文崩"。）

**第二层：为什么压缩字库解不出来。**
`lv_font_fmt_txt.c` 中 gid≠0、box≠0 时，唯一能返回 NULL 的路径：

```c
#if LV_USE_FONT_COMPRESSED
    ...decompress...; return draw_buf;     /* 非空 */
#else
    LV_LOG_WARN("Compressed fonts is used but LV_USE_FONT_COMPRESSED is not enabled...");
    return NULL;                           /* ← 命中这里 */
#endif
```
PLAIN 分支与该宏无关 → 只有压缩字库受影响，与"jbm / montserrat 全部正常"完全吻合。

**第三层（真正的坑）：宏为什么是 0。**

```text
sdkconfig:2028: CONFIG_LV_CONF_SKIP=y
```

`lv_conf_internal.h` 的处理：

```c
#include "lv_conf_kconfig.h"                              /* 36-42 行 */
#if defined(CONFIG_LV_CONF_SKIP) && !defined(LV_CONF_SKIP)
    #define LV_CONF_SKIP
#endif
...
#if !defined(LV_CONF_SKIP) || defined(LV_CONF_PATH)
    #include "lv_conf.h"          /* ← 被 SKIP，整份 lv_conf.h 不参与编译 */
#endif
...
#ifndef LV_USE_FONT_COMPRESSED                            /* 1900-1907 行 */
    #ifdef CONFIG_LV_USE_FONT_COMPRESSED
        #define LV_USE_FONT_COMPRESSED CONFIG_LV_USE_FONT_COMPRESSED
    #else
        #define LV_USE_FONT_COMPRESSED 0                  /* ← 最终值 */
    #endif
#endif
```

本工程有 **三份 `lv_conf.h`**（`port/esp32s3/main/`、`third_party/lvgl/`、模拟器目录），
`third_party/lvgl/lv_conf.h:153` 甚至写着 `#define LV_USE_FONT_COMPRESSED 1` 且带注释说明"压缩字体…" ——
**但它是死配置，从未参与编译**。历史排查一直在改这个文件，所以"改了没用"。

### 15.5 修复

改 **sdkconfig**（Kconfig 才是真实来源），而不是 `lv_conf.h`：

```bash
sed -i 's/# CONFIG_LV_USE_FONT_COMPRESSED is not set/CONFIG_LV_USE_FONT_COMPRESSED=y/' sdkconfig
idf.py reconfigure      # 必须：重新生成 build/config/sdkconfig.h
idf.py build            # 宏影响 lv_global.h 结构体布局，会全量重编
```

验证：
```text
build/config/sdkconfig.h:957:#define CONFIG_LV_USE_FONT_COMPRESSED 1
```

真机复测（探针日志）：
```text
[bm] han '?' cp=27721 gid=2852 box=22x21 bm=0x3fcccde0 d0=00 d1=00 d2=11   ← bm 非空，有真实 alpha
```
watchface 正常显示，心跳持续 1600+ ticks 无崩溃。

**正面副作用**：`eos_font_icon` 也是压缩字库，此前"右滑打开 Control Center 渲染图标字符崩溃重启"
大概率同一根因，一并修复。

### 15.6 关键排查手法：dump 宏的"真实编译值"（本次最值钱的一招）

**不要相信"文件里写了什么"，要直接问编译器"看到了什么"。**

1. 从 `compile_commands.json` 取目标源文件的编译命令，把 `-c` 换成 `-E -dM`
   （去掉 `-o` / `-MMD` / `-MF` / `-MT`）：

```bash
cd port/esp32s3 && python3 -c "
import json,subprocess
for e in json.load(open('build/compile_commands.json')):
    if e['file'].endswith('lv_font_fmt_txt.c'):
        toks=e['command'].split(); out=[]; i=0
        while i<len(toks):
            t=toks[i]
            if t=='-c': out.append('-E -dM')
            elif t in ('-o','-MF','-MT'): i+=1
            elif t.startswith('-MMD'): pass
            else: out.append(t)
            i+=1
        p=subprocess.run(' '.join(out),shell=True,capture_output=True,text=True)
        print([l for l in p.stdout.splitlines() if 'LV_USE_FONT_COMPRESSED' in l])
        break"
```
输出 `#define LV_USE_FONT_COMPRESSED 0` → 宏真值实锤。
（坑：`-c` 必须按 **token** 精确替换；用 `str.replace('-c', ...)` 会污染 `-fdiagnostics-color`
之类的选项导致编译失败。）

2. 再换 `-E -H` 打印 header 树 → 树里**只有 `lv_conf_internal.h`，从未出现 `lv_conf.h`**，
   直接暴露 `LV_CONF_SKIP` 生效、用户配置被旁路。

3. 交叉验证：`grep CONFIG_LV_USE_FONT_COMPRESSED sdkconfig build/config/sdkconfig.h`。

### 15.7 通用教训

1. **LVGL 有两套配置来源，先确认用哪套**：`CONFIG_LV_CONF_SKIP=y` 时 **`lv_conf.h` 整个失效**，
   一切以 sdkconfig（Kconfig）为准。在 ESP-IDF + LVGL 项目里改配置前
   **先 `grep CONFIG_LV_CONF_SKIP sdkconfig`**，再决定改 `lv_conf.h` 还是 `sdkconfig`；
   "改了 `lv_conf.h` 没效果"的第一嫌疑就是这个。
2. **"配置改了没生效"优先怀疑编译单元没看到它**：用 15.6 的 `-E -dM` 直接问编译器，
   比反复读源码/加日志快一个数量级；`-E -H` 还能看出实际 include 的是哪一份同名头文件
   （本工程有三份 `lv_conf.h`）。
3. **崩溃点在渲染线程时，在 UI 线程"复现调用链"最有效**：按崩溃栈的同一组 API
   （`lv_font_get_glyph_dsc` → `lv_font_get_glyph_bitmap`）自己调一遍并打印返回值，
   能在不破坏系统的前提下拿到崩溃现场变量（`db` / `bm` / `box` / `gid`），把"猜测"变成"读数"。
4. **用"路径排除法"读返回值**：`db` 非空排除内存分配；`gid` / `box` 非零排除 cmap 与索引；
   `resolved_font` 排除 fallback 链异常 —— 排除一层、收敛一层。
   本 bug 第一版假说（cmap 查表错乱 / gid 越界）就是被"gid 与 box 都正常"这一读数推翻的。
5. **DRAM 只剩 ~74 KB 不等于内存是根因**。本次 `DRAM free=74895` 看着紧张，但 draw buffer 分配成功、
   崩在 NULL 解引用 → 内存只是背景噪声。看到"内存小"先验证能否分配成功，别急着查泄漏。
6. **`lv_font_conv` 的压缩属性要显式确认**：`bitmap_format=1`（压缩）必须配套
   `LV_USE_FONT_COMPRESSED=1`；`--no-compress` 生成的是 `0`，无需该宏。
   同一项目两种字库并存时，"只有某几个字库崩"就是最强的分流线索。
7. **子集字库 + fallback 是隐形炸弹**：子集外的字才走 fallback 全量字库，
   若两者压缩属性不同，只有出现子集外字符时才崩。排查务必确认 **fallback 目标字库**
   （`--lv-fallback` 指向谁）及其属性。
8. **临时探针必须可回收**：探针集中写进一个带"temp 诊断，定位后删除"注释的块，
   定位后用 `git diff HEAD -- <file>` 核对 —— 若该文件相对 HEAD 的 diff 恰好只有探针
   （本例 +135 行、无其他改动），`git checkout -- <file>` 即可原子还原，不留残渣。

### 15.8 验证清单（字体 / 渲染类改动后）

- [ ] 中英文混排首屏不崩（**子集内 + 子集外汉字都要测**，后者才会走 fallback）
- [ ] 探针日志确认 `bm != NULL` 且首字节有真实 alpha（不是全 0）
- [ ] `idf.py reconfigure` 后核对 `build/config/sdkconfig.h` 里出现新宏
- [ ] 改宏后**全量重编**（宏会改变 LVGL 全局结构体布局，增量编译容易留下不一致的 .o）
- [ ] 定位完成后 `git diff HEAD -- <探针文件>`，确认只剩探针后清理干净

---

## 十七、控制中心四项设置的 SD 快照 `/sdcard/history/cc/settings.txt`

### 17.1 存什么、为什么另开一份

亮度 / 蓝牙开关 / WiFi 开关 / 电源模式（beastmode·powersave·智能）四项，
`key=value` 文本另存一份到 SD 卡：

```text
brightness=42
bt=1
wifi=0
power=beastmode
```

**为什么要再存一份**：cfg.json 落在"已挂载卷"上，而无卡时那个卷是**内部
SPIFFS 兜底**。真卡上的快照让用户的选择能跟着卡走（换设备、重刷内部
分区都不丢），而且拔卡插读卡器就能看/改。

### 17.2 三个必须避开的坑

1. **无卡时绝不能崩、也不能悄悄改行为**。快照层在无真 SD 卡时**完全透明**：
   写是空操作（RAM/服务状态仍是权威），读返回"没有快照"，四个消费者继续
   走 cfg.json 原路径。所以每个调用点都可以无条件调用。
2. **"文件里没有这一行" ≠ "值是 0"**。结构体每个字段都配 `has_*` 标志位：
   只写了 `bt=1` 的文件，恢复时**不得**把亮度和 WiFi 重置成默认值。
   这是最容易写错的地方，也最容易在真机上表现为"改了一项、别的项被莫名重置"。
3. **深睡/关机是"整机断电"语义**。延迟写（deferred writer）可能还没落卡就
   断电了，所以快照必须走 `eos_storage_write_file_immediate()`。

### 17.3 落盘与恢复时机

| 时机 | 动作 | 位置 |
|---|---|---|
| CC 里拨开关 / 亮度松手 | 单键写 | `eos_control_center.c` 四个回调 |
| 退出省电模式 | 写 power + 复位 WiFi/BT | `eos_service_power_save.c` |
| 进/出性能模式 | 写 power | `eos_service_beast_mode.c` |
| 关机深睡 | 全量落盘（immediate） | `board_poweroff_enter_deep_sleep()` |
| L2 待机深睡 | 全量落盘（immediate） | `board_standby_enter_deep_sleep()` |
| **开机 / 深睡唤醒** | 全量恢复 | `app_main` 步骤 7.8，**早于 `eos_init()`** |

**恢复必须早于 `eos_init()`**，三条理由：
- `eos_service_config_init()` 会用 cfg.json 设一次亮度和蓝牙；
- CC widget 在 `eos_init()` 内创建，靠读服务状态决定开关位置；
- power_save / beast_mode 服务 init 时会把"当前 PM 配置"快照成 `_pm_normal_config`，
  此处先恢复模式，快照到的才是正确基线。

light sleep **不做任何处理**：CPU 还在跑，RAM/LVGL/服务状态全部保留，
没有"恢复"这回事。

### 17.4 app 侧的两个陷阱

- **`board_sd_is_real()` 曾被 `#if CONFIG_USB_MSC_APP_ENABLE` 整块包住**。
  这个 config 默认关，于是"是否真卡"的判断在普通构建里根本不存在。
  已把 `s_sd_is_real` 与 `board_sd_is_real()` 提到条件编译之外，
  挂载成功处无条件置位。
- **弱符号钩子**：服务侧声明 `bool board_sd_is_real(void) __attribute__((weak))`
  返回 false，板级提供强定义覆盖。这样模拟器/无板构建不需要额外改动就能链接。
  验证：`nm elenixos_esp32s3.elf | grep board_sd_is_real` 应为唯一的 `T`（强定义），
  不是 `W`。

### 17.5 验证清单

- [ ] 真卡：拨一次 WiFi 开关，`cat /sdcard/history/cc/settings.txt` 出现 `wifi=1`
- [ ] 无卡：开机日志出现 `Snapshot skipped: no real SD card`，四项设置仍按
      cfg.json 恢复（行为与改动前完全一致）
- [ ] 关机再开机 → 亮度/BT/WiFi/电源模式全部保持
- [ ] shell `cc show` 打印快照内容；`cc save` / `cc load` 手动落盘/恢复
- [ ] 手工把某一行删掉 → 重启后该项**保持原值**，不被重置成默认
- [ ] `nm` 确认 `board_sd_is_real` 是 `T` 而非 `W`（弱定义没被覆盖会永远返回 false）
- [ ] 快照文本的纯逻辑用离线单测跑过往返（尤其 `wifi=0` 这种"假值"不能丢）

---

## 十六、Album 双击放大倍数契约：9.3x → 4.0x（2026-09）

> 修正 §12.3：那节写的 `ALBUM_ZOOM_LEVELS[] = {256,384,512,768}` 档位缩放早已被
> 1:1 瓦片浏览（`_tile_*`）取代，该常量在代码里已不存在，不要再照着它改。

### 16.1 结论：倍数不是常量，是"编码长边 ÷ 150"

常态视图 `_fit_scale()` 把整图塞进 `ALBUM_IMG_MAX = 150` 的框，所以**整图长边永远
显示成 150px**；双击进 `_tile_enter()` 的 1:1 模式后瓦片不设缩放（默认 256），即
**1 编码像素 = 1 屏幕像素**。于是

    双击放大倍数 = meta.txt 里的编码长边 / 150

排查这类"手感不对"的问题，先 `cat .tiles/<名>/meta.txt`，第一个数 ÷ 150 就是答案。

### 16.2 为什么会到 9.3x

`enc_size_for_budget()` 的预算是 `1.2 × 原图文件大小`，而 RGB565 是 2 B/px、JPEG 才
~0.3 B/px，**RGB565 反而"比输入的 JPEG 还大"**。结果是原图越大、允许的编码分辨率
越高，双击跳得越狠：

| 输入 | 编码长边 | 改前倍数 | 1:1 一屏可见 |
|---|---|---|---|
| 2560×1440 / 2.2 MB | 1394 | **9.3x** | 17% 宽 |
| 1600×900 / 867 KB | 843 | 5.6x | 28% |
| 1024×576 / 354 KB | 538 | 3.6x | 45% |
| 640×360 / 139 KB | 512 | 3.4x | 47% |
| 480×270 / 79 KB | 480 | 3.2x | 50% |

同一批相册里 3.2x ~ 9.3x 都出现，且 1:1 时只能看到 17% 宽 —— 这就是"放太大"的来源。

### 16.3 修法：把倍数变成一个显式参数

脚本加 `--zoom`（默认 4.0），把预编码长边夹在 `--zoom × FIT_BOX_PX(150)`：

- 三条约束夹同一个长边，**取更严的那个**：`--zoom`（倍数上限，默认 600px）、
  `--budget`（thumb+tiles ≤ 1.2× 输入）、`--enc-min`（512px 质量下限）。
- `--zoom` 压过 `--enc-min`：两者冲突时以用户显式选的倍数为准。
- `--zoom > 6` 直接拒绝 —— 窗口最多驻留 4×4 块，编码再宽只会让每次平移多读瓦片。
- app **一行渲染逻辑都不用改**：`meta.txt` 记录的是真实尺寸，设备照它摆瓦片。
  想调倍数只改这一个数，重新跑脚本即可，不用重刷固件。

选 4.0x 的取舍（240px 圆屏）：一屏显示 40% 宽度、整图 3×2 = 6 块瓦片、
SD 占用 ~470 KB（1394px 时是 3.1 MB，6.6 倍）。3x 以下跳得不明显，
6x 以上每次平移要重读大量瓦片。

### 16.4 app 侧只动了三处

1. 角标 `"1:1"` → 真实倍数 `"4.0x"`。`"1:1"` 描述的是像素映射，用户想知道的是
   "放大几倍"，而预编码本来就是有意缩小过的，两者不是一回事。
   **用整数十分位拼字符串，不用 `%f`** —— newlib-nano 的浮点 printf 不保证可用。
2. `meta.txt` 来自 SD，属于不可信输入：原来只判 `> 0`，补上 `ts`/`ow`/`oh` 上界，
   否则 `rx * ow`、`c * s_ts` 会整型溢出。
3. 进入守卫 `ow <= 240 && oh <= 240` **保持原值不动**（≈1.6x），只加注释说明它
   等价于"放大不足 1.6x 就别跳"；脚本保证不会产出这种包，它只挡手工假包。

### 16.5 验证清单

- [ ] 重跑脚本，汇总行出现 `double-tap zoom: 4.0x .. 4.0x on the device`
- [ ] `.tiles/<名>/meta.txt` 第一个数 ÷ 150 **等于** 设备角标显示的数字
- [ ] 老包（编码 1394px）仍能显示，但角标会是 9.3x —— 要统一必须重跑脚本
- [ ] `--zoom 8` 必须报错退出；`--zoom 2` 必须只告警
- [ ] app 改完 `idf.py build` 无新增 warning（`eos_album.c:1646` 那条 `%s` 截断是既有问题）
