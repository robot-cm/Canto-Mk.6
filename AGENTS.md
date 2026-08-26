# Canto Mk.6 + XIAO ESP32-S3 自定义系统开发任务

## 0. 项目目标

基于现有的 **ElenixOS** 开源项目进行二次开发。
注：新名称叫 Canto Mk.6，项目中有残留注释和底层 API,请把 ElenixOS 直接视为 Canto Mk.6。

目标硬件：

* Seeed Studio XIAO ESP32-S3 Pre-Soldered
* Seeed Studio 1.28" Round Touch Display for XIAO

目标不是简单修改一个 Demo，而是逐步把 Canto Mk.6 改造成一个：

> **Core 固件 + System Services + SD 卡插件 APP + 统一 UI**

的轻量级 ESP32-S3 系统。

最终目标类似一个微型设备 OS：

```text
Canto Mk.6
│
├── Core
│   ├── Boot
│   ├── FreeRTOS
│   ├── Hardware
│   ├── LVGL
│   ├── System UI
│   ├── Shell
│   └── Network
│
├── System Services
│   ├── Wi-Fi
│   ├── SOCKS5 Client
│   ├── RTC
│   ├── Storage
│   ├── Resource Manager
│   └── Plugin Manager
│
└── SD Card
    └── Apps
        ├── Clock
        ├── Gallery
        ├── File Manager
        ├── Calculator
        ├── Timer
        ├── Alarm
        └── Games
```

---

# 1. 目标硬件

## MCU

Seeed Studio XIAO ESP32-S3 Pre-Soldered

官方产品：

https://shop.seeedstudio.com.cn/Seeed-Studio-XIAO-ESP32S3-Pre-Soldered-p-6334.html

官方 Wiki：

https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/

主要参数：

* ESP32-S3R8
* 双核 Xtensa LX7
* 最高 240 MHz
* 8 MB Flash
* 8 MB PSRAM
* Wi-Fi 2.4 GHz
* Bluetooth LE
* USB Type-C

**本项目使用普通 XIAO ESP32-S3，不是 XIAO ESP32-S3 Sense。**

禁止将 Sense 的：

* 摄像头
* 麦克风
* 板载 SD
* Sense 专用 GPIO

错误地应用到本项目。

---

# 2. Round Display

Seeed Studio 1.28" Round Touch Display for XIAO

官方产品：

https://shop.seeedstudio.com.cn/1-28-Round-Touch-Display-for-Seeed-Studio-XIAO-ESP32.html

官方 Wiki：

https://wiki.seeedstudio.com/get_start_round_display/

主要硬件：

* 1.28 英寸
* 圆形
* 240 × 240
* 65K colors
* GC9A01 LCD
* SPI
* 电容触摸
* microSD / TF 卡
* RTC
* 电池接口
* 充电电路

目前只重点使用：

* 显示
* 触摸
* SD
* RTC

暂时不围绕其他外设扩展系统。

---

# 3. 最重要的架构原则

本系统必须明确分成三个层次：

```text
┌─────────────────────────────────┐
│           Canto Mk.6 Core         │
│                                 │
│ Boot / RTOS / Hardware / LVGL   │
│ Shell / Network / System UI     │
└────────────────┬────────────────┘
                 │
┌────────────────┴────────────────┐
│          System Services        │
│                                 │
│ Wi-Fi / SOCKS5 / RTC / Storage  │
│ Resource / Plugin Manager       │
└────────────────┬────────────────┘
                 │
              Plugin API
                 │
┌────────────────┴────────────────┐
│             SD Card             │
│                                 │
│ Clock / Gallery / Games / etc.  │
└─────────────────────────────────┘
```

核心思想：

> **Core 是稳定的。**
>
> **Service 提供系统能力。**
>
> **APP 尽可能插件化。**

---

# 4. Flash / PSRAM / SD 的职责

XIAO ESP32-S3：

```text
Flash = 8 MB
PSRAM = 8 MB
```

因此必须避免把所有内容都编译进固件。

## Flash

主要存：

