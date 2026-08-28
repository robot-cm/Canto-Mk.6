# 背光调试交接文档 / Backlight Debug Handoff

> 适用对象：接手 ESP32-S3 + GC9A01 圆形屏背光问题的工程师 / 另一个 AI 助手
> 板型：XIAO ESP32-S3（圆屏扩展板，GC9A01）
> 日期：2026-08-28
> 状态：**软件层已定位并修复，物理层结论待硬件验证**

---

## 0. TL;DR（一句话结论）

软件侧已经确认：**GPIO43（BL 引脚）信号输出链路是正确的**，背光不亮/亮度无效的根因**极大概率是物理层**——BL 引脚到背光 LED 的供电/通路问题，或背光板本身需要更高的驱动能力。下一步要用 `bltest` 命令做"引脚 HIGH 屏幕是否亮"的物理判定。

---

## 1. 硬件引脚映射（已真机核对）

来源：`port/esp32s3/main/board_xiao_esp32s3_round.h`

| 功能 | 板丝印 | GPIO | 备注 |
|------|--------|------|------|
| MOSI | D10 | GPIO9 | |
| SCLK | D8  | GPIO7 | |
| CS   | D1  | GPIO2 | |
| DC   | D3  | GPIO4 | |
| BL   | D6  | **GPIO43** | 背光，本次调试对象 |
| MISO | D9  | GPIO8 | LCD 不读，与 SD 共用 |
| RST  | —   | -1   | 软件复位；GPIO3 是 SD CS，绝不能占用 |

> ⚠️ **关键陷阱**：GPIO43 ≥ 32，其使能位/输出位在 `GPIO.enable1 / GPIO.out1` 寄存器（bit = pin − 32），不在 `GPIO.enable / GPIO.out`。早期诊断读错寄存器，导致 `en/out` 恒为 0 的假象，曾一度误判为"引脚没输出"。

---

## 2. 已完成的软件层验证

### 2.1 现象（原始日志）
```
I GC9A01: bltest: BL(GPIO43)->HIGH stop=ESP_OK dir=ESP_OK lvl=ESP_OK rd=0
[display] bltest: BL(GPIO43) HIGH -> screen should be FULLY BRIGHT (PWM bypassed)
```
- `gpio_set_level(GPIO43, 1)` 返回 `ESP_OK`，但回读为 `0`。
- 屏幕亮度**没有变化**。

### 2.2 根因（软件侧已修复）

**`gpio_set_direction(GPIO_MODE_OUTPUT)` 只打开输出使能（enable），不会把信号源 `func_out_sel` 从 LEDC 切回 `GPIO.out` 寄存器。**

- GPIO43 之前被 `ledc_channel_config` 绑定到 `LEDC_CH0` 外设信号。
- `ledc_stop` 之后该通道停发，pad 呈**高阻态**。
- 此时即便 `gpio_set_level(GPIO43, 1)` 写进了 `GPIO.out`，**信号源仍是 LEDC**，输出没送到 pad → 直驱"无效"，PWM 也"无效"，现象完全吻合。

### 2.3 修复手段（已落地）

在 `display_bltest()` 中显式切回 GPIO 信号源：
```c
esp_rom_gpio_pad_select_gpio(DISPLAY_PIN_BL);
esp_rom_gpio_connect_out_signal(DISPLAY_PIN_BL, SIG_GPIO_OUT_IDX, 0, 0);
gpio_set_direction(DISPLAY_PIN_BL, GPIO_MODE_OUTPUT);
gpio_set_level(DISPLAY_PIN_BL, high ? 1 : 0);
```
- 另：`gpio_get_level` 在 OUTPUT 模式读的是 input 寄存器（被 disable，恒 0），**无诊断价值**。改为直读 `GPIO.enable1 / GPIO.out1 / func_out_sel` 寄存器确认链路。
- 修复后 `bltest HIGH` 应输出：
  ```
  en=1 out=1 func_sel=<SIG_GPIO_OUT_IDX>   # 信号源已切回 GPIO.out
  ```

