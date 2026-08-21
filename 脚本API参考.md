# ElenixOS 脚本 API 参考

> 本文档列出 JerryScript 脚本引擎（Script Engine / SNI）暴露给 **JS 应用与表盘** 的全部可调用接口。
> 所有 API 通过两个全局命名空间访问：`eos.*`、`lv.*`。

---

## 一、API 总览

| 命名空间 | 挂载名 | 用途 | 内容 |
|---|---|---|---|
| **`eos.*`** | `eos` | ElenixOS 系统 API | 8 个静态类 + 常量 |
| **`lv.*`** | `lv` | LVGL 图形库绑定 | 17 个类（组件 + 基础对象 + 动画/定时器） |

---

## 二、`eos.*` 系统 API

`eos` 命名空间下的类全部是**静态类**（无需 `new`，直接 `eos.类名.方法()`）。

### 2.1 `eos.view`
| 方法 | 说明 |
|---|---|
| `eos.view.active()` | 获取当前活动视图（`lv.obj`） |

### 2.2 `eos.console`
| 方法 | 说明 |
|---|---|
| `eos.console.log(...)` | 打印日志 |
| `eos.console.error(...)` | 错误日志 |
| `eos.console.warn(...)` | 警告日志 |
| `eos.console.info(...)` | 信息日志 |
| `eos.console.debug(...)` | 调试日志 |

### 2.3 `eos.config`
键值存储（字符串 / 布尔 / 数字）。
| 方法 | 说明 |
|---|---|
| `eos.config.setStr(key, val)` | 写字符串配置 |
| `eos.config.setBool(key, val)` | 写布尔配置 |
| `eos.config.setNumber(key, val)` | 写数字配置 |
| `eos.config.getStr(key)` | 读字符串配置 |
| `eos.config.getBool(key)` | 读布尔配置 |
| `eos.config.getNumber(key)` | 读数字配置 |

### 2.4 `eos.time`
| 方法 | 说明 |
|---|---|
| `eos.time.getNow()` | 获取当前时间 |

### 2.5 `eos.appHeader`
应用页顶部标题栏。
| 方法 | 说明 |
|---|---|
| `eos.appHeader.setTitle(text)` | 设置标题 |
| `eos.appHeader.show()` | 显示标题栏 |
| `eos.appHeader.hide()` | 隐藏标题栏 |

### 2.6 `eos.clockHand`
表盘指针。
| 方法 | 说明 |
|---|---|
| `eos.clockHand.create(...)` | 创建指针 |
| `eos.clockHand.center(...)` | 设置指针中心 |
| `eos.clockHand.placePivot(...)` | 设置旋转轴心 |
| `eos.clockHand.attach(...)` | 挂载指针 |
| `eos.clockHand.centerStyle(...)` | 中心点样式 |

### 2.7 `eos.activity`
Activity 导航（应用/页面切换）。
| 方法 | 说明 |
|---|---|
| `eos.activity.current()` | 当前活动 |
| `eos.activity.visible()` | 当前可见活动 |
| `eos.activity.bottom()` | 栈底活动 |
| `eos.activity.watchface()` | 表盘活动（根） |
| `eos.activity.rootScreen()` | 根屏幕对象 |
| `eos.activity.getView()` / `setView(v)` | 读写活动视图 |
| `eos.activity.getTitle()` / `setTitle(t)` | 读写活动标题 |
| `eos.activity.getType()` / `setType(t)` | 读写活动类型 |
| `eos.activity.setAppHeaderVisible(bool)` | 设置标题栏可见 |
| `eos.activity.isAppHeaderVisible()` | 标题栏是否可见 |
| `eos.activity.enter(...)` | 进入活动 |
| `eos.activity.back()` | 返回上一活动 |
| `eos.activity.isTransitionInProgress()` | 是否正在转场动画 |

### 2.8 `eos.permission`
| 方法 | 说明 |
|---|---|
| `eos.permission.request(...)` | 申请权限 |
| `eos.permission.check(...)` | 检查权限 |

