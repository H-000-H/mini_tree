# mini_tree 架构总览 / mini_tree Architecture Overview

> 当前中间件 shelf 的分层、启动、数据流与安全模型。不绑定厂商 SDK。
> Layering, startup, data flow, and safety model of the current middleware shelf. Not bound to any vendor SDK.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 需要改中间件或做深度移植的工程师 / Engineers modifying the middleware or doing deep porting |
| **前置 / Prerequisites** | 已读 [usage.md](usage.md) 术语表 / Have read the terminology in [usage.md](usage.md) |
| **相关 / Related** | [design_decisions.md](design_decisions.md) · [file_index.md](file_index.md) · [driver_guide.md](driver_guide.md) · [ecosystem.md](ecosystem.md) |

---

## 目录 / Table of Contents

1. [分层拓扑与契约 / 1. Layering Topology & Contracts](#1-分层拓扑与契约)
2. [模块职责 / 2. Module Responsibilities](#2-模块职责)
3. [启动时序（两段式点火）/ 3. Startup Sequence (Two-Phase Ignition)](#3-启动时序两段式点火)
4. [核心数据流 / 4. Core Data Flow](#4-核心数据流)
5. [配置与生成物 / 5. Configuration & Generated Artifacts](#5-配置与生成物)
6. [中断模型 / 6. Interrupt Model](#6-中断模型)
7. [安全架构 / 7. Safety Architecture](#7-安全架构)
8. [跨平台边界 / 8. Cross-Platform Boundary](#8-跨平台边界)

---

## 1. 分层拓扑与契约 / 1. Layering Topology & Contracts

分层拓扑（自顶向下调用，横向为基础能力）/ Layer topology (top-down calls; horizontal bands are base capabilities):

```
┌─────────────────────────────────────────────────────────────┐
│ Application / 平台业务                                       │
│   device_open/read/write/ioctl · EventBus · osal_task_*     │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ board/   device · driver · bus.h · lifecycle · config_store │
│          device_tree_init · board_driver_probe_all          │
└────────────────────────────┬────────────────────────────────┘
                             │ DRIVER_REGISTER → dtc-lite 表
┌────────────────────────────▼────────────────────────────────┐
│ vfs/     外设 file_operations（持 device 锁 / lifecycle）    │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ bus/     host/client 池 · 引用计数 · 对上层 poison hal_*     │
└────────────────────────────┬────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────┐
│ hal/     平台无关头 + weak 空 .c                             │
└────────────────────────────┬────────────────────────────────┘
                             │ 仅平台 .c include 厂商头
┌────────────────────────────▼────────────────────────────────┐
│ 厂商 SDK / LL / 标准外设库 / ESP-IDF …（不在本仓库）           │
└─────────────────────────────────────────────────────────────┘

横向: core · osal · interrupt · system_c|cpp · time_slice · can_hook · tools
```

### 1.1 层间契约 / Inter-Layer Contracts

| 边界 / Boundary | 允许 / Allowed | 禁止 / Forbidden |
| :--- | :--- | :--- |
| App → board | `device_*`、业务 ioctl / business ioctl | `hal_*`、厂商头 / vendor headers |
| vfs → bus | `*_bus_*` | 直接 `hal_*`（头文件 poison）/ direct `hal_*` (header poison) |
| bus → hal | 调用 HAL；`.c` 可定义 `*_BUS_IMPL` / call HAL; `.c` may define `*_BUS_IMPL` | 把厂商类型泄漏到公共头 / leaking vendor types into public headers |
| HAL 头 / HAL headers | `uintptr_t` / 整型宏字段 / integer macro fields | `SPI_HandleTypeDef` 等 typedef / typedefs like `SPI_HandleTypeDef` |

### 1.2 硬件直投 / Hardware Pass-Through

DTSI 中 `#include <厂商头>` → `cpp` 展开 → 属性写成整数 → VFS 填入 `hal_*_config` → 平台 HAL 灌入 LL/驱动。中间件**不**维护「Linux 式 enum → 厂商 enum」映射表。
In the DTSI, `#include <vendor_header>` → expanded by `cpp` → properties become integers → VFS fills `hal_*_config` → the platform HAL feeds them into the LL/driver. The middleware does **not** maintain a "Linux-style enum → vendor enum" mapping table.

---

## 2. 模块职责 / 2. Module Responsibilities

| 目录 / Directory | 职责 / Responsibility | 典型符号 / Typical Symbols |
| :--- | :--- | :--- |
| `board/` | 设备实例、probe 序、生命周期、配置聚合 / device instances, probe order, lifecycle, config aggregation | `device_find`、`DRIVER_REGISTER` |
| `vfs/` | 按 compatible 绑定的驱动 / drivers bound by compatible | `vfs_spi_probe`、uart/can/usb… |
| `bus/` | 控制器 host + client 会话 / controller host + client sessions | `spi_bus_open`、`can_bus_transmit` |
| `hal/` | 抽象寄存器操作 / abstract register operations | `hal_gpio_fast_set_level`、`hal_uart_write` |
| `core/` | 错误码、兼容宏、事件、缓冲池、日志 / error codes, compat macros, events, buffer pools, logging | `VFS_ERR_*`、`event_bus_*` |
| `osal/` | 锁/队列/任务/时间 / locks, queues, tasks, time | `osal_mutex_lock` |
| `interrupt/` | VIRQ、上/下半部 / VIRQ, top/bottom halves | `interrupt_virtual_dispatch` |
| `system_c` / `system_cpp` | 启动、WDT、scrubber、safe_state | `mini_tree_pre_os_init` / `mini_tree::system_pre_os_init` |
| `time_slice/` | 裸机协作式调度 / bare-metal cooperative scheduling | `x_scheduler` / `x_task`（仅 `OSAL_NULL`） |
| `drivers/<chip>/` | 产品驱动（37 个，`{include,src}` 结构）/ product drivers (37, `{include,src}` layout) | `DRIVER_REGISTER` / ioctl；dtc-lite 编译期 probe |
| `can_hook/` | CAN 钩子扩展 / CAN hook extensions | — |
| `lib/` + `cmake/*.cmake` | vendor：FreeRTOS / RT-Thread / ETL；TinyUSB / lwIP / cJSON 配置期 FetchContent；其余积木链接期 FetchContent / vendored: FreeRTOS / RT-Thread / ETL; TinyUSB / lwIP / cJSON configure-time FetchContent; other bricks link-time FetchContent | OSAL 内核按 Kconfig；其余 `mini_tree_link_*`（见 [ecosystem.md](ecosystem.md)） |

### 2.1 外设覆盖（当前）/ Peripheral Coverage (Current)

| 能力 / Capability | HAL | Bus | VFS |
| :---: | :---: | :---: | :---: |
| GPIO | ✓ | — | ✓ |
| SPI / UART / I2C / I2S | ✓ | ✓ | ✓ |
| CAN / USB | ✓ | ✓ | ✓ |
| ADC / DAC / TIM | ✓ | — | ✓ |
| RTC / IWDG / WWDG | ✓ | — | ✓ |
| AMP / storage / platform_safety | ✓ | — | — |

---

## 3. 启动时序（两段式点火）/ 3. Startup Sequence (Two-Phase Ignition)

### 3.1 C API（`system_c/include/system_init.h`）/ C API (`system_c/include/system_init.h`)

| 阶段 / Phase | API | 典型工作 / Typical Work |
| :---: | :--- | :--- |
| 1 | `mini_tree_pre_os_init()` | 关全局中断、EventBus、safe_state、可选 WDT、`device_tree_init` / disable global interrupts, EventBus, safe_state, optional WDT, `device_tree_init` |
| — | （可选）业务/平台准备 / (optional) business/platform prep | 静态配置、额外注册 / static config, extra registrations |
| 2 | `mini_tree_start_tasks()` | `board_driver_probe_all`、TWDT、Flash Scrubber |
| 3 | `system_init_complete()` | 释放全局中断 / re-enable global interrupts |
| 4 | 调度或裸机循环 / scheduler or bare-metal loop | `vTaskStartScheduler` / `rt_system_scheduler_start` / `mini_tree_system_loop` |

### 3.2 C++ API（`system_cpp`）/ C++ API (`system_cpp`)

`mini_tree::system_pre_os_init()` / `mini_tree::system_start_tasks()` 与上表阶段 1/2 对应，最后仍调用 `system_init_complete()`；另有 `extern "C"` 包装 `mini_tree_pre_os_init()` / `mini_tree_start_tasks()` 供 C 侧调用。
`mini_tree::system_pre_os_init()` / `mini_tree::system_start_tasks()` correspond to phases 1/2 in the table above and still end with `system_init_complete()`; `extern "C"` wrappers `mini_tree_pre_os_init()` / `mini_tree_start_tasks()` are also provided for the C side.

### 3.3 Probe 协作 / Probe Cooperation

dtc-lite 扫描 `DRIVER_REGISTER` 并生成 probe/remove 表，`board_driver_probe_all` 按序执行：
dtc-lite scans `DRIVER_REGISTER` and generates the probe/remove table; `board_driver_probe_all` executes in order:

```text
dtc-lite 扫描 DRIVER_REGISTER
  → 生成 probe/remove 表 + board_probe_order()
board_driver_probe_all()
  → 按序取 device → 调匹配的 probe_fn
  → 失败时按 criticality 告警或安全停机
```

---

## 4. 核心数据流 / 4. Core Data Flow

### 4.1 外设 I/O / Peripheral I/O

```text
device_read/write/ioctl
  → 持 device->lock（框架包装）
  → ops->read/write/ioctl (vfs)
  → *_bus_* （有 bus 的外设）
  → hal_* （平台实现）
```

### 4.2 EventBus

`core` 发布订阅；模块开关 `CONFIG_EVENT_BUS`（默认关闭，依赖 `SYSTEM`），容量由 `CONFIG_EVENT_BUS_QUEUE_LEN`、`CONFIG_EVENT_BUS_MAX_SUBSCRIBERS` 决定。
Publish/subscribe in `core`; module switch `CONFIG_EVENT_BUS` (off by default, depends on `SYSTEM`); capacity is set by `CONFIG_EVENT_BUS_QUEUE_LEN` and `CONFIG_EVENT_BUS_MAX_SUBSCRIBERS`.

### 4.3 BufferPool / algorithm

`buffer_pool` 提供池化块；`algorithm/buffer` 提供环形/双缓冲等结构，供驱动与业务复用。
`buffer_pool` provides pooled blocks; `algorithm/buffer` provides ring/double buffers and similar structures for reuse by drivers and business logic.

### 4.4 错误与指针 / Errors & Pointers

- 返回值：`int`，`VFS_OK` 或负的 `VFS_ERR_*` / Return values: `int`, `VFS_OK` or negative `VFS_ERR_*`
- 特殊指针：`ERR_PTR` / `IS_ERR` / `PTR_ERR`（依赖 `error_symbols.ld` 的 `ERR_SECTION_BASE`）/ Special pointers: `ERR_PTR` / `IS_ERR` / `PTR_ERR` (rely on `ERR_SECTION_BASE` from `error_symbols.ld`)

---

## 5. 配置与生成物 / 5. Configuration & Generated Artifacts

| 输入 / Input | 工具 / Tool | 输出目录（典型）/ Output Directory (Typical) |
| :--- | :--- | :--- |
| `Kconfig` + `.config` | `tools/genconfig.py` | `generated/kconfig/mini_tree/config.h` |
| `BOARD_DTS` + dtsi + `VENDOR_INC_*` | `tools/dtc-lite.py` | `generated/board/mini_tree/*` |
| scrubber stub | CMake copy | `generated/scrubber/.../system_scrubber_crc_gen.h` |
| 根 `compile_flags.txt` | `tools/gen_compile_db.py` | `compile_commands.json`（含 `.h/.hpp` 头文件条目 / including header entries） |

Kconfig 菜单：Platform、Multi-core/AMP、OSAL、Spinlock、System Log、System Runtime（`SYSTEM` 总开关 + C/CPP 后端）、Components（USB）、Board Features（WDT/Scrubber/…）、Runtime Capacity（`EVENT_BUS` 总开关 + 容量）、Compiler、Build。
Kconfig menus: Platform, Multi-core/AMP, OSAL, Spinlock, System Log, System Runtime (`SYSTEM` master switch + C/CPP backend), Components (USB), Board Features (WDT/Scrubber/…), Runtime Capacity (`EVENT_BUS` master switch + capacity), Compiler, Build.

CMake 关键缓存变量：`BOARD_DTS`、`BOARD_DTSI_DIR`、`VENDOR_INC_DIRS`、`VENDOR_DEFINES`。
Key CMake cache variables: `BOARD_DTS`, `BOARD_DTSI_DIR`, `VENDOR_INC_DIRS`, `VENDOR_DEFINES`.

---

## 6. 中断模型 / 6. Interrupt Model

`interrupt/interrupt.h`：

- **VIRQ**：按 block 划分（system/tim/gpio/adc/uart/spi/i2c/i2s/…），块大小 `VIRTUAL_IRQ_BLOCK_SIZE`（2 的幂）。
  **VIRQ**: divided into blocks (system/tim/gpio/adc/uart/spi/i2c/i2s/…), block size `VIRTUAL_IRQ_BLOCK_SIZE` (a power of two).
- **上半部 / Top half**：ISR 内 `top_half`，可自动 submit 下半部。/ `top_half` inside the ISR, can auto-submit the bottom half.
- **下半部 / Bottom half**：SPSC 队列；裸机主循环 `poll`，RTOS 可用任务 + sem。/ SPSC queue; bare-metal main loop `poll`s, RTOS may use a task + semaphore.
- **红线 / Red line**：ISR 内禁止 printf / 长时间锁 / 无界工作（见 [fast_path.md](fast_path.md)）。/ No printf / long-held locks / unbounded work inside ISRs (see [fast_path.md](fast_path.md)).

---

## 7. 安全架构 / 7. Safety Architecture

| 机制 / Mechanism | 作用 / Purpose |
| :--- | :--- |
| `safe_state` + `hal_platform_safety` / `hal_amp` | 进入安全态、关输出 / enter safe state, disable outputs |
| `board_safety_register_shutdown` | 仅 probe 阶段注册停机回调 / shutdown callbacks are registered only during probe |
| IWDG / Scrubber（Kconfig） | 看门狗与 Flash 巡检 / watchdog and flash inspection |
| `compiler_compat_poison` | 限制 malloc/printf/裸 mem* / restrict malloc/printf/bare mem* |
| `COMPAT_*` 内存 API | 替代被 poison 的 libc 符号 / replacements for the poisoned libc symbols |

---

## 8. 跨平台边界 / 8. Cross-Platform Boundary

| 本仓库提供 / This Repository Provides | 平台仓库提供 / The Platform Repository Provides |
| :--- | :--- |
| 中间件源码、weak HAL、占位 DTS、文档、ide stubs / middleware source, weak HAL, placeholder DTS, docs, IDE stubs | `hal_*_<soc>.c`、完整 dts/dtsi、厂商 `-I`、板级链接脚本与启动文件 / full dts/dtsi, vendor `-I`, board linker scripts and startup files |
| OSAL 三后端骨架 / three OSAL backend skeletons | 时钟、堆、SysTick/RTOS 端口（若需要）/ clocks, heap, SysTick/RTOS ports (if needed) |

通用 CMake 路径下，平台通过 `MINI_TREE_BOARD_PORT`（绝对路径）或同级 `board_port.cmake` 注入板级；ESP-IDF 路径由 `ESP_PLATFORM` 触发组件模式。验证矩阵以各 `platform/*/mini_tree` 工程为准，不在本 shelf 内绑定具体 SoC。
On the generic CMake path, the platform injects its board via `MINI_TREE_BOARD_PORT` (absolute path) or a sibling `board_port.cmake`; the ESP-IDF path is triggered by `ESP_PLATFORM` and uses the component mode. The validation matrix lives in each `platform/*/mini_tree` project; this shelf does not bind to a specific SoC.

---

## 相关文档 / Related Documents

- [usage.md](usage.md) · [getting_started.md](getting_started.md) · [ecosystem.md](ecosystem.md)
- [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md)
- [design_decisions.md](design_decisions.md) · [api_compatibility.md](api_compatibility.md)
