# Canto Mk.6 UI Framework 设计计划表

> 目标：先搭一个**自适应 UI Framework**（不是单个页面），未来所有 App（表盘/天气/音乐/闹钟/设置/小游戏）都跑在同一套布局系统上。
> 硬件：ESP32-S3 + 240×240 圆屏，LVGL v9，性能有限 → 不用实时模糊/3D GPU/复杂粒子，目标 60FPS 稳定流畅。

---

## 0. 设计原则（硬性约束）

1. **禁止写死坐标**：组件只声明 `anchor / constraint / percentage / dp`，物理像素由 LayoutManager 在运行时算出。
2. **禁止矩形裁剪凑圆屏**：圆屏用极坐标（中心 + 半径）布局，元素必须在 Safe Area 内，越界即拉回，不裁剪隐藏。
3. **多形同码**：同一 App 在 `CIRCLE / SQUARE / RECTANGLE` 下由同一套组件 + 不同 LayoutManager 渲染，App 不感知屏幕形状。
4. **逻辑单位 dp**：所有尺寸用 `dp`，按参考宽 240 缩放；`px = dp × (screen_w / 240)`。
5. **性能预算**：动画只用 opacity / transform_scale / translate（LVGL 合成层），玻璃风只用半透明+渐变+高光+阴影，不用逐帧模糊。

---

## 1. 模块地图（Layers）

| 层 | 模块 | 路径 | 职责 | 依赖 |
|---|---|---|---|---|
| L1 | Display Adaptation | `display/eos_display_profile` | DisplayProfile（shape/center/radius/safe_area/aspect）、dp 缩放、安全区 clamp | 无（纯 C） |
| L1 | Display Profiles | `display/eos_display_profiles` | 4 个测试 Profile 注册表 | profile |
| L2 | Layout Engine | `layout/eos_layout` | LayoutManager 基类 + 工厂（按 shape 选）、Anchor/Constraint、CommonWidget | profile |
| L2 | CircleLayout | `layout/eos_layout_circle` | 极坐标 / 半径定位 / Arc 排列 | layout, profile |
| L2 | SquareLayout | `layout/eos_layout_square` | Grid / Flex + 边缘 padding | layout, profile |
| L3 | Physics | `physics/eos_physics` | 惯性滚动器：velocity / friction / spring / bounce | 无（纯 C） |
| L4 | Animation | `anim/eos_anim` | 缓动曲线（纯数学）+ LVGL 页面切换 Fade/Scale | profile, lvgl |
| L5 | ArcList | `applist/eos_arclist` | 华为弧形 App 列表（弧形排布 + 缩放/透明度 + 惯性 + 回弹） | layout, physics, profile |
| L6 | AppManager | `personalize/eos_app_manager` | App 注册 / 排序 / 拖动重排 | 无 |
| L6 | LayoutStorage | `personalize/eos_layout_storage` | 布局 + App 顺序持久化（JSON，复用 eos_storage） | storage |
| L6 | ThemeManager | `personalize/eos_theme_manager` | Liquid Glass 主题参数 + 应用 + 持久化 | lvgl, storage |
| L7 | Liquid Glass | `style/eos_liquid_glass` | 玻璃卡 style 辅助（半透明/渐变/高光/阴影/圆角/微光） | lvgl |

---

## 2. 分阶段执行计划

| 阶段 | 交付物 | 关键文件 | 验证方式 |
|---|---|---|---|
| **P1 基础** | DisplayProfile + 4 Profile + dp + clamp | `display/*` | 4 屏：safe 区半径、dp 缩放、图标中心被 clamp 到安全区 |
| **P2 布局引擎** | LayoutManager 工厂 + Anchor/Constraint + CommonWidget + Circle/Square | `layout/*` | 圆屏极坐标无越界；方屏 Grid 在 padding 内 |
| **P3 物理** | 惯性滚动器（摩擦/弹簧/回弹） | `physics/*` | 喂入 flick 速度 → 衰减到静止且不超过边界 |
| **P4 动画** | 缓动曲线 + 页面 Fade/Scale | `anim/*` | 缓动端点/单调性；LVGL 切换淡入缩放 |
| **P5 ArcList** | 弧形 App 列表几何 | `applist/*` | N 个 App 沿弧排布，中心最大最亮，远处缩小变淡，全在安全区 |
| **P6 个性化** | AppManager + LayoutStorage + ThemeManager | `personalize/*` | 顺序重排持久化往返一致；主题切换生效 |
| **P7 玻璃风** | Liquid Glass 卡 style | `style/*` | 创建玻璃卡 style 不报错 |
| **P8 集成** | 模拟器 `--ui-framework-test` | `simulator/main.c` | 无头跑 4 屏全检查，退出码 0 |

---

## 3. 四屏测试矩阵（必须全部满足）

| 目标 | shape | w×h | Safe Area | 布局策略 |
|---|---|---|---|---|
| 小圆屏 | CIRCLE | 240×240 | 内切圆 r≈110（留 8% 边） | ArcList + 极坐标 |
| 大圆屏 | CIRCLE | 320×320 | 内切圆 r≈147 | 同上，dp 自动放大 |
| 方屏 | SQUARE | 240×280 | 矩形 inset padding | Grid 2×2，信息更密 |
| 矩形屏 | RECTANGLE | 240×320 | 矩形 inset padding | Grid + 纵向扩展 |

**验收**：无越界、无遮挡、动画正常、点击区域正确（命中盒在安全区内）。

---

## 4. 关键公式

- **dp→px**：`px = dp × (width / 240.0)`；参考宽度 `REF_W = 240`。
- **圆屏 Safe**：`center=(w/2,h/2)`，`R=min(w,h)/2`，`safe_R = R × 0.92`。
- **圆形 clamp**：若 `dist(center, p) + r > safe_R` → 沿径向把 `p` 拉回到 `safe_R - r` 处。
- **极坐标**：`x = cx + R·cos(a)`，`y = cy + R·sin(a)`（y 向下，a 单位弧度）。
- **ArcList 几何**：`angle_i = focus + (i·step − offset)`；`Δ = |angle_i − focus|`；
  `scale_i = clamp(1.25 − k·Δ, 0.5, 1.25)`，`opacity_i = clamp(1 − k2·Δ, 0.15, 1)`；`visible` 当 `Δ < 170°`。
- **物理**：拖动跟手（pos=手指）；松手 `v` 衰减 `v *= friction^dt`；越界 `a = −k·(pos−bound) − c·v`（弹簧+阻尼）。
- **缓动**：`ease_out_back(x)=1+2.7·(x−1)³+1.7·(x−1)²`（带回弹过冲）；`ease_in_out_cubic`。

---

## 5. 与现有代码关系

- 不删除现有 `eos_launcher` / `eos_round_clip`，新框架作为**基础层**供未来 App 接入；ArcList 是新的 App 列表 UI 候选。
- 持久化复用 `eos_storage_write_file / eos_storage_read_file`（与 Gallery/Files 同源）。
- 新文件落在 `src/ui/framework/`，根 CMake 自动 glob 编译，需**重新 configure**。
- 头文件目录自动加入 include，无需改 CMake。

---

## 6. 开放项 / 后续

- 现有 Launcher 是否整体迁移到 ArcList + LayoutManager（P8 之后评估）。
- 真机 ESP-IDF 板级移植（GC9A01/CST816/SD/RTC）仍属独立 task #5，本框架不依赖。
- 表盘/天气/音乐等 App 的具体 UI 在框架上二次开发（本计划只交付框架 + ArcList 演示）。
