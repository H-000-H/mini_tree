# 内存足迹 / Memory Footprint（固定开销 / Fixed Overhead）

> 构建后**固定付出**的静态内存开销：编译期确定、运行期不变、全部可经 Kconfig / 配置头裁剪。
> The **fixed** static memory cost after build: determined at compile time, unchanged at runtime, and all trimmable via Kconfig / config headers.
> 不含：板级设备表（随 DTS 变化）、用户自建任务栈、`buffer_pool`（调用方提供内存）、RTOS 内核对象。

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 资源紧张的 MCU 选型、内存裁剪 / RAM-constrained MCUs, memory trimming |
| **相关 / Related** | [architecture.md](architecture.md) · [runtime_services.md](runtime_services.md) · [osal_switching.md](osal_switching.md) |

---

## 1. 默认配置实测 / Default-Config Measurement

> 以下为 **arm-none-eabi-gcc (Cortex-M4F) 实测** `libmini_tree.a` 全量成员 `.data + .bss` 之和；默认 `.config`：`CONFIG_OSAL_NULL` + `CONFIG_SYSTEM_CPP`（EventBus / SystemCmd / Flash-Scrubber 均默认关闭）。
> Measured with **arm-none-eabi-gcc (Cortex-M4F)** across every member of `libmini_tree.a` (`.data + .bss`); default `.config`: `CONFIG_OSAL_NULL` + `CONFIG_SYSTEM_CPP` (EventBus / SystemCmd / Flash-Scrubber all off by default).

| 模块 / Module | RAM / 说明 Notes |
| :--- | :--- |
| **全量 `libmini_tree.a`（所有成员，最坏情况 / worst case）** | ≈ **28.0 KB**（`28667 B`，104 成员 / members）|
| ├─ 产品驱动 `drivers/*`（37 个，按需链入） | ≈ **7.4 KB** |
| ├─ VFS / Bus 静态池（11 模块全编入，池各 = 1） | ≈ **14 KB** |
| └─ **核心固定 / Core fixed** | ≈ **2.8 KB** |

> **关键认知 / Key insight**：28.0 KB 是"全部成员都链入"的上限。最终固件链接时静态库**按引用提取 + `--gc-sections`**——DTS 未声明的驱动/VFS/Bus 整段裁掉。**真正固定付出只有 ≈ 2.8 KB**，见 §2。
> 28.0 KB is the upper bound when **every member is linked**. At final link, members are pulled on reference + trimmed by `--gc-sections` — drivers/VFS/Bus absent from the board DTS are dropped wholesale. The **truly fixed cost is ≈ 2.8 KB**, see §2.

---

## 2. 最小所需内存 / Minimum Required RAM

> **一个外设都不开**（板级 DTS 无外设节点 / 全部 `disabled`）时，链接期裁掉全部 VFS/Bus/驱动，固件只剩框架核心。
> With **no peripherals** (board DTS declares none / all `disabled`), the link trims every VFS/Bus/driver module — the firmware keeps only the framework core.

### 2.1 默认最小（实测 / measured）

| 模块 / Module | RAM | 构成 / Contents |
| :--- | :--- | :--- |
| `osal`（裸机后端） | **0.4 KB** | 互斥/信号量池（队列池默认 0，EventBus 关时不占）/ mutex/sem pools (no queue pool — off with EventBus) |
| `interrupt` | **1.2 KB** | VIRQ 虚拟中断表 + 下半部环形缓冲 / VIRQ tables + bottom-half ring |
| `board` | **1.1 KB** | 设备实例表 + 锁存储 + 键值配置存储（8 项）/ device table + lock storage + config store (8 entries) |
| `core` + `system` + `hal` + `time_slice` + 生成物 | **< 0.2 KB** | 日志、引导、weak HAL、裸机调度器 / logging, boot, weak HAL, bare-metal scheduler |
| **默认最小合计 / default minimum** | ≈ **2.8 KB**（`2825 B`）| |

### 2.2 这是默认最小，不是绝对最小 / This Is the Default Minimum, Not the Floor

> 上表是**默认配置下的最小**——即不改任何默认值、DTS 空着就能达到。若内存仍紧张，以下开关可**继续往下压**：
> The table above is the minimum **with default settings** — reachable with an empty DTS and no overrides. When RAM is still tight, these knobs squeeze further:

| 压缩点 / Knob | 省多少 / Savings | 代价 / Trade-off |
| :--- | :--- | :--- |
| 队列池已默认 0（基础 0 + EventBus 关）| 0（已免 2 KB）| 手动用队列 → Kconfig 设基础数；开 EventBus 自动 +1 |
| `OSAL_NULL_QUEUE_BUF_SZ` 2048→512（开 EventBus 时）| 1.5 KB | 队列深度上限 512→128 单元（EventBus 恰好够，业务再建大队列会失败）|
| `CONFIG_OSAL_MUTEX_POOL_SIZE` 24→8 | ≈ 0.3 KB | 同时持有的互斥锁上限 24→8 |
| `CONFIG_BOTTOM_HALF_QUEUE_DEPTH` 16→8 | 64 B | 下半部 FIFO 深度减半 |
| `config_store` `MAX_ENTRIES` 8→4（`board/src/config_store.c`）| 0.4 KB | 键值配置项上限 8→4 |
| `CONFIG_SYSTEM=n`（去掉引导/看门狗/EventBus 全部）| **Flash ≈ 2.7 KB + RAM ≈ 0.15 KB**（实测，见 §3.3）| 框架引导管线不可用，业务自管启动 |
| `CONFIG_VIRQ=n`（关虚拟中断）| ≈ **1.3 KB RAM + 1 KB Flash** | 时间片调度器无 tick、ADC/I2S 中断下半部与 GPIO 中断路由不可用（详见 §3.6）|
| VIRQ 虚拟中断块数（`interrupt.h`）| 数百 B | 虚拟中断号空间缩小（契约级改动，谨慎）|

> 反向参考：**全开**（所有外设都在 DTS 声明、EventBus 开）时，`osal` 队列池自动按需（`CONFIG_OSAL_NULL_*`）、VFS/驱动池按 `DTC_GEN_COUNT_*` 节点数分配——典型固件落在 **5 ~ 20 KB** 区间。
> For reference, **fully loaded** (every peripheral in DTS, EventBus on) lands in the **5–20 KB** range — pools follow `DTC_GEN_COUNT_*` node counts.

---

## 3. 分模块明细 / Module Breakdown

### 3.1 OSAL（`osal/src/`，三后端内存模型不同）

| 后端 / Backend | 队列内存 / Queue Memory | 默认 / Default | 可控开关 / Knob |
| :--- | :--- | :--- | :--- |
| `OSAL_NULL`（裸机）| 静态池 `s_queues[N]` × 2048 B | **0**（基础 0 + EventBus 关）/ EventBus 开自动 +1（N=1）| `CONFIG_OSAL_NULL_MAX_QUEUES`（基础数，默认 0；EventBus 开启时 C 层自动 +1）/ `CONFIG_OSAL_NULL_QUEUE_BUF_SZ`（Kconfig，仅裸机可见）|
| `OSAL_FREERTOS` | FreeRTOS 动态堆（`xQueueCreate`）| 堆共 **8 KB** | `CONFIG_FREERTOS_HEAP_SIZE`（`configTOTAL_HEAP_SIZE`）|
| `OSAL_RTTHREAD` | RTT 独立静态堆 `s_rtt_heap` | **32 KB**（.bss）| `CONFIG_RTT_HEAP_SIZE` |

> 裸机互斥/信号量池：`CONFIG_OSAL_MUTEX_POOL_SIZE`（默认 24）、`OSAL_SEM_POOL_SIZE`（4）；锁存储 `OSAL_MUTEX_STORAGE_SIZE`（128 B/把，跨后端统一，RTOS 静态锁所需）。
> Bare-metal mutex/sem pools: `CONFIG_OSAL_MUTEX_POOL_SIZE` (24), `OSAL_SEM_POOL_SIZE` (4); lock storage `OSAL_MUTEX_STORAGE_SIZE` (128 B/lock, unified across backends for RTOS static locks).

### 3.2 EventBus（默认关闭 / off by default）

| 静态项 / Item | 大小 / Size | 可控开关 / Knob |
| :--- | :--- | :--- |
| 订阅者表 `subscribers[]` | 24 × 24 = 576 B | `CONFIG_EVENT_BUS_MAX_SUBSCRIBERS` |
| 事件队列 | 64 × 8 B = 512 B | `CONFIG_EVENT_BUS_QUEUE_LEN`（占裸机队列池 128 单元）|
| 分派任务栈 | 2 KB（仅 RTOS 后端 / RTOS backends）| `CONFIG_EVENT_BUS_DISPATCH_STACK` |
| **模块总开关** | 默认**不编入** / not compiled by default | `CONFIG_EVENT_BUS` |

### 3.3 System（`system_cpp` 后端）

