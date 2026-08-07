# 运行时服务

> 启动后常用的横向能力：事件总线、VIRQ、系统语言后端、缓冲池，以及可选的看门狗 / CRC 巡检 / 安全停机模块。分层总览见 [architecture.md](architecture.md)。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 写业务任务 / 驱动下半部的人 |
| **前置** | [getting_started.md](getting_started.md) 点火 · [service_spec.md](service_spec.md) |
| **相关** | [fast_path.md](fast_path.md) · [amp.md](amp.md) · [ecosystem.md](ecosystem.md) |

---

## 目录

1. [EventBus](#1-eventbus)
2. [VIRQ 与上下半部](#2-virq-与上下半部)
3. [SYSTEM_C vs SYSTEM_CPP](#3-system_c-vs-system_cpp)
4. [BufferPool 与 algorithm/buffer](#4-bufferpool-与-algorithmbuffer)
5. [安全类可选模块（积木）](#5-安全类可选模块积木)

---

## 1. EventBus

> **可选模块（默认关闭）**：`CONFIG_EVENT_BUS`（依赖 `SYSTEM`）。开启后 `core/src/event_bus.c` 编入，`event_bus_*` API 可用；关闭（默认）则不编入、不广播 `EVENT_SYS_*`。

头：`core/include/event_bus.h`（C++ 另有 `event_bus.hpp` 包装）。

### 1.1 事件 ID

| 范围 | 宏 | 说明 |
| :--- | :--- | :--- |
| 框架 | `EVENT_SYS_BOOT` / `READY` / `FAULT` / `DEVICE_REMOVED` | 框架语义；业务勿滥用 |
| 用户 | `EVENT_USER_BASE`（`0x1000`）起 | 业务自定义：`EVENT_USER_BASE + n` |

框架**只搬运 ID + `uintptr_t arg`**，不解释业务含义。

### 1.2 API 要点

| API | 说明 |
| :--- | :--- |
| `event_bus_init` | 冷启动早期调用（已在 `mini_tree_pre_os_init` 路径） |
| `event_bus_subscribe(id_min, id_max, cb, user)` | 区间订阅 |
| `event_bus_post` / `post_from_isr` | 任务 / ISR 投递 |
| `event_bus_start` / `stop` | 运行控制 |
| `event_bus_seal` | **封口后禁止再 subscribe**（通常在启动完成后） |
| `event_bus_dropped_count` | 队列满丢弃计数 |

建议：

1. 业务订阅放在 `pre_os_init` 之后、`seal` 之前（或文档化的平台窗口）。
2. ISR 只用 `post_from_isr`，回调里不做重活。
3. `arg` 若为指针：生命周期必须活过回调（静态/池化，勿栈指针）。

容量：`CONFIG_EVENT_BUS_*`（见 Kconfig Runtime）；开关 `CONFIG_EVENT_BUS`。

---

## 2. VIRQ 与上下半部

头：`interrupt/interrupt.h`。

```text
硬件 IRQ
  → 平台 ISR（尽量短）
  → interrupt_virtual_dispatch(virq) / top_half
  → 自动 submit 下半部
  → interrupt_bottom_half_poll() 或 bottom_half 任务
  → bottom_half 回调（可 device_ioctl / EventBus / 协议）
```

| 概念 | 说明 |
| :--- | :--- |
| 虚拟块 | `system` / `tim` / `gpio` / `adc` / `uart` / `spi` / `i2c` / `i2s` / `user` 等 |
| 块大小 | `VIRTUAL_IRQ_BLOCK_SIZE`（须为 2 的幂） |
| 裸机 | 主循环调 `interrupt_bottom_half_poll`（常经 `mini_tree_system_loop`） |
| RTOS | 下半部任务 + sem 唤醒（实现条件编译） |

ISR 禁止：`printf`、长时间锁、无界工作 — [fast_path.md](fast_path.md)。

### 2.1 关闭 VIRQ

总开关 `CONFIG_VIRQ`（默认开）；关闭后 `interrupt/interrupt.c` 不编入，系统主循环不再 poll 下半部。

**关闭前请确认以下功能不需要（关闭后不可用，板级/驱动需同步裁剪）：**

| 功能 | 关闭后的影响 |
| :--- | :--- |
| 裸机时间片调度器（`time_slice/xtask`） | `xscheduler_start()` 不再注册 TIM 定时中断，调度器无 tick 源，`x_scheduler_poll()` 永不调度任务 |
| ADC / I2S DMA 中断下半部 | `vfs-adc` / `i2s_bus` 跳过 VIRQ 注册与中断使能，异步/DMA 回调不可用（轮询模式驱动不受影响） |
| GPIO 硬件中断路由 | `hal_gpio` 的 `virq_idx` 槽位形同虚设 |
| 板级/驱动 ISR | 不得再调 `interrupt_virtual_dispatch()` / `interrupt_virtual_register()`，否则链接失败 |

**内存收益 ≈ 1.3 KB RAM + 1 KB Flash**（三张 VIRQ 表 864 B + 下半部 poller 320 B + 工作项；FIFO 深度 `CONFIG_BOTTOM_HALF_QUEUE_DEPTH` 每槽 4 B）。详见 [memory_footprint.md](memory_footprint.md) §3.6。

---

## 3. SYSTEM_C vs SYSTEM_CPP

> **可选模块（默认自开）**：总开关 `CONFIG_SYSTEM`。关闭后 `system_c/` 与 `system_cpp/` 均不编入（`CONFIG_SYSTEM_WDT` / `CONFIG_SYSTEM_SCRUBBER` / `CONFIG_EVENT_BUS` 也依赖本开关）。

`CONFIG_SYSTEM` 开启时 Kconfig **二选一**：编入 `system_c/` 或 `system_cpp/`。

| | `SYSTEM_C` | `SYSTEM_CPP` |
| :--- | :--- | :--- |
| 头 | `system_c/include/system_init.h` | `system_cpp/include/system_init.hpp` |
| 阶段 1 | `mini_tree_pre_os_init()` | `mini_tree::system_pre_os_init()` |
| 阶段 2 | `mini_tree_start_tasks()` | `mini_tree::system_start_tasks()` |
| 收尾 | `system_init_complete()`（两侧共用 C） | 同左 |
| 裸机 loop | `mini_tree_system_loop()` | 同左（C API） |
| 依赖 | 更少 | **ETL 默认进库**（上层 C++ 基础 / heap-free C++ base）；根 CMake 常加 `-fno-rtti` / `-fno-exceptions` |

**如何选：**

- 固件整体偏 C、工具链无例外 → `SYSTEM_C`。
- 已有 C++ 业务 / 要用 `event_bus.hpp`、ETL 头 → `SYSTEM_CPP`（仓库默认 `.config` 常见为此）。
- 南向 HAL/VFS **仍是 C ABI**；换 SYSTEM 不改变外设栈语言。

---

## 4. BufferPool 与 algorithm/buffer

| 组件 | 路径 | 用途 |
| :--- | :--- | :--- |
| BufferPool | `core/include/buffer_pool.h` | 定长块池；驱动/协议借还 |
| 环形/双缓冲 | `algorithm/buffer/` | `fifo_spsc`、`double_buffer_spsc` 等结构 |

业务可直接用；勿在 ISR 里做复杂分配（池 API 是否 ISR-safe 以头文件注释为准）。

---

## 5. 安全类可选模块（积木）

> **这些模块是可选积木**：推荐启用/链接入库（默认随 `mini_tree` 编译），**但不启用也不影响核心功能开发**——按 Kconfig 关掉即可。

| 模块 | 功能 | Kconfig | 说明 |
| :--- | :--- | :--- | :--- |
| 看门狗 | `system_wdt`：IWDG / WWDG / TWDT | `CONFIG_SYSTEM_WDT` | 框架引导自动看门狗（IWDG/TWDT + 自动喂狗 + bootloop 防护）；应用可编程看门狗走 `vfs-iwdg`/`vfs-wwdg`（DTS） |
| Flash CRC 巡检 | `system_scrubber`：后台扫描 + CRC 基线 | `CONFIG_SYSTEM_SCRUBBER` | 掉电/位翻转防护；链接后由 `post_build_crc.py` 覆盖 CRC 基线 |
| 安全停机 | `safe_state` + `critical_data` + `hal_platform_safety` | `CONFIG_SAFETY_SHUTDOWN` | 停机回调、bootloop 防护、NMI 紧急标记、关键变量双反码存储、硬件闭锁 + 故障 LED/蜂鸣器 |
| 跨核急停 | `hal_cpu_emergency_stop_all_cores`（`hal/amp`） | `CONFIG_CPU_CORES > 1` | 双核 AMP 时须停所有核输出 |

要点：

1. **推荐**将上述模块链接入库并启用（生产环境默认开）；它们已是 `mini_tree` 库的一部分。
2. **不启用不影响开发**：关闭对应 Kconfig 后，核心（设备模型 / VFS / OSAL / EventBus）照常工作。
3. 与 **EventBus 封表（`seal`）无关**——封表是核心运行行为，不是可选积木。

---

## 相关文档

- [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [fast_path.md](fast_path.md)
- [amp.md](amp.md) · [peripherals.md](peripherals.md) · [ecosystem.md](ecosystem.md)