### 2.9 `eos` 常量
| 常量 | 含义 |
|---|---|
| `eos.FONT_SIZE_LARGE` / `FONT_SIZE_MEDIUM` / `FONT_SIZE_SMALL` | 字号 |
| `eos.DISPLAY_WIDTH` / `DISPLAY_HEIGHT` | 屏幕宽高 |
| `eos.CLOCK_HAND_HOUR` / `MINUTE` / `SECOND` | 指针类型 |
| `eos.ACTIVITY_TYPE_NULL` / `APP` / `APP_LIST` / `WATCHFACE` / `WATCHFACE_LIST` | 活动类型 |

---

## 三、`lv.*` LVGL 绑定

`lv` 命名空间暴露 LVGL 对象模型。组件类通过构造函数创建：`new lv.obj()`、`lv.button()` 等。

### 3.1 类清单（17 个）

| 类 | 构造函数 | 继承 | 说明 |
|---|---|---|---|
| `lv.obj` | `new lv.obj(parent)` | — | 基础对象（382 个方法，见 3.2） |
| `lv.button` | `lv.button(parent)` | obj | 按钮 |
| `lv.label` | `lv.label(parent)` | obj | 文本标签 |
| `lv.arc` | `lv.arc(parent)` | obj | 圆弧 |
| `lv.bar` | `lv.bar(parent)` | obj | 进度条 |
| `lv.buttonmatrix` | `lv.buttonmatrix(parent)` | obj | 按钮矩阵 |
| `lv.calendar` | `lv.calendar(parent)` | obj | 日历 |
| `lv.chart` | `lv.chart(parent)` | obj | 图表 |
| `lv.canvas` | `lv.canvas(parent)` | obj | 画布 |
| `lv.checkbox` | `lv.checkbox(parent)` | obj | 复选框 |
| `lv.dropdown` | `lv.dropdown(parent)` | obj | 下拉框 |
| `lv.image` | `lv.image(parent)` | obj | 图片 |
| `lv.imagebutton` | `lv.imagebutton(parent)` | obj | 图片按钮 |
| `lv.screen` | —（静态类） | — | 屏幕：`lv.screen.active()` |
| `lv.color` | —（静态类） | — | 颜色：`lv.color.hex("#RRGGBB")` |
| `lv.timer` | `lv.timer(cb, period)` | — | 定时器 |
| `lv.anim` | `lv.anim()` | — | 动画 |

### 3.2 `lv.obj` 基础方法（382 个，按功能分组）

#### 创建 / 删除
`create`, `delete`, `clean`, `deleteDelayed`, `deleteAsync`

#### 树结构 / 父子关系
`setParent`, `swap`, `moveToIndex`, `getScreen`, `getParent`, `getChild`, `getChildByType`,
`getSibling`, `getSiblingByType`, `getChildCount`, `getChildCountByType`, `getIndex`, `getIndexByType`,
`treeWalk`, `dumpTree`, `moveForeground`, `moveBackground`

#### 位置 / 尺寸
`setPos`, `setX`, `setY`, `setSize`, `refrSize`, `setWidth`, `setHeight`, `setContentWidth`, `setContentHeight`,
`setLayout`, `isLayoutPositioned`, `markLayoutAsDirty`, `updateLayout`,
`getCoords`, `getX`, `getX2`, `getY`, `getY2`, `getXAligned`, `getYAligned`, `getWidth`, `getHeight`,
`getContentWidth`, `getContentHeight`, `getContentCoords`, `getSelfWidth`, `getSelfHeight`,
`refreshSelfSize`, `refrPos`, `moveTo`, `moveChildrenBy`

#### 对齐
`setAlign`, `align`, `alignTo`, `center`

#### 变换 / 命中
`transformPoint`, `transformPointArray`, `getTransformedArea`, `invalidateArea`, `invalidate`,
`areaIsVisible`, `isVisible`, `setExtClickArea`, `getClickArea`, `hitTest`