> 模块实测总开销（`CONFIG_SYSTEM=n` 省下的，arm-none-eabi-gcc -Os / Cortex-M4F）：**Flash ≈ 2.7 KB（2733 B）+ RAM ≈ 148 B（bss 128 + data 20）**——System 是代码密集模块，RAM 大头是栈监控表（128 B）与 safe_state；关掉只省 ~0.15 KB RAM，主要收益在 Flash。
> Measured module total (what `CONFIG_SYSTEM=n` saves): **Flash ≈ 2.7 KB (2733 B) + RAM ≈ 148 B (bss 128 + data 20)** — System is code-heavy; the RAM side is mostly the stack-monitor table (128 B) and safe_state. Disabling it saves ~0.15 KB RAM; the real win is Flash.
> **C 与 C++ 后端实测几乎无差异**：全量 RAM 完全一致（28396 B），Flash 仅差 16 B（C++ 略多，-fno-rtti/-fno-exceptions）。选后端纯粹是语言偏好，不影响资源占用；`CONFIG_SYSTEM_CMD` 仅 C++ 后端可用。
> **C vs C++ backend measured nearly identical**: identical total RAM (28396 B), Flash differs by only 16 B (C++ slightly larger, -fno-rtti/-fno-exceptions). The backend choice is pure language preference — no resource impact; `CONFIG_SYSTEM_CMD` is C++-only.

| 静态项 / Item | 默认 / Default | 可控开关 / Knob |
| :--- | :--- | :--- |
| SystemCmd 命令表 | ≈ 0.2 KB | `CONFIG_SYSTEM_CMD`（默认**不编入**）|
| 栈监控表 | 8 × 16 = 128 B | `CONFIG_BOARD_STACK_MONITOR_MAX_TASKS` |
| WDT / Scrubber | 框架看门狗默认开；Flash CRC 巡检默认**关** | `CONFIG_SYSTEM_WDT` / `CONFIG_SYSTEM_SCRUBBER` |

### 3.4 板级服务 / Board Services

| 静态项 / Item | 默认 / Default | 可控开关 / Knob |
| :--- | :--- | :--- |
| 键值配置存储 `s_entries[]` | 8 × ~104 B ≈ 0.8 KB | `MAX_ENTRIES`（`board/src/config_store.c`，板级可改）|
| 设备实例表 | `DEV_ID_COUNT` 个 `struct device`（随 DTS）| 板级 DTS |

### 3.5 VFS / Bus / 驱动（按 DTS 链入，非固定）

> 池大小自动 = 同名 compatible 的 DTS 节点数（`DTC_GEN_COUNT_*`，`#ifndef` 兜底 1）；**DTS 不声明即不链入**。模板见 `board/dtsi/vfs/` 与 `board/dtsi/drivers/`，配置头见 `board/define/`。
> Pools size themselves by the number of same-compatible DTS nodes (`DTC_GEN_COUNT_*`, fallback 1); **unused in DTS = not linked**. Templates: `board/dtsi/vfs/`, `board/dtsi/drivers/`; config headers: `board/define/`.

| 模块 / Module | 单实例典型 / Typical per instance | 说明 / Notes |
| :--- | :--- | :--- |
| `vfs-adc` | ≈ 0.6–1.9 KB | 私有池 + 按需 DMA 双缓冲（`DMA_BUFFER_SIZE=256`，仅 DMA 模式 claim）|
| `vfs-i2s` | ≈ 2–5.6 KB | host 环缓 `I2S_CIRC_FIFO_SIZE`（512）|
| `vfs-dac` | ≈ 1.5–2.5 KB | 缓冲 + 字段表 |
| 其余 vfs / bus | 各 0.2–1 KB | — |
| `drivers/*`（37 个）| 各 ≈ 0.2 KB | 池 = 自身 compatible 节点数 |

### 3.6 VIRQ 开关内存对比 / VIRQ On/Off Comparison

> 实测：arm-none-eabi-gcc（Cortex-M4F，-Os），默认 `.config`（`CONFIG_OSAL_NULL` + `CONFIG_SYSTEM_CPP`，`CONFIG_BOTTOM_HALF_QUEUE_DEPTH=16`，USB 关作统一基线）。
> Measured with arm-none-eabi-gcc (Cortex-M4F, -Os) under the default `.config` (`CONFIG_OSAL_NULL` + `CONFIG_SYSTEM_CPP`, `CONFIG_BOTTOM_HALF_QUEUE_DEPTH=16`; USB off as the common baseline).

