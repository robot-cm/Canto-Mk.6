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