#### 滚动
`setScrollbarMode`, `setScrollDir`, `setScrollSnapX`, `setScrollSnapY`,
`getScrollbarMode`, `getScrollDir`, `getScrollSnapX`, `getScrollSnapY`,
`getScrollX`, `getScrollY`, `getScrollTop`, `getScrollBottom`, `getScrollLeft`, `getScrollRight`, `getScrollEnd`,
`scrollBy`, `scrollByBounded`, `scrollTo`, `scrollToX`, `scrollToY`, `scrollToView`, `scrollToViewRecursive`,
`isScrolling`, `updateSnap`, `getScrollbarArea`, `scrollbarInvalidate`, `readjustScroll`

#### 样式（见 3.4）
`addStyle`, `replaceStyle`, `removeStyle`, `removeStyleAll`, `refreshStyle`,
`hasStyleProp`, `setLocalStyleProp`, `getLocalStyleProp`, `removeLocalStyleProp`,
`fadeIn`, `fadeOut`，以及 `getStyleXxx` / `setStyleXxx` 全套（约 220 个）

#### 事件
`sendEvent`, `addEventCb`, `getEventCount`, `removeEvent`, `removeEventCb`, `removeEventDsc`, `removeEventCbWithUserData`

#### Flag / State / 数据
`addFlag`, `removeFlag`, `updateFlag`, `addState`, `removeState`, `setState`, `setUserData`,
`hasFlag`, `hasFlagAny`, `getState`, `hasState`, `getUserData`

#### 其他
`allocateSpecAttr`, `checkType`, `hasClass`, `isValid`, `nullOnDelete`, `removeFromSubject`, `setFontSize`,
`classInitObj`, `isEditable`, `isGroupDef`

### 3.3 各组件类特有方法

#### `lv.label`（13）
`setText`, `setLongMode`, `setTextSelectionStart`, `setTextSelectionEnd`, `getText`, `getLongMode`,
`getLetterPos`, `getLetterOn`, `isCharUnderPos`, `getTextSelectionStart`, `getTextSelectionEnd`, `insText`, `cutText`

#### `lv.bar`（12）
`setValue`, `setStartValue`, `setRange`, `setMode`, `setOrientation`, `getValue`, `getStartValue`,
`getMinValue`, `getMaxValue`, `getMode`, `getOrientation`, `isSymmetrical`

#### `lv.buttonmatrix`（13）
`setMap`, `setCtrlMap`, `setSelectedButton`, `setButtonCtrl`, `clearButtonCtrl`, `setButtonCtrlAll`,
`clearButtonCtrlAll`, `setButtonWidth`, `setOneChecked`, `getSelectedButton`, `getButtonText`, `hasButtonCtrl`, `getOneChecked`

#### `lv.dropdown`（21）
`setText`, `setOptions`, `setOptionsStatic`, `addOption`, `clearOptions`, `setSelected`, `setDir`, `setSymbol`,
`setSelectedHighlight`, `getList`, `getText`, `getOptions`, `getSelected`, `getOptionCount`, `getSelectedStr`,
`getOptionIndex`, `getSymbol`, `getSelectedHighlight`, `getDir`, `close`, `isOpen`

#### `lv.image`（24）
`create`, `setSrc`, `setOffsetX`, `setOffsetY`, `setRotation`, `setPivot`, `setScale`, `setScaleX`, `setScaleY`,
`setBlendMode`, `setAntialias`, `setInnerAlign`, `setBitmapMapSrc`, `getSrc`, `getOffsetX`, `getOffsetY`,
`getRotation`, `getPivot`, `getScale`, `getScaleX`, `getScaleY`, `getBlendMode`, `getAntialias`, `getInnerAlign`

