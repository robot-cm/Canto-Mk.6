# ElenixOS Watch OS UI 框架 — 架构设计

> 版本：v1.0（设计稿）｜日期：2026-08-15
> 目标：为 240×240 圆屏智能手表建立一套**可长期扩展**的统一 UI 框架，替代当前"固定坐标 + 全局对象 + 页面残留"的临时实现。
> 原则：**不写临时代码、不建大量全局 lv_obj_t、不用固定坐标、每个 App 完全独立、切换即彻底清理。**

---

## 0. 设计目标（对应需求逐条）

| 需求 | 设计落点 |
|---|---|
| 保留网页设计语言（圆角卡片/半透明玻璃/简洁文字/平滑动画/现代手表风格） | §1 Design Tokens + §2 System UI |
| System Layer：状态栏 / 后台 App 图标 / 通知层 / 全局动画层 | §2.2 System Layer（4 个常驻模块） |
| App Manager：生命周期 / 打开 / 关闭 / 切换动画 / 防残留 | §2.3 App Manager + §4 状态机 + §8 防残留 |
| 每个 App 独立：root container / UI 对象 / event / timer | §2.4 App 实例抽象（eos_wos_app_t） |
| 切换自动清理旧页面，不允许 LVGL 对象残留、多页面叠加 | §4 生命周期 + §8 清理审计 |
| 布局优先 flex / grid / container，不大量用固定坐标 | §5 布局策略 |
| 动画：开 App 缩放+淡入、关 App 缩小退出、页面左右滑、控制中心下拉、后台顶部小图标 | §6 动画规范 |
| 分步实施（先架构 → 基础框架 → Clock → Timer → Control Center） | §9 实施计划 |

---

## 1. Design Tokens（设计语言）

统一在 `eos_wos_theme` 中定义，禁止各 App 私自写颜色/圆角/字号。

### 1.1 色彩
```
--wos-bg            #000000        (深空底，OLED)
--wos-card          #1A1E26  @ 88% (玻璃卡片底色)
--wos-card-border   #FFFFFF  @ 12% (玻璃高光描边)
--wos-text-primary  #F5F0F0
--wos-text-secondary#8A8F98
--wos-accent        #B0949E        (莫奈紫，可随主题切换)
--wos-accent-alt    #7AD0FF        (极光蓝)
```

### 1.2 玻璃感（Glass Card）
一个圆角卡片 = 半透明底 + 1px 高光描边 + 微内阴影，封装成单一样式函数：
```c
void wos_style_glass_card(lv_obj_t *card, int radius, lv_opa_t tint);
// radius 默认 16；tint 默认 88%；描边 #FFFFFF @12%
// 内部: bg_color=card, bg_opa=tint, border_width=1,
//       border_opa=12%, shadow 可选 (LVGL 9 shadow 开销大, 默认关)
```

### 1.3 圆角 / 字号 / 间距 token
| Token | 值 | 用途 |
|---|---|---|
| radius-card | 16 | 卡片 |
| radius-pill | 999 | 胶囊按钮/指示条 |
| font-xxl | 28 | 大数字（计时） |
| font-xl | 20 | 标题 |
| font-md | 14 | 正文 |
| font-sm | 11 | 辅助/状态栏 |
| pad-card | 12 | 卡片内边距 |
| gap-row | 8 | 行内间距 |

### 1.4 动效 token（见 §6）
时长统一 150–300ms，缓动统一 ease-out（进入）/ ease-in（退出）。

---

## 2. 系统架构总览

### 2.1 屏幕层级（自底向上，基于现有 `eos_overlay_layer` 扩展）

```
┌────────────────────────────────────────────┐
│  L6  SystemOverlay   控制中心 / Dialog / Toast │  (eos_wos_overlay)
│  L5  GlobalAnimLayer 全局动画（切换快照）      │  (eos_wos_anim_layer)
│  L4  BgIndicator     后台运行 App 图标（顶部）  │  (eos_wos_bg_indicator)
│  L3  StatusBar       状态栏（时间/电量）        │  (eos_wos_statusbar)  ← 常驻，所有页面之上
│  L2  Notification    通知层（悬浮卡片）         │  (eos_wos_notification)
│  L1  AppLayer        当前 App 唯一 root        │  (App Manager 持有)
│  L0  WatchFaceLayer  表盘                     │  (root activity)
└────────────────────────────────────────────┘
  所有层挂 lv_layer_top()，由 eos_overlay_layer 统一建层（现有 5 层扩展为 6 层）
```

