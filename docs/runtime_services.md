# 运行时服务（EventBus · 中断 · SYSTEM_C/CPP · 缓冲）

> 启动后常用横向能力：事件总线、VIRQ、系统语言后端、缓冲池。  
> 分层总览仍见 [architecture.md](architecture.md)。

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
5. [Storage / Safety 钩子](#5-storage--safety-钩子)

---

## 1. EventBus

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

容量：`CONFIG_EVENT_BUS_*`（见 Kconfig Runtime）。

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

---

## 3. SYSTEM_C vs SYSTEM_CPP

Kconfig **二选一**：编入 `system_c/` 或 `system_cpp/`。

| | `SYSTEM_C` | `SYSTEM_CPP` |
| :--- | :--- | :--- |
| 头 | `system_c/include/system_init.h` | `system_cpp/include/system_init.hpp` |
| 阶段 1 | `mini_tree_pre_os_init()` | `MiniTree::System_Pre_OS_Init()` |
| 阶段 2 | `mini_tree_start_tasks()` | `MiniTree::System_Start_Tasks()` |
| 收尾 | `system_init_complete()`（两侧共用 C） | 同左 |
| 裸机 loop | `mini_tree_system_loop()` | 同左（C API） |
| 依赖 | 更少 | **ETL 默认进库**（上层 C++ 基础；配置期解析）；根 CMake 常加 `-fno-rtti` / `-fno-exceptions` |

**如何选：**

- 固件整体偏 C、工具链无例外 → `SYSTEM_C`。  
- 已有 C++ 业务 / 要用 `event_bus.hpp`、ETL 头 → `SYSTEM_CPP`（仓库默认 `.config` 常见为此）。ETL 为上层 C++ 基础且默认进 `mini_tree`，见 [ecosystem.md](ecosystem.md)。
- 南向 HAL/VFS **仍是 C ABI**；换 SYSTEM 不改变外设栈语言。

---

## 4. BufferPool 与 algorithm/buffer

| 组件 | 路径 | 用途 |
| :--- | :--- | :--- |
| BufferPool | `core/include/buffer_pool.h` | 定长块池；驱动/协议借还 |
| 环形/双缓冲 | `algorithm/buffer/` | `circle_fifo`、`double_buffer` 等结构 |

业务可直接用；勿在 ISR 里做复杂分配（池 API 是否 ISR-safe 以头文件注释为准）。

---

## 5. Storage / Safety 钩子

| 模块 | 说明 |
| :--- | :--- |
| `hal_storage_*` | 双槽 A/B + flag；给板级配置持久化；平台实现真正 Flash/EEPROM |
| `hal_platform_safety_*` | 进 `safe_state` 时关功率级等 |
| `board,safety-hw` | DTS 节点；probe 注册 shutdown |

无 VFS 节点时由系统模块直接调 HAL；有 Flash 文件系统需求时再用 `winbond,w25q64` 等驱动。

---

## 相关文档

- [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [fast_path.md](fast_path.md)  
- [amp.md](amp.md) · [peripherals.md](peripherals.md) · [ecosystem.md](ecosystem.md)