#### `lv.chart`（20）
`setType`, `setPointCount`, `setRange`, `setUpdateMode`, `setDivLineCount`, `getType`, `getPointCount`,
`getXStartPoint`, `refresh`, `addSeries`, `removeSeries`, `setXStartPoint`, `addCursor`, `setAllValue`,
`setNextValue`, `setNextValue2`, `setExtYArray`, `setExtXArray`, `getPressedPoint`, `getFirstPointCenterOffset`

#### `lv.canvas`（10）
`setBuffer`, `setDrawBuf`, `setPx`, `setPalette`, `getDrawBuf`, `getPx`, `copyBuf`, `fillBg`, `initBuffer`, `freeBuffer`

#### `lv.imagebutton`（6）
`create`, `setSrc`, `setState`, `getSrcLeft`, `getSrcMiddle`, `getSrcRight`

#### `lv.calendar`（5）
`setTodayDate`, `setShowedDate`, `setHighlightedDates`, `setDayNames`, `getBtnmatrix`

#### `lv.checkbox`（2）
`setText`, `getText`

#### `lv.timer`（10）
`delete`, `pause`, `resume`, `setCb`, `setPeriod`, `ready`, `setRepeatCount`, `reset`, `getPaused`, `setAutoDelete`

#### `lv.anim`（32）
`init`, `setVar`, `setDuration`, `setTime`, `setDelay`, `setValues`, `setCustomExecCb`, `setPathCb`, `setStartCb`,
`setGetValueCb`, `setCompletedCb`, `setDeletedCb`, `setPlaybackDuration`, `setPlaybackTime`, `setPlaybackDelay`,
`setRepeatCount`, `setRepeatDelay`, `setEarlyApply`, `setBezier3Param`, `start`, `getDelay`, `getPlaytime`, `getTime`,
`getRepeatCount`, `pathLinear`, `pathEaseIn`, `pathEaseOut`, `pathEaseInOut`, `pathOvershoot`, `pathBounce`, `pathStep`, `pathCustomBezier3`

### 3.4 样式属性（`setStyleXxx` / `getStyleXxx`）

`lv.obj` 通过 `setStyle{Prop}(val)` / `getStyle{Prop}()` 读写样式。支持的全部样式属性：

**几何**：`Width, MinWidth, MaxWidth, Height, MinHeight, MaxHeight, Length, X, Y, Align`

**变换**：`TransformWidth, TransformHeight, TranslateX, TranslateY, TransformScaleX, TransformScaleY, TransformRotation, TransformPivotX, TransformPivotY, TransformSkewX, TransformSkewY, TransformScale(快捷)`

**内边距**：`PadTop, PadBottom, PadLeft, PadRight, PadRow, PadColumn, PadAll(快捷), PadHor(快捷), PadVer(快捷), PadGap(快捷)`

**外边距**：`MarginTop, MarginBottom, MarginLeft, MarginRight, MarginAll(快捷), MarginHor(快捷), MarginVer(快捷)`

**背景**：`BgColor, BgOpa, BgGradColor, BgGradDir, BgMainStop, BgGradStop, BgMainOpa, BgGradOpa, BgGrad, BgImageSrc, BgImageOpa, BgImageRecolor, BgImageRecolorOpa, BgImageTiled`

**边框**：`BorderColor, BorderOpa, BorderWidth, BorderSide, BorderPost`

**轮廓**：`OutlineWidth, OutlineColor, OutlineOpa, OutlinePad`

**阴影**：`ShadowWidth, ShadowOffsetX, ShadowOffsetY, ShadowSpread, ShadowColor, ShadowOpa`

**图片**：`ImageOpa, ImageRecolor, ImageRecolorOpa`

**线**：`LineWidth, LineDashWidth, LineDashGap, LineRounded, LineColor, LineOpa`

**圆弧**：`ArcWidth, ArcRounded, ArcColor, ArcOpa, ArcImageSrc`

**文本**：`TextColor, TextOpa, TextFont, TextLetterSpace, TextLineSpace, TextDecor, TextAlign`

**圆角/透明度**：`Radius, ClipCorner, Opa, OpaLayered, ColorFilterDsc, ColorFilterOpa`