### 2.2 System Layer（常驻，不随 App 销毁）

| 模块 | 职责 | 关键接口 |
|---|---|---|
| **StatusBar** | 顶部 24px：左=App 标题，右=时间/电量小字；半透明玻璃底 | `wos_statusbar_set_title/update_clock` |
| **BgIndicator** | 顶部中央 20px 圆形后台 App 图标，点击恢复 | `wos_bg_indicator_register/unregister`（现有 eos_bg_indicator 迁入） |
| **Notification** | 通知到达时顶部滑入卡片，3s 自动消失 | `wos_notification_show(title, body)` |
| **AnimLayer** | 页面切换的过渡快照层（旧页截图淡出） | `wos_anim_layer_snapshot(obj)` |

### 2.3 App Manager（唯一入口，负责一切页面切换）

```
eos_wos_app_manager
├── wos_app_manager_open(app_id)      # 关闭当前 → 建新 root → 缩放+淡入
├── wos_app_manager_close()           # 缩小淡出 → 递归删除 → 残留审计
├── wos_app_manager_switch(app_id)    # 左右滑动过渡切换
├── wos_app_manager_get_active()      # 当前活跃 app
├── wos_app_manager_register(desc)    # App 注册表（id + 构建/销毁回调）
└── 内部：_cleanup_app()              # 彻底清理（§8）
```

**硬约束：任何时刻最多 1 个 App 页面可见。** `open` 前强制 `close` 当前。

### 2.4 App 实例抽象（每个 App 独立的世界）

```c
typedef struct {
    const char *id;                       // "clock" / "timer" / ...
    /* 生命周期（App 必须实现） */
    void (*build)(eos_wos_app_t *app);    // 在自己的 root 上建 UI
    void (*destroy)(eos_wos_app_t *app);  // 释放 JS/C 资源
} eos_wos_app_desc_t;

typedef struct {
    lv_obj_t *root;          // App 专属容器（flex column，全屏安全区）
    const eos_wos_app_desc_t *desc;
    void *user_data;         // 状态（如 JS program / C 状态机）
    /* 独立资源账本：本 app 创建的 timer / anim 全部登记，销毁时统一释放 */
    lv_timer_t *timers[8];  uint8_t timer_cnt;
    lv_anim_t  *anims[8];   uint8_t anim_cnt;
} eos_wos_app_t;
```

每个 App：`root` 内自建 UI、自己的 event 回调、自己的 timer（登记到账本）→ 切换时一次性销毁，**不残留**。

### 2.5 模块关系图

```
输入(触摸/手势/按键)                        Service 层
   │                                          │
   ▼                                          ▼
App Manager ──路由──► 当前 eos_wos_app_t   NotificationService/PM/...
   │  ▲                    │ build/destroy      │
   │  │                    ▼                    ▼
   │  └──── root(container) ◄──── UI 对象      NotificationLayer
   ▼                                        StatusBar/BgIndicator
System Layer (常驻) ◄────────────── 事件订阅 (EOS_EVENT_*)
```

---

## 3. 文件结构（目标布局，新增 `src/ui/wos/`）

```
src/ui/wos/
├── eos_wos.h                        # 总头文件（一行 include 全部）
├── core/
│   ├── eos_wos_theme.h/.c           # Design Tokens + glass 样式函数
│   ├── eos_wos_app.h/.c             # App 实例抽象（root/账本）
│   └── eos_wos_layout.h/.c          # flex/grid 布局助手（快捷容器）
├── manager/
│   ├── eos_wos_app_manager.h/.c     # 生命周期/注册表/清理审计
│   └── eos_wos_transition.h/.c      # 开/关/切换动画（缩放/淡入/滑动）
├── system/
│   ├── eos_wos_statusbar.h/.c       # 状态栏
│   ├── eos_wos_notification.h/.c    # 通知层
│   └── eos_wos_bg_indicator.h/.c    # 后台图标（迁入现有 eos_bg_indicator）
├── overlay/
│   └── eos_wos_control_center.h/.c  # 控制中心（下拉，替换现有 CC）
└── apps/                            # 每个 App 一个子目录，纯注册
    ├── clock/   (eos_wos_app_clock.c)
    ├── timer/   (eos_wos_app_timer.c)
    └── alarm/   (eos_wos_app_alarm.c)
```