| 变体 / Variant | Flash (text) | RAM (data+bss) |
| :--- | ---: | ---: |
| VIRQ 开 / On（默认）| 132627 B | 28396 B |
| VIRQ 关 / Off | 131599 B | 27108 B |
| **省下 / Saved** | **1028 B** | **1288 B**（bss 1280 + data 8）|

**省下的 1288 B RAM 构成（`interrupt.o` 模块实测）/ RAM breakdown of the savings：**

| 项 / Item | RAM | 说明 / Notes |
| :--- | ---: | :--- |
| 3 × VIRQ 注册表（72 槽）| **864 B** | `top_half[72]` + `work*[72]` + `arg[72]`，各 72×4 B；**固定成本，与使用量无关** |
| 全局下半部 poller | **320 B** | fifo 控制结构（64 B 对齐属性）+ `ring[16]×4 B` + 唤醒标志 |
| 2 × 下半部工作项（`g_adc`/`g_i2s`）| ~24 B | ADC/I2S DMA 下半部工作项（各 2 指针 + 3 原子位）|
| 对齐填充 / padding | ~32 B | 模块内 64 B 结构对齐 |
| 模块合计 / module total | **1240 B**（bss）+ 8 B（data）| `arm-none-eabi-size interrupt.o` 实测 |

> 全量对比的 bss 差（1280 B）比模块本身（1240 B）多出的 ~40 B 来自链接合并时的对齐/调用点残留。
> The full-build bss delta (1280 B) exceeds the module alone (1240 B) by ~40 B from merge-time alignment / call-site residue.

**Flash 构成：模块代码 512 B + 各调用点 gate 掉的注册/轮询代码 ~516 B（i2s_bus / vfs-adc / xtask / system_init）。**

**伸缩规则 / Scaling：**

- FIFO 深度每槽 4 B（`fifo_data_type` = `uintptr_t`）：`CONFIG_BOTTOM_HALF_QUEUE_DEPTH` 16→32 实测 poller 320→384 B，代码体积不变；
- 三张表固定 864 B：块数（`interrupt.h` 的 `VIRTUAL_IRQ_BLOCK_TABLE`）× `VIRTUAL_IRQ_BLOCK_SIZE`(8) × 3 表 × 4 B；绝大多数块实际只用 idx 0；
- 总代价 ≈ **1.26 KB RAM + 1 KB Flash**，其中 ~85% 是 bss 静态结构，不随业务量增减——省内存最有效的做法是整体关掉，而不是调深度。

**关闭的功能代价 / Functional cost of disabling（与 Kconfig help 一致）：** 裸机时间片调度器失去 tick 源、ADC/I2S 中断下半部与 GPIO 中断路由不可用、板级 ISR 不得再调 `interrupt_virtual_dispatch()` / `interrupt_virtual_register()`。

---

## 4. 裁剪指引 / Trimming Guide

| 目标 / Goal | 改哪里 / Where |
| :--- | :--- |
| 理解最小占用 | DTS 空着即可达到 §2.1 的 ≈ 2.8 KB；不要再加外设节点 |
| 整体去掉 System 层 | `CONFIG_SYSTEM=n`（连带 WDT/scrubber/EventBus）|
| 队列池（裸机）| 基础 `CONFIG_OSAL_NULL_MAX_QUEUES`（默认 0）+ EventBus 开启自动 +1；再调 `CONFIG_OSAL_NULL_QUEUE_BUF_SZ` 2048→512 |
| 精简 EventBus | 默认已关；开时调 `CONFIG_EVENT_BUS_QUEUE_LEN` / `MAX_SUBSCRIBERS` / `DISPATCH_STACK` |
| 精简键值存储 | `MAX_ENTRIES` 8→4（`board/src/config_store.c`）|
| RTOS 后端 | FreeRTOS 堆 `CONFIG_FREERTOS_HEAP_SIZE`；RTT 堆 `CONFIG_RTT_HEAP_SIZE` |
| VFS/驱动 | 板级 DTS 只声明用到的外设（`DTC_GEN_COUNT_*` 自动控制池大小）|
| 精确核算 | 用 `size`/`nm` 看 `.bss` / `.data`：`arm-none-eabi-size firmware.elf` |
| 可裁剪前提 | 这些开销全部编译期确定——Kconfig/宏改小即生效，无运行时波动 |

---

## 相关文档 / Related Documents

- [architecture.md](architecture.md) · [runtime_services.md](runtime_services.md)（安全积木按需）· [osal_switching.md](osal_switching.md)
- [ecosystem.md](ecosystem.md)（外部积木按需链接，不占固定开销）