**布局**：`Layout, BaseDir, FlexFlow, FlexMainPlace, FlexCrossPlace, FlexTrackPlace, FlexGrow, GridColumnDscArray, GridColumnAlign, GridRowDscArray, GridRowAlign, GridCellColumnPos, GridCellXAlign, GridCellColumnSpan, GridCellRowPos, GridCellYAlign, GridCellRowSpan`

**其他**：`Anim, AnimDuration, Transition, BlendMode, BitmapMaskSrc, RotarySensitivity`

> 样式值的枚举（对齐、方向、事件码等）通过 `lv.XXX` 常量访问，见第四节。

---

## 四、`lv.*` 常用常量速查

`lv` 命名空间下暴露了全部 LVGL 宏枚举常量（共 1089 个，命名即原宏名）。常用分组：

### 对齐 `lv.ALIGN_*`
`ALIGN_CENTER`, `ALIGN_TOP_LEFT`, `ALIGN_TOP_MID`, `ALIGN_TOP_RIGHT`, `ALIGN_BOTTOM_LEFT`, `ALIGN_BOTTOM_MID`, `ALIGN_BOTTOM_RIGHT`, `ALIGN_LEFT_MID`, `ALIGN_RIGHT_MID`, `ALIGN_OUT_*`, `ALIGN_DEFAULT`

### 方向 `lv.DIR_*`
`DIR_NONE`, `DIR_LEFT`, `DIR_RIGHT`, `DIR_TOP`, `DIR_BOTTOM`, `DIR_HOR`, `DIR_VER`, `DIR_ALL`

### 滚动条 `lv.SCROLLBAR_MODE_*`
`SCROLLBAR_MODE_OFF`, `SCROLLBAR_MODE_ON`, `SCROLLBAR_MODE_ACTIVE`, `SCROLLBAR_MODE_AUTO`

### 滚动吸附 `lv.SCROLL_SNAP_*`
`SCROLL_SNAP_NONE`, `SCROLL_SNAP_START`, `SCROLL_SNAP_END`, `SCROLL_SNAP_CENTER`

### 布局 `lv.LAYOUT_*` / `lv.FLEX_*` / `lv.GRID_*`
`LAYOUT_NONE`, `LAYOUT_FLEX`, `LAYOUT_GRID`,
`FLEX_FLOW_ROW`, `FLEX_FLOW_COLUMN`, `FLEX_FLOW_ROW_WRAP`, `FLEX_FLOW_COLUMN_WRAP` 等,
`FLEX_ALIGN_START`, `FLEX_ALIGN_CENTER`, `FLEX_ALIGN_END`, `FLEX_ALIGN_SPACE_BETWEEN` 等,
`GRID_ALIGN_START`, `GRID_ALIGN_CENTER`, `GRID_ALIGN_END`, `GRID_ALIGN_STRETCH` 等

### 事件 `lv.EVENT_*`
`EVENT_PRESSED`, `EVENT_PRESSING`, `EVENT_RELEASED`, `EVENT_CLICKED`, `EVENT_SHORT_CLICKED`, `EVENT_LONG_PRESSED`, `EVENT_LONG_PRESSED_REPEAT`, `EVENT_GESTURE`, `EVENT_SCROLL`, `EVENT_SCROLL_BEGIN`, `EVENT_SCROLL_END`, `EVENT_VALUE_CHANGED`, `EVENT_FOCUSED`, `EVENT_DEFOCUSED`, `EVENT_DELETE`, `EVENT_ALL`, ...

### 状态 `lv.STATE_*`
`STATE_DEFAULT`, `STATE_CHECKED`, `STATE_FOCUSED`, `STATE_FOCUS_KEY`, `STATE_EDITED`, `STATE_HOVERED`, `STATE_PRESSED`, `STATE_SCROLLED`, `STATE_DISABLED`, `STATE_USER_1..4`, `STATE_ANY`