> JS 脚本 App（clock/timer/alarm 现有 .eapk）如何纳入？
> `eos_wos_app.c` 提供一个 "JS 适配 App"：build = 调 `spm_app_run(&pkg)`，destroy = `spm_app_stop()`，
> root = `eos_activity_get_view(activity)`。JS 侧不用改，SPM/activity 生命周期由框架转发。
> 这样 C App 与 JS App 在 App Manager 里统一调度。

---

## 4. App 生命周期状态机

```
            wos_app_manager_open(app_id)
  IDLE ────────────────────────────► LAUNCHING ──(scale+fade 完成)──► ACTIVE
   ▲                                       │                            │
   │                                       │ close()                    │ close()
   │                                       ▼                            ▼
   └───────────── DESTROYED ◄────────── CLOSING ◄─────────────────── 动画中
                    │                   (缩小淡出完成)
                    │  wos_cleanup_audit(): root 子对象数必须==0
                    ▼
                彻底释放（user_data/timers/anims）
```

| 状态 | 行为 |
|---|---|
| IDLE | 无 App |
| LAUNCHING | 创建 root → 注册表移除旧 → build() → 缩放 0.85→1 + 淡入 0→255（200ms）→ ACTIVE |
| ACTIVE | 正常交互；事件/timer 属于本 app |
| CLOSING | 缩小 1→0.85 + 淡出（150ms）→ 期间不再响应输入 |
| DESTROYED | 递归删 root 全部子对象 → 删 timers/anims 账本 → user_data destroy() → 审计 → IDLE |

---

## 5. 布局策略（禁止固定坐标）

### 5.1 强制规则
1. **禁止** `lv_obj_set_pos(x, y)` 布局页面结构（仅允许小偏移微调，如 `-2` 对齐）。
2. 页面骨架一律 **flex / grid / align**：
   - 整页 root：`flex column`，子项 `flex_grow` 自动分配
   - 图标矩阵：`lv_obj_set_grid_dsc_array` + `grid_align`
   - 单元素定位：`align(ALIGN_TOP_MID / BOTTOM_MID / CENTER)` + 小偏移
3. 内边距/间距用 token（§1.3），不用魔法数字。

### 5.2 通用容器（eos_wos_layout 提供）
```c
lv_obj_t *wos_row(lv_obj_t *parent);        // flex row, gap 8
lv_obj_t *wos_column(lv_obj_t *parent);     // flex column, gap 8
lv_obj_t *wos_card(lv_obj_t *parent);       // 玻璃圆角卡片（§1.2）
lv_obj_t *wos_grid(parent, cols, gap);      // grid 容器
void wos_make_safe_area(lv_obj_t *root);    // 依据 display profile 预留 notch/圆边
```

### 5.3 分辨率适配
- 240×240 是当前目标；布局用 `lv_pct()` / flex_grow，换屏尺寸自动缩放。
- 圆屏：`wos_make_safe_area` 用 `eos_display_profiles_get(EOS_PROFILE_240C).safe_*` 计算内容安全区，保证控件不越出圆形可视区。

---

## 6. 动画规范（统一由 eos_wos_transition 提供）

| 场景 | 动画 | 参数 |
|---|---|---|
| 打开 App | scale 0.85→1.0 + opa 0→255（同轴） | 200ms, ease_out |
| 关闭 App | scale 1.0→0.85 + opa 255→0 | 150ms, ease_in |
| 页面左右切换 | 旧页 slide -100% + 新页 slide +100%→0 | 200ms, ease_out |
| 控制中心 | 下拉：translate_y -100% → 0 | 250ms, ease_out |
| 后台图标出现 | 缩放 0.5→1 + 淡入 | 150ms, ease_out |
| 通知滑入 | translate_y -30 → 0 + 淡入 | 200ms |

```c
void wos_transition_open(lv_obj_t *root);     // 缩放+淡入
void wos_transition_close(lv_obj_t *root, lv_anim_ready_cb_t on_done); // 缩小+淡出
void wos_transition_slide(lv_obj_t *from, lv_obj_t *to, lv_dir_t dir);
void wos_transition_pull_down(lv_obj_t *panel); // 控制中心下拉
```

