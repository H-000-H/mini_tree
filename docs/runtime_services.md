# 运行时服务 / Runtime Services（EventBus · 中断 / Interrupts · SYSTEM_C/CPP · 缓冲 / Buffers）

> 启动后常用的横向能力：事件总线、VIRQ、系统语言后端、缓冲池，以及可选的看门狗 / CRC 巡检 / 安全停机模块。
> Horizontal capabilities used after boot: event bus, VIRQ, system-language backends, buffer pools, plus optional watchdog / CRC scrubber / safe-state modules.
> 分层总览见 [architecture.md](architecture.md) / Layer overview: [architecture.md](architecture.md).

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 写业务任务 / 驱动下半部的人 / Business-task & bottom-half writers |
| **前置 / Prereq** | [getting_started.md](getting_started.md) 点火 / boot · [service_spec.md](service_spec.md) |
| **相关 / Related** | [fast_path.md](fast_path.md) · [amp.md](amp.md) · [ecosystem.md](ecosystem.md) |

---

## 目录 / Contents

1. [EventBus](#1-eventbus)
2. [VIRQ 与上下半部 / VIRQ & Top/Bottom Halves](#2-virq-与上下半部--virq--topbottom-halves)
3. [SYSTEM_C vs SYSTEM_CPP](#3-system_c-vs-system_cpp)
4. [BufferPool 与 algorithm/buffer / BufferPool & algorithm/buffer](#4-bufferpool-与-algorithmbuffer--bufferpool--algorithmbuffer)
5. [安全类可选模块（积木）/ Optional Safety Modules (Bricks)](#5-安全类可选模块积木--optional-safety-modules-bricks)

---

## 1. EventBus

头 / Header：`core/include/event_bus.h`（C++ 另有 `event_bus.hpp` 包装 / plus the `event_bus.hpp` C++ wrapper）。

### 1.1 事件 ID / Event IDs

| 范围 / Range | 宏 / Macro | 说明 / Notes |
| :--- | :--- | :--- |
| 框架 / Framework | `EVENT_SYS_BOOT` / `READY` / `FAULT` / `DEVICE_REMOVED` | 框架语义；业务勿滥用 / Framework semantics; avoid in business code |
| 用户 / User | `EVENT_USER_BASE`（`0x1000`）起 / onwards | 业务自定义：`EVENT_USER_BASE + n` |

框架**只搬运 ID + `uintptr_t arg`**，不解释业务含义。
The framework only carries **ID + `uintptr_t arg`**; it never interprets business meaning.

### 1.2 API 要点 / API Highlights

| API | 说明 / Notes |
| :--- | :--- |
| `event_bus_init` | 冷启动早期调用（已在 `mini_tree_pre_os_init` 路径）/ Called early at cold boot (already on the `mini_tree_pre_os_init` path) |
| `event_bus_subscribe(id_min, id_max, cb, user)` | 区间订阅 / Range subscription |
| `event_bus_post` / `post_from_isr` | 任务 / ISR 投递 / Task / ISR posting |
| `event_bus_start` / `stop` | 运行控制 / Run control |
| `event_bus_seal` | **封口后禁止再 subscribe**（通常在启动完成后）/ **No further `subscribe` after seal** (typically after boot completes) |
| `event_bus_dropped_count` | 队列满丢弃计数 / Dropped-event counter when the queue is full |

建议 / Recommendations：

1. 业务订阅放在 `pre_os_init` 之后、`seal` 之前（或文档化的平台窗口）。
   Subscribe after `pre_os_init` and before `seal` (or within a documented platform window).
2. ISR 只用 `post_from_isr`，回调里不做重活。
   ISRs must use only `post_from_isr`; keep callbacks light.
3. `arg` 若为指针：生命周期必须活过回调（静态/池化，勿栈指针）。
   If `arg` is a pointer, it must outlive the callback (static/pooled; never a stack pointer).

容量 / Capacity：`CONFIG_EVENT_BUS_*`（见 Kconfig Runtime）。

---

## 2. VIRQ 与上下半部 / VIRQ & Top/Bottom Halves

头 / Header：`interrupt/interrupt.h`。

```text
硬件 IRQ / HW IRQ
  → 平台 ISR（尽量短 / keep short）
  → interrupt_virtual_dispatch(virq) / top_half
  → 自动 submit 下半部 / auto-submit bottom half
  → interrupt_bottom_half_poll() 或 bottom_half 任务 / or a bottom-half task
  → bottom_half 回调（可 device_ioctl / EventBus / 协议）/ bottom-half callback (device_ioctl / EventBus / protocol)
```

| 概念 / Concept | 说明 / Notes |
| :--- | :--- |
| 虚拟块 / Virtual blocks | `system` / `tim` / `gpio` / `adc` / `uart` / `spi` / `i2c` / `i2s` / `user` 等 / etc. |
| 块大小 / Block size | `VIRTUAL_IRQ_BLOCK_SIZE`（须为 2 的幂 / power of two） |
| 裸机 / Bare-metal | 主循环调 `interrupt_bottom_half_poll`（常经 `mini_tree_system_loop`）/ Main loop polls `interrupt_bottom_half_poll` (usually via `mini_tree_system_loop`) |
| RTOS | 下半部任务 + sem 唤醒（实现条件编译）/ Bottom-half task + semaphore wake (compile-time) |

ISR 禁止：`printf`、长时间锁、无界工作 — [fast_path.md](fast_path.md)。
ISR forbidden: `printf`, long-held locks, unbounded work — [fast_path.md](fast_path.md).

---

## 3. SYSTEM_C vs SYSTEM_CPP

Kconfig **二选一**：编入 `system_c/` 或 `system_cpp/`。
Kconfig picks **one**: compile `system_c/` or `system_cpp/`.

| | `SYSTEM_C` | `SYSTEM_CPP` |
| :--- | :--- | :--- |
| 头 / Header | `system_c/include/system_init.h` | `system_cpp/include/system_init.hpp` |
| 阶段 1 / Phase 1 | `mini_tree_pre_os_init()` | `mini_tree::system_pre_os_init()` |
| 阶段 2 / Phase 2 | `mini_tree_start_tasks()` | `mini_tree::system_start_tasks()` |
| 收尾 / Finalize | `system_init_complete()`（两侧共用 C / shared C） | 同左 / same |
| 裸机 loop / Bare loop | `mini_tree_system_loop()` | 同左（C API）/ same (C API) |
| 依赖 / Deps | 更少 / fewer | **ETL 默认进库**（上层 C++ 基础 / heap-free C++ base）；根 CMake 常加 `-fno-rtti` / `-fno-exceptions` |

**如何选 / How to choose：**

- 固件整体偏 C、工具链无例外 → `SYSTEM_C`。
  Firmware is mostly C, no toolchain exception → `SYSTEM_C`.
- 已有 C++ 业务 / 要用 `event_bus.hpp`、ETL 头 → `SYSTEM_CPP`（仓库默认 `.config` 常见为此）。
  Existing C++ business / need `event_bus.hpp` or ETL → `SYSTEM_CPP` (the repo's default `.config` usually does).
- 南向 HAL/VFS **仍是 C ABI**；换 SYSTEM 不改变外设栈语言。
  Southbound HAL/VFS stays **C ABI**; switching SYSTEM backends never changes the peripheral-stack language.

---

## 4. BufferPool 与 algorithm/buffer / BufferPool & algorithm/buffer

| 组件 / Component | 路径 / Path | 用途 / Use |
| :--- | :--- | :--- |
| BufferPool | `core/include/buffer_pool.h` | 定长块池；驱动/协议借还 / Fixed-size block pool; driver/protocol borrow-return |
| 环形/双缓冲 / Ring & double buffer | `algorithm/buffer/` | `fifo_spsc`、`double_buffer_spsc` 等结构 / structures |

业务可直接用；勿在 ISR 里做复杂分配（池 API 是否 ISR-safe 以头文件注释为准）。
Business code may use them directly; avoid complex allocation in ISRs (pool ISR-safety is documented per-header).

---

## 5. 安全类可选模块（积木）/ Optional Safety Modules (Bricks)

> **这些模块是可选积木**：推荐启用/链接入库（默认随 `mini_tree` 编译），**但不启用也不影响核心功能开发**——按 Kconfig 关掉即可。
> These modules are **optional bricks**: linking them into the library is recommended (they compile into `mini_tree` by default), but **not enabling them does not block core development** — just turn off the Kconfig switches.

| 模块 / Module | 功能 / Function | Kconfig | 说明 / Notes |
| :--- | :--- | :--- | :--- |
| 看门狗 / Watchdog | `system_wdt`：IWDG / WWDG / TWDT | `CONFIG_ENABLE_WDT` | 任务/硬件看门狗，喂狗超时触发复位或安全回调 / HW & task watchdogs; timeout triggers reset or safe callback |
| Flash CRC 巡检 / CRC Scrubber | `system_scrubber`：后台扫描 + CRC 基线 | `CONFIG_ENABLE_FLASH_SCRUBBER` | 掉电/位翻转防护；链接后由 `post_build_crc.py` 覆盖 CRC 基线 / Bit-rot scan + CRC baseline (overwritten post-link by `post_build_crc.py`) |
| 安全停机 / Safe State | `safe_state` + `critical_data` + `hal_platform_safety` | `CONFIG_SAFETY_SHUTDOWN` | 停机回调、bootloop 防护、NMI 紧急标记、关键变量双反码存储、硬件闭锁 + 故障 LED/蜂鸣器 / Shutdown callbacks, bootloop guard, NMI stamp, dual-inverted critical storage, hardware latch + fault LED/buzzer |
| 跨核急停 / CPU Stop | `hal_cpu_emergency_stop_all_cores`（`hal/amp`） | `CONFIG_CPU_CORES > 1` | 双核 AMP 时须停所有核输出 / Stop all cores on AMP |

要点 / Key points：

1. **推荐**将上述模块链接入库并启用（生产环境默认开）；它们已是 `mini_tree` 库的一部分。
   **Recommended**: link and enable these modules (default on for production); they are already part of the `mini_tree` library.
2. **不启用不影响开发**：关闭对应 Kconfig 后，核心（设备模型 / VFS / OSAL / EventBus）照常工作。
   **Optional**: with the Kconfig switches off, the core (device model / VFS / OSAL / EventBus) keeps working.
3. 与 **EventBus 封表（`seal`）无关**——封表是核心运行行为，不是可选积木。
   Unrelated to **EventBus seal** — sealing is core runtime behavior, not an optional brick.

---

## 相关文档 / Related Documents

- [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [fast_path.md](fast_path.md)
- [amp.md](amp.md) · [peripherals.md](peripherals.md) · [ecosystem.md](ecosystem.md)