### 对象 Flag `lv.OBJ_FLAG_*`
`OBJ_FLAG_HIDDEN`, `OBJ_FLAG_CLICKABLE`, `OBJ_FLAG_CLICK_FOCUSABLE`, `OBJ_FLAG_CHECKABLE`, `OBJ_FLAG_SCROLLABLE`, `OBJ_FLAG_SCROLL_ELASTIC`, `OBJ_FLAG_SCROLL_MOMENTUM`, `OBJ_FLAG_SCROLL_ONE`, `OBJ_FLAG_SCROLL_CHAIN`, `OBJ_FLAG_SCROLL_ON_FOCUS`, `OBJ_FLAG_SNAPPABLE`, `OBJ_FLAG_PRESS_LOCK`, `OBJ_FLAG_EVENT_BUBBLE`, `OBJ_FLAG_GESTURE_BUBBLE`, `OBJ_FLAG_ADV_HITTEST`, `OBJ_FLAG_IGNORE_LAYOUT`, `OBJ_FLAG_FLOATING`, `OBJ_FLAG_OVERFLOW_VISIBLE`, `OBJ_FLAG_LAYOUT_1`, `OBJ_FLAG_LAYOUT_2`, `OBJ_FLAG_WIDGET_1`, `OBJ_FLAG_WIDGET_2`, `OBJ_FLAG_USER_1..4`

### 颜色 / 透明度
`lv.color.hex("#RRGGBB")` 构造颜色；`OPA_0` ~ `OPA_100`, `OPA_TRANSP`, `OPA_COVER`, `OPA_MAX`, `OPA_MIN`

### 调色板 `lv.PALETTE_*`
`PALETTE_RED`, `PALETTE_PINK`, `PALETTE_PURPLE`, `PALETTE_DEEP_PURPLE`, `PALETTE_INDIGO`, `PALETTE_BLUE`, `PALETTE_LIGHT_BLUE`, `PALETTE_CYAN`, `PALETTE_TEAL`, `PALETTE_GREEN`, `PALETTE_LIGHT_GREEN`, `PALETTE_LIME`, `PALETTE_YELLOW`, `PALETTE_AMBER`, `PALETTE_ORANGE`, `PALETTE_DEEP_ORANGE`, `PALETTE_BROWN`, `PALETTE_BLUE_GREY`, `PALETTE_GREY`, `PALETTE_NONE`, ...

### 符号 `lv.SYMBOL_*`
`SYMBOL_OK`, `SYMBOL_CLOSE`, `SYMBOL_BACKSPACE`, `SYMBOL_SETTINGS`, `SYMBOL_BLUETOOTH`, `SYMBOL_WIFI`, `SYMBOL_BATTERY_FULL`, `SYMBOL_BATTERY_EMPTY`, `SYMBOL_PLAY`, `SYMBOL_PAUSE`, `SYMBOL_HOME`, `SYMBOL_LIST`, `SYMBOL_IMAGE`, `SYMBOL_FILE`, `SYMBOL_AUDIO`, ...

### 圆角 / 字体
`RADIUS_CIRCLE`, `FONT_MONTSERRAT_8` ~ `FONT_MONTSERRAT_48`, `FONT_UNSCII_8`, `FONT_UNSCII_16`

### 颜色格式 / 混合 / 动画路径 / 图表类型 等
`COLOR_FORMAT_RGB565`, `COLOR_FORMAT_ARGB8888`, ..., `BLEND_MODE_NORMAL`, `BLEND_MODE_ADDITIVE`, `BLEND_MODE_MULTIPLY`, `BLEND_MODE_SUBTRACTIVE`, `CHART_TYPE_LINE`, `CHART_TYPE_BAR`, `CHART_TYPE_SCATTER`, `BAR_MODE_NORMAL`, `BAR_MODE_RANGE`, `BAR_MODE_SYMMETRICAL`, ...