> 注意：不要对带 `round_clip`（border-radius + clip_corner）的 view 做 transform_scale
> （LVGL 9 已知渲染 bug，见历史记录）—— 打开动画改在 **App root 的容器**上做，
> 外层圆屏裁剪保持静态。这是从既往事故中提炼的硬约束。

---

## 7. 数据流

```
触摸/手势 ──► indev 事件 ──► App Manager 路由
                                 ├─ App 内部点击 → 该 App 自己的 event handler
                                 ├─ 左滑返回   → wos_app_manager_close()
                                 └─ 下拉       → Control Center (overlay)

后台驻留   ──► App Manager ──► BgIndicator.register(app_id, icon)
恢复前台   ──► 点 indicator ──► wos_app_manager_open(app_id) (resume)

通知       ──► NotificationService ──► NotificationLayer.show(card)
时间/电量  ──► StatusBar.update_clock/update_battery (1 分钟 timer 常驻)
```

---

## 8. 防残留机制（硬性保证）

1. **唯一活动指针**：App Manager 持有 `active`，`open()` 先 `close()`。
2. **递归删除**：CLOSING 完成后 `lv_obj_clean(app->root)`（删所有子对象）再 `lv_obj_delete(app->root)`。
3. **资源账本**：App 的 timers/anims 登记在 `eos_wos_app_t` 数组，销毁时全部 `lv_timer_delete` / `lv_anim_del`。
4. **残留审计（开发期）**：destroy 后断言 `lv_obj_get_child_cnt(root) == 0` 且账本全空，否则 `EOS_LOG_E("wos: object leak in app %s")` —— 编译期 DEBUG 开启。
5. **JS App 对齐**：JS App 的 root = activity view，destroy = `spm_app_stop()`（JS realm + 资源一并释放），与 C App 同一套清理流程。

---

## 9. 分步实施计划（按你的流程，不一次生成）

| Step | 内容 | 产出 | 依赖 |
|---|---|---|---|
| **1** | ✅ 本架构设计文档 | 本文件 | — |
| **2** | 基础框架：`core/` + `manager/` + `system/` 骨架 + overlay 扩展 6 层 + 清理审计 | 可编译空框架，shell `wos` 命令可 open/close 一个空 app | 本设计 |
| **3** | Clock App（JS 适配版 + C 版二选一，按你定的）：flex/卡片/玻璃风格重写 | 干净时钟页 | Step2 |
| **4** | Timer App | 倒计时/秒表 | Step2 |
| **5** | Control Center（下拉动画 + 玻璃卡片按钮） | 下拉控制中心 | Step2 |
| **6**（后续） | 迁移 alarm / settings / 通知层 / 状态栏电量 | 全部页面统一 | Step3-5 |

> 每一步独立可验证（编译 + 真窗口截图），完成一步确认一步，不再出现"一次写一堆然后返工"。

---

## 10. 与现有代码的关系（复用 vs 替换）

| 现有模块 | 处理 |
|---|---|
| `eos_overlay_layer`（5 层） | **复用**，扩展为 6 层（加 NotificationLayer） |
| `eos_bg_indicator` | **迁入** `system/eos_wos_bg_indicator` |
| `eos_round_clip` / `eos_display_profiles` | **复用**（safe area 计算） |
| `eos_chrome_manager` / `eos_control_center` | **替换**为 wos overlay（保留 API 兼容壳） |
| `bubble_grid`（固定坐标 app 页） | **替换**为 App Manager + flex 布局 app 页 |
| `eos_app_header` back 按钮 | **已隐藏**（左滑退出已可用），新框架不再画 back 按钮 |
| SPM / eos.activity（JS App） | **复用**，经 JS 适配 App 纳入统一调度 |
| `spm_suspend/resume` + background 机制 | **复用**（后台驻留/指示器已工作） |

---

## 11. 验收标准（每个 Step 完成后）

- [ ] 无 `lv_obj_set_pos` 页面骨架调用（允许 ±4px 微调）
- [ ] open/close 100 次，`lv_obj_get_child_cnt` 泄漏为 0
- [ ] 任一时刻截图只有 1 个 App 页面 + 常驻 System Layer
- [ ] 240×240 圆屏内控件不越界、文字不重叠
- [ ] 打开/关闭/切换/下拉均有动画且无残影
- [ ] JS App 与 C App 在同一 App Manager 下行为一致