* Bootloader
* Partition Table
* Canto Mk.6 Core
* FreeRTOS
* Hardware abstraction
* LVGL
* System UI
* Shell
* Network
* SOCKS5 Client
* System Services
* Plugin Manager
* 必要系统资源
* 必要配置

## PSRAM

主要用于：

* LVGL buffer
* 图片缓存
* 动画缓存
* 页面数据
* 大型临时数据
* Plugin runtime
* Resource cache

## SD Card

主要用于：

* APP
* APP 图标
* APP 资源
* 图片
* 壁纸
* 字体
* 动画
* 用户文件
* APP 数据
* 大型资源

目标：

> **Flash 放系统，PSRAM 放运行时大对象，SD 放 APP 和可变资源。**

---

# 5. APP 必须尽可能插件化

目标：

**不要把所有 APP 编译进 Core。**

理想结构：

```text
/sdcard/apps/

    Clock/
        manifest.json
        icon.png
        <entry>
        assets/

    Gallery/
        manifest.json
        icon.png
        <entry>
        assets/

    FileManager/
        manifest.json
        icon.png
        <entry>
        assets/

    Games/
        manifest.json
        icon.png
        <entry>
        assets/
```

其中 `<entry>` 的具体格式必须根据 Canto Mk.6 当前实际 runtime 决定。

**不要默认它一定是 JavaScript。**

必须先检查 Canto Mk.6 源码，确认它到底支持：

* JavaScript
* scripting
* bytecode
* VM
* native plugin
* 其他 APP runtime

然后再决定插件技术方案。

---

# 6. 插件技术方案必须先评估

重点分析以下方案：

## A. Native C/C++ Plugin

优点：

* 性能最高
* C API 直接
* 适合硬件功能

缺点：

* 安全性差
* 内存管理复杂
* 动态加载困难
* APP 崩溃可能影响系统

## B. JavaScript / Script Plugin

优点：

* APP 文件可以放 SD
* 开发简单
* UI APP 非常适合
* AI 很容易生成
* 不需要重新刷 Core

缺点：

* Runtime 占内存
* 性能低于 Native

## C. Bytecode / VM Plugin

优点：

* 可控
* 可以限制权限
* 适合插件

缺点：

* 需要 runtime

## D. Resource-only APP

APP 逻辑仍在 Core，但：

* 图标
* UI资源
* 配置
* 数据

放 SD。

这只是过渡方案。

---

# 7. 插件系统的目标

Plugin Manager 负责：

```text
扫描
 ↓
读取 manifest
 ↓
验证
 ↓
注册
 ↓
显示 Launcher
 ↓
启动
 ↓
退出
 ↓
释放资源
```

未来支持：

* 安装
* 卸载
* 启用
* 禁用
* 更新
* 版本管理

第一阶段只需要：

* 扫描
* 注册
* 启动
* 退出

---

# 8. APP 与 Core 隔离

APP 不应该直接访问：

* GPIO
* SPI
* I2C
* LCD controller
* Touch controller
* FreeRTOS 内部对象
* LVGL 底层 driver
* Wi-Fi driver
* TCP/IP 内部结构

APP 应该通过统一 Plugin API。

目标：

```text
Plugin
  ↓
Plugin API
  ↓
System Service
  ↓
Hardware
```

例如未来可以有：

```text
ui.*
storage.*
rtc.*
network.*
proxy.*
input.*
settings.*
app.*
```

具体 API 必须根据实际 Canto Mk.6 架构设计。

---

# 9. Shell：必须属于 Core

系统必须加入一个底层 Shell。

Shell 不能依赖 SD 卡。

即使：

* SD 卡不存在
* SD 卡损坏
* 所有 APP 崩溃
* Launcher UI 崩溃

Shell 仍然必须可以使用。

第一阶段：

**USB Serial / UART Shell**

示例：

```text
> help
> mem
> ps
> heap
> psram
> flash
> sd
> rtc
> display
> touch
> wifi
> proxy
> apps
> app list
> app start Clock
> app stop Clock
> app disable Clock
> log
> reboot
```

---

# 10. Shell 调试能力

至少需要能够查看：

## 内存

```text
mem
heap
psram
```

输出：

* free
* used
* largest free block
* minimum free heap