> 完整常量见 `src/script_engine/sni/sni_api/lv/sni_api_lv.c` 的 `lv_root_constants[]`（1089 个）。

---

## 五、API 源文件索引

| 命名空间 | 源文件 | 类描述表 |
|---|---|---|
| `eos.*` | `src/script_engine/sni/sni_api/eos/sni_api_eos.c` | `eos_api_classes[]` |
| `eos.*`（权限） | `.../eos/sni_api_eos_permission.c` | `eos_class_desc_permission` |
| `lv.*` | `src/script_engine/sni/sni_api/lv/sni_api_lv.c` | `lv_api_classes[]` |
| 导出框架 | `src/script_engine/sni/sni_api/sni_api_export.c` | `sni_api_build` / `sni_api_mount` |

---

## 六、WOS 框架 C API（`src/ui/wos/`）

> **这是 C 层框架 API**（不是 JS 脚本 API），供原生 C App / 系统层调用。
> 架构见 `WatchOS_UI框架设计.md`。总头文件：`src/ui/wos/eos_wos.h`（一行 include 全部）。

### 6.1 初始化

| 函数 | 说明 |
|---|---|
| `void eos_wos_framework_init(void)` | 初始化整个框架：App Manager + 状态栏 + 通知层 + 注册内置 app。启动时由 `eos_core` 调用 |

### 6.2 Design Tokens / 样式（`core/eos_wos_theme.h`）

| 宏 / 函数 | 说明 |
|---|---|
| `WOS_COLOR_BG / CARD / TEXT_PRIMARY / TEXT_SECONDARY / ACCENT / ACCENT_ALT` | 颜色 token |
| `WOS_RADIUS_CARD / WOS_RADIUS_PILL` | 圆角 token（16 / 999） |
| `WOS_FONT_XXL / XL / MD / SM` | 字号 token（28 / 20 / 14 / 11） |
| `WOS_PAD_CARD / WOS_GAP_ROW / WOS_GAP_COL / WOS_STATUSBAR_H` | 间距 / 状态栏高 token |
| `void wos_style_glass_card(lv_obj_t*, int radius, lv_opa_t tint)` | 玻璃卡片样式（半透明底 + 1px 高光描边） |
| `void wos_style_glass_pill(lv_obj_t*, lv_opa_t tint)` | 胶囊玻璃样式 |
| `void wos_style_text_primary(lv_obj_t*, int size)` / `wos_style_text_secondary(...)` | 文本样式快捷设置 |

### 6.3 布局助手（`core/eos_wos_layout.h`）— 禁固定坐标，全 flex/grid

| 函数 | 说明 |
|---|---|
| `lv_obj_t *wos_row(lv_obj_t *parent)` | flex 行容器（gap 8） |
| `lv_obj_t *wos_column(lv_obj_t *parent)` | flex 列容器（gap 8） |
| `lv_obj_t *wos_card(lv_obj_t *parent)` | 玻璃圆角卡片 |
| `lv_obj_t *wos_grid(lv_obj_t *parent, int cols, int gap)` | grid 容器 |
| `void wos_flex_grow(lv_obj_t *obj)` | 子项弹性撑满 |
| `void wos_make_safe_area(lv_obj_t *root)` | 按圆形 display profile 预留安全区（自动加状态栏高度内边距） |

### 6.4 App 实例抽象（`core/eos_wos_app.h`）

| 类型 / 函数 | 说明 |
|---|---|
| `eos_wos_app_desc_t` | App 描述符：`id` / `name` / `build(app)` / `destroy(app)` 回调 |
| `eos_wos_app_t` | App 实例：`root`（专属容器）+ `user_data` + **timer/anim 账本** |
| `lv_timer_t *wos_app_timer(eos_wos_app_t*, lv_timer_cb_t, uint32_t period_ms, void *user_data)` | 登记到账本的定时器（关闭时自动删除，防残留） |

### 6.5 App Manager（`manager/eos_wos_app_manager.h`）— 页面切换唯一入口