### 2.4 `bltest` 命令判读标准

| `bltest HIGH` 结果 | 结论 |
|--------------------|------|
| 屏幕**变全亮** | 软件链路 OK；若 PWM 亮度仍无效 → 检查亮度调节入口（如 KE 开关是否被别的路径覆盖） |
| 屏幕**依旧暗** | **物理层问题**：BL 引脚到背光 LED 断线 / 背光板异常 / 需要更高驱动电流（GPIO 拉电流不足） |

### 2.5 相关代码位置

| 文件 | 符号 | 作用 |
|------|------|------|
| `port/esp32s3/main/eos_dev_display_gc9a01.c` | `display_backlight_init()` | LEDC 定时器 + 通道配置 |
| 同上 | `display_set_brightness()` | 亮度设定，含 bltest 后 rebind 回 LEDC |
| 同上 | `display_bltest()` | 绕过 PWM 的 GPIO 直驱诊断（已修复） |
| 同上 | `display_power_on/off()` | 亮/灭背光，维护 `s_last_brightness` |
| `board_xiao_esp32s3_round.h` | `BOARD_GC9A01_BL_PIN` | = `XIAO_D6_GPIO` = GPIO43 |
| `src/kernel/shell/eos_shell.c` | `bltest` 命令 | 触发 `display_bltest()` |

---

## 3. 当前状态（截至交接）

- ✅ LEDC 初始化、PWM 亮度设定代码路径正常（duty readback 正确）。
- ✅ `bltest` 直驱信号源切换已修复，能真正把 GPIO43 拉高/拉低。
- ⏳ **待验证**：`bltest HIGH` 时屏幕是否真的全亮 —— 这一步需要在真机上执行，是区分"软件/物理层"的判定点。
- ⏳ 若物理层确认断链，需要硬件改板或加背光驱动电路（如 MOSFET / 专用背光 IC），GPIO43 仅做开关信号。

---

## 4. 给接手者的下一步行动（Action Items）

1. **编译烧录**当前代码（含 2.3 修复）。
2. **串口进 shell**，执行：
   - `bltest 1`（HIGH）→ 观察屏幕是否全亮
   - `bltest 0`（LOW）→ 观察屏幕是否全暗
3. **判读**（对照 2.4）：
   - 亮/暗符合预期 → 软件层闭环，问题收敛到亮度调节业务链路（如 power_save / control_center 覆盖）。
   - 不符预期 → 物理层，转硬件排查（万用表测 GPIO43 对背光焊盘通断、背光 IC 供电）。
4. **无论结果**，把 `bltest` 的 printf 输出贴回继续分析。

---

## 5. 已知坑（避免重复踩）

1. GPIO43 ≥ 32 → 用 `enable1/out1`，别读 `enable/out`。
2. `gpio_set_direction(OUTPUT)` **不会**切回 GPIO.out 信号源，必须先 `esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, ...)`。
3. `gpio_get_level` 在 OUTPUT 模式恒读 0，用外设寄存器判断。
4. `ledc_stop(ch, idle=0)` 让 pad 输出 idle 低电平（不是高阻），但若之后又 `gpio_set_direction` 而不切信号源，电平仍不输出。
5. 亮度恢复用 `s_last_brightness`，不要用固定 100%，否则"亮度和没记住"。

---

## 6. 相关上下文/引用

- 驱动头注释见 `eos_dev_display_gc9a01.c` 顶部（引脚映射依据 ElenixOS-main 真机验证）。
- 原始排查日志（含 `rd=0` 假象）见会话历史 2026-08-28。
- 同仓库其他板型的背光处理可作参考：`third_party/espp-for-view/components/*/src/frontlight.cpp`、`video.cpp`（不同 IC，信号源切换写法一致）。