## FreeRTOS

```text
ps
```

显示：

* Task
* State
* Priority
* Stack
* CPU usage（如果可用）

## LVGL

```text
lvgl stats
```

尽量显示：

* FPS
* rendering time
* flush time
* buffer
* memory

## SD

```text
sd
```

显示：

* mounted
* capacity
* free
* filesystem
* speed（如果可以）

## APP

```text
apps
app list
app info Clock
app restart Clock
app disable Clock
```

## 日志

```text
log
log level debug
log level info
```

---

# 11. 网络必须属于 Core

网络不是普通 APP。

Core 负责：

* Wi-Fi
* TCP/IP
* DNS
* Network Manager

网络服务应该向 APP 提供统一 API。

结构：

```text
APP
 ↓
Network API
 ↓
Network Service
 ↓
TCP/IP
 ↓
Wi-Fi
```

---

# 12. SOCKS5：客户端

本项目需要的是：

> **SOCKS5 Client**

不是 SOCKS5 Server。

ESP32 作为客户端连接一个外部 SOCKS5 代理服务器。

结构：

```text
APP
 ↓
Network API
 ↓
Proxy Service
 ↓
SOCKS5 Client
 ↓
TCP/IP
 ↓
Wi-Fi
 ↓
SOCKS5 Server
 ↓
Internet
```

SOCKS5 Client 属于：

**Core / System Service**

不是 SD 插件。

---

# 13. SOCKS5 功能

至少支持：

* SOCKS5
* IPv4
* domain name
* TCP CONNECT
* 无认证
* 用户名/密码认证（如果资源允许）

配置存储在系统配置中：

```text
proxy.enabled
proxy.host
proxy.port
proxy.username
proxy.password
```

密码：

**禁止通过普通 Shell 日志明文输出。**

Shell：

```text
> proxy status

Enabled: yes
Server: xxx.xxx.xxx.xxx:1080
Auth: configured
Status: connected
```

不要输出密码。

---

# 14. 网络代理设计原则

不要让每个 APP 自己实现 SOCKS5。

错误：

```text
Gallery
 └── SOCKS5 implementation

Browser
 └── SOCKS5 implementation

Updater
 └── SOCKS5 implementation
```

正确：

```text
             Network API
                  │
           Network Service
                  │
             SOCKS5 Client
                  │
                Wi-Fi
```

所有需要联网的 APP 统一使用 Network API。

---

# 15. UI 系统

需要重新设计现有 Canto Mk.6 UI。

目标：

> **不要保留“开发板 Demo 风格”。**

UI 必须：

* 现代
* 统一
* 简洁
* 适合 240×240 圆屏
* 触摸友好
* 动画流畅

重点设计：

* Launcher
* APP Grid
* APP Icon
* Page
* Header
* Button
* List
* Card
* Dialog
* Settings
* Clock

---

# 16. Design System

建立统一 UI 组件。

例如：

```text
SystemUI
├── Label
├── Button
├── Icon
├── Card
├── List
├── Dialog
├── Slider
├── Page
└── Navigation
```

APP 尽量调用 SystemUI。

不要让每个 APP 自己重新设计 UI。

---

# 17. 图标系统

APP 自带：

```text
icon.png
```

Launcher 从 Plugin Manager 获取：

```text
APP metadata
 ↓
Icon Manager
 ↓
icon.png
```

以后支持主题：

```text
/sdcard/themes/default/
/sdcard/themes/dark/
/sdcard/themes/custom/
```

主题可以覆盖 APP 图标。

---

# 18. 资源系统

系统资源：

放 Flash。

大型、可替换资源：

放 SD。

APP 私有资源：

```text
/sdcard/apps/<APP>/assets/
```

推荐：

```text
/sdcard/
├── apps/
├── themes/
├── fonts/
├── wallpapers/
├── animations/
├── resources/
└── user/
```

---

# 19. SD IO

**绝对不能让 SD IO 长时间阻塞 UI。**

禁止：

```text
LVGL Task
 ↓
SD Read
 ↓
等待
 ↓
UI 卡顿
```

推荐：