| 函数 | 说明 |
|---|---|
| `void wos_app_manager_init(void)` | 初始化（注册左滑关闭手势） |
| `bool wos_app_manager_register(const eos_wos_app_desc_t *desc)` | 注册 App（幂等） |
| `bool wos_app_manager_open(const char *id)` | 打开 App：先同步清理当前页 → 建 root → build → 缩放+淡入 |
| `void wos_app_manager_close(void)` | 关闭当前页：缩小+淡出 → 200ms 后递归清理 + **残留审计** |
| `eos_wos_app_t *wos_app_manager_get_active(void)` | 当前活跃 App（NULL=无） |
| `wos_app_state_t wos_app_manager_get_state(void)` | 状态：IDLE / LAUNCHING / ACTIVE / CLOSING |
| `const char *wos_app_manager_active_id(void)` | 当前 App id |
| `int wos_app_manager_registered_count(void)` / `wos_app_manager_registered(int i)` | 注册表遍历 |

### 6.6 动画（`manager/eos_wos_transition.h`）

| 函数 | 说明 |
|---|---|
| `void wos_transition_open(lv_obj_t *root)` | 打开动画：scale 0.85→1 + fade，200ms ease-out |
| `void wos_transition_close(lv_obj_t *root, lv_anim_ready_cb_t on_done)` | 关闭动画：缩小+淡出，150ms |
| `void wos_transition_slide(lv_obj_t *from, lv_obj_t *to, lv_dir_t dir)` | 页面左右滑动切换，200ms |
| `void wos_transition_pull_down(lv_obj_t *panel)` | 下拉动画（控制中心风格），250ms |

> ⚠️ 动画只做在 **App root 容器**上（不 round-clip 的对象）。对带 `border-radius + clip_corner` 的 view 做 transform_scale 会触发 LVGL9 渲染 bug。

### 6.7 系统层（`system/`）

| 函数 | 说明 |
|---|---|
| `void wos_statusbar_init(void)` | 初始化状态栏（24px 半透明玻璃条：左标题右时钟，30s 刷新） |
| `void wos_statusbar_set_title(const char *title)` | 设置状态栏左标题 |
| `void wos_statusbar_update_clock(void)` | 立即刷新时钟 |
| `void wos_notification_init(void)` | 初始化通知层 |
| `void wos_notification_show(const char *title, const char *body, uint32_t hold_ms)` | 显示通知卡片（顶部滑入，hold_ms 后自动消失） |
| `void wos_notification_dismiss(void)` | 立即关闭当前通知 |

### 6.8 Overlay 层（扩展自 `eos_overlay_layer`，8 层）

自底向上：`user_top / snapshot / app / header / notification / statusbar / indicator / overlay`。
App 页面 root 挂在 **app 层**（`eos_overlay_get_app_layer()`），状态栏/通知/后台图标/系统 overlay 在其上，层级固定无需 hide/show 协调。

| 函数 | 说明 |
|---|---|
| `lv_obj_t *eos_overlay_get_app_layer(void)` | App 页层（App Manager 使用） |
| `lv_obj_t *eos_overlay_get_notification_layer(void)` | 通知层 |
| `lv_obj_t *eos_overlay_get_statusbar_layer(void)` | 状态栏层 |

### 6.9 Shell 调试命令

```
wos list                     # 列出已注册 App
wos open <id>                # 打开 App（动画 + 生命周期）
wos close                    # 关闭当前 App
wos status                   # 状态机 + 活跃 App
wos notify <title> <body>    # 弹一条通知
power status|deep-sleep [sec]|wake   # 电源管理（含深度睡眠）
```

### 6.10 已注册内置 App

| id | 名称 | 说明 |
|---|---|---|
| `demo` | Demo | 框架验证 app：flex 玻璃卡片 + 账本 timer |

> 模拟器热键：**W** 开关 demo app；**C** 控制中心；**H** 回表盘；**L** app 列表；**R** 返回；**Esc** 退出。