```text
SD Worker
 ↓
Read
 ↓
Cache
 ↓
PSRAM
 ↓
LVGL
```

必要时实现：

* preload
* lazy loading
* asynchronous loading
* resource cache

---

# 20. 动画性能

这是项目核心目标之一。

优先保证：

**稳定、流畅、低延迟。**

需要分析：

* LVGL draw buffer
* DMA
* SPI
* refresh rate
* LVGL tick
* UI task
* FreeRTOS priority
* internal RAM
* PSRAM
* heap
* cache

目标：

尽可能接近稳定高帧率，而不是只追求理论最大 FPS。

---

# 21. 动画资源

不要把所有动画都做成大量图片。

优先：

```text
LVGL animation
+
简单图形
+
缓存资源
```

复杂动画再使用帧动画。

如果使用帧动画：

```text
SD
 ↓
Preload
 ↓
PSRAM
 ↓
LVGL
```

禁止：

```text
每一帧
 ↓
SD Read
 ↓
显示
```

---

# 22. 内存策略

必须严格区分：

### Internal RAM

用于：

* 实时任务
* DMA
* 高频访问数据
* 系统关键结构

### PSRAM

用于：

* 图片
* LVGL buffer
* 动画
* Cache
* 大型 APP 数据

### Flash

用于：

* Core
* 固定代码
* 必要资源

### SD

用于：

* Plugins
* 大型资源
* 用户数据

不要无脑：

```text
malloc → Internal RAM
```

也不要无脑：

```text
malloc → PSRAM
```

必须考虑 DMA、访问速度和生命周期。

---

# 23. Flash Partition

由于 Flash 只有 8MB：

必须检查当前 Canto Mk.6 partition table。

分析：

* Bootloader
* Partition table
* App partition
* NVS
* OTA
* filesystem

如果 OTA 占用大量 Flash：

评估是否需要保留。

如果项目第一阶段不需要 OTA，可以考虑重新设计 partition，让 Core 获得更大的 app partition。

但是：

**不要直接删除 OTA。**

先说明：

* 当前 partition
* 当前 app 最大尺寸
* 当前固件尺寸
* OTA 占用
* 删除 OTA 的收益
* 删除 OTA 的代价

然后再决定。

---

# 24. 核心系统 APP

只有必要功能可以直接属于 Core：

```text
Launcher
Settings
Clock
```

其他 APP 尽量插件化：

```text
Gallery
File Manager
Calculator
Timer
Alarm
Games
Music
```

如果某个 APP 必须使用 native C 才能达到性能要求：

说明原因。

不要为了插件化而牺牲系统稳定性。

---

# 25. 插件崩溃处理

目标：

```text
APP crash
 ↓
Plugin Manager
 ↓
destroy APP
 ↓
release resources
 ↓
return Launcher
```

如果当前 Canto Mk.6 无法做到真正隔离：

必须明确说明。

不要声称“APP 崩溃不会影响系统”，除非技术上确实能够保证。

---

# 26. 安全

SD 卡是不可信输入。

Plugin Manager 必须检查：

* manifest
* APP ID
* version
* path
* path traversal
* 文件大小
* 资源大小
* 非法路径
* 缺少 entry
* 无效资源

禁止 APP 使用：

```text
../../
```

访问系统目录。

---

# 27. AI 修改代码的最高优先级规则

你不是从零写系统。

你必须：

> **先理解现有 Canto Mk.6，再进行最小必要修改。**

## 修改之前

必须搜索：

* 整个仓库
* 相关 symbol
* API
* hardware definitions
* build system
* dependencies

不能只看一个文件就开始修改。

---

## 禁止猜测

不能凭空猜：

* GPIO
* SPI
* I2C
* LCD CS
* LCD DC
* LCD RST
* Touch IRQ
* SD CS
* RTC 地址
* LVGL API
* Canto Mk.6 API
* Plugin API

不确定时：

**搜索源码或官方资料。**

---

# 28. 硬件资料优先级

如果信息冲突：

1. 实际硬件原理图 / Datasheet
2. Seeed 官方 Wiki
3. Seeed 官方代码 / Library
4. Canto Mk.6 当前源码
5. 其他可靠资料
6. 网络博客
7. 模型自身记忆

不要使用未经确认的信息修改硬件驱动。

---

# 29. 修改流程

每次修改：

```text
搜索
 ↓
理解依赖
 ↓
提出修改方案
 ↓
最小修改
 ↓
编译
 ↓
查看错误
 ↓
查看 warning
 ↓
检查 Flash
 ↓
检查 RAM
 ↓
检查 PSRAM
 ↓
继续
```

一次只解决一个明确问题。

不要同时重构：

* UI
* Network
* Plugin
* Driver

除非任务明确要求。

---

# 30. 第一阶段：只读分析

**现在禁止修改任何代码。**

首先完整扫描当前 Canto Mk.6 仓库。

输出以下报告：

## A. 项目结构

完整目录树。

## B. 构建

确定：

* ESP-IDF / Arduino / 其他
* C / C++
* JavaScript / scripting / VM
* CMake
* dependencies

## C. 硬件

找到：

* ESP32-S3
* Display
* Touch
* SD
* RTC
* GPIO
* SPI
* I2C

## D. UI

找到：

* LVGL
* Launcher
* APP UI
* Theme
* Icon
* Animation

## E. APP

确定：

* 当前 APP 架构
* APP 是否编译进 Flash
* APP 是否动态加载
* 当前 runtime
* 是否支持 JS
* 是否支持 VM
* 是否已有 Plugin API

## F. 网络

确定：

* Wi-Fi
* TCP/IP
* DNS
* Network abstraction
* 是否已有 proxy
* 是否适合加入 SOCKS5 Client

## G. Shell

确定：

* 当前是否存在 Shell
* Shell 使用什么接口
* 是否可以扩展
* 是否可以永久保留为底层调试入口

## H. Flash

输出：

* Partition Table
* 各分区大小
* 当前固件大小
* 最大可用 App size
* OTA 占用

## I. Memory

输出：

* Internal RAM
* PSRAM
* Heap
* LVGL buffer
* 最大连续内存

## J. Plugin 可行性

重点回答：

> **当前 Canto Mk.6 最适合采用哪种 SD 卡插件机制？**

比较：

* Native C/C++
* JavaScript
* Bytecode / VM
* Resource-only

必须根据实际源码判断。

---

# 31. 第一阶段结束条件

完成以上分析后：

**停止。**

不要修改代码。

不要创建新文件。

不要重构。

不要自动安装依赖。

不要删除任何东西。

只输出分析报告和推荐架构。

---

# 32. 最终目标架构

最终希望得到：

```text
                     ┌──────────────────┐
                     │   Canto Mk.6 Core  │
                     │                  │
                     │ Boot             │
                     │ FreeRTOS         │
                     │ Hardware        │
                     │ LVGL             │
                     │ System UI        │
                     │ Shell            │
                     │ Network          │
                     └────────┬─────────┘
                              │
              ┌───────────────┼────────────────┐
              │               │                │
        Network Service   Storage Service   Plugin Manager
              │               │                │
          Wi-Fi/SOCKS5       SD Card          /apps
                                               │
                              ┌────────────────┼──────────────┐
                              │                │              │
                            Clock           Gallery         Games
                              │                │              │
                              └────────────────┴──────────────┘
```

系统最终应该具备：

* ESP32-S3
* 240×240 圆形触摸 UI
* 流畅 LVGL 动画
* SD 卡扩展
* 插件 APP
* APP 图标系统
* Theme 系统
* RTC
* Wi-Fi
* SOCKS5 Client
* USB/UART Shell
* 完整日志系统
* 内存监控
* APP 管理
* 可持续扩展的 Plugin API

核心原则：

> **Core 小而稳定。**
>
> **Service 统一提供能力。**
>
> **APP 尽量放 SD。**
>
> **资源尽量放 SD。**
>
> **大对象使用 PSRAM。**
>
> **UI 与硬件驱动解耦。**
>
> **网络与 APP 解耦。**
>
> **Shell 永远作为底层救援和调试入口。**
>
> **任何修改先搜索、先理解，再动代码。**
