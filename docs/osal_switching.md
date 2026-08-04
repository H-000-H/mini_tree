# OSAL 后端切换注意事项 / OSAL Backend Switching Notes

> 在 FreeRTOS / RT-Thread / 裸机（NULL）之间切换时，必须复查的行为差异。
> Behavioral differences you must re-check when switching between FreeRTOS / RT-Thread / bare-metal (NULL).

| 项 / Item | 内容 / Description |
| :--- | :--- |
| **读者 / Audience** | 改 `.config` 或维护多后端的人<br>People editing `.config` or maintaining multiple backends |
| **前置 / Prereq.** | [getting_started.md](getting_started.md) |
| **相关 / Related** | [faq.md](faq.md) · [architecture.md](architecture.md) |

---

## 目录 / Table of Contents

1. [后端对照表 / Backend Comparison](#1-后端对照表-backend-comparison)
2. [切换步骤 / Switching Steps](#2-切换步骤-switching-steps)
3. [优先级与调度 / Priority and Scheduling](#3-优先级与调度-priority-and-scheduling)
4. [同步与 ISR / Synchronization and ISR](#4-同步与-isr-synchronization-and-isr)
5. [启动差异 / Startup Differences](#5-启动差异-startup-differences)
6. [容量与内存 / Capacity and Memory](#6-容量与内存-capacity-and-memory)
7. [检查清单 / Checklist](#7-检查清单-checklist)

---

## 1. 后端对照表 / Backend Comparison

| 宏 / Macro | 实现文件 / Implementation | 链接依赖 / Link deps | 任务模型 / Task model |
| :--- | :--- | :--- | :--- |
| `CONFIG_OSAL_NULL` | `osal/src/osal_null.c`<br>+ `osal/src/osal_task.cpp`（`CONFIG_OSAL_NULL_TASK_CPP=y` 时） | `time_slice/task`（`x_task` / `x_scheduler`） | 协作式时间片（裸机）<br>Cooperative time slices (bare-metal) |
| `CONFIG_OSAL_FREERTOS` | `osal/src/osal_freertos.c` | `lib/freeRTOS`（v11.3.0） | 抢占<br>Preemptive |
| `CONFIG_OSAL_RTTHREAD` | `osal/src/osal_rtthread.c` | `lib/rtthread`（v5.3.0） | 抢占<br>Preemptive |

公共表面：`osal/include/osal.h`。业务与 VFS 应只依赖该头。
Public surface: `osal/include/osal.h`. Business code and VFS should depend on this header only.

`lib/` 现状：随仓 vendor 仅 **FreeRTOS（v11.3.0）、RT-Thread（v5.3.0）、ETL**；全部积木（TinyUSB / lwIP / cJSON 及 LVGL、littlefs、FatFs、Mbed TLS、coreMQTT、coreHTTP、nanopb、MCUBoot、FreeModbus、libmodbus、CMSIS-DSP、MultiButton、EasyFlash、EasyLogger、FlashDB、u8g2、SFUD、miniz）均为链接期 FetchContent（`mini_tree_link_*`）。

Current `lib/` state: only **FreeRTOS (v11.3.0), RT-Thread (v5.3.0), and ETL** are vendored; TinyUSB / lwIP / cJSON are config-time FetchContent, and the rest (LVGL, littlefs, FatFs, Mbed TLS, coreMQTT, coreHTTP, nanopb, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB, u8g2, SFUD, miniz) are link-time FetchContent (`mini_tree_link_*`).

---

## 2. 切换步骤 / Switching Steps

1. 改 `mini_tree/.config` 中 OSAL choice（互斥）。
   Change the OSAL choice in `mini_tree/.config` (mutually exclusive).
2. 重新 `genconfig.py` / 重跑 CMake。
   Re-run `genconfig.py` / re-run CMake.
3. **全量重编**（勿混用旧 `config.h`）。
   **Full rebuild** (don't mix in a stale `config.h`).
4. 按下文复查优先级、启动、栈、ISR。
   Re-check priorities, startup, stacks, and ISRs per the sections below.
5. 跑一遍关键外设与安全路径。
   Exercise the critical peripherals and safety paths.

---

## 3. 优先级与调度 / Priority and Scheduling

| 后端 / Backend | 数值语义 / Numeric semantics |
| :--- | :--- |
| FreeRTOS | 数值 **越大** 优先级越高<br>**Higher** number = higher priority |
| RT-Thread | 数值 **越小** 优先级越高<br>**Lower** number = higher priority |
| NULL | C API 忽略优先级参数<br>C API ignores priority arguments |

裸机任务创建路径由 `CONFIG_OSAL_NULL_TASK_CPP` 控制（依赖 `SYSTEM_CPP`，默认开启）：
- **开启（走统一）**：用 `osal_null.h` 的 C++ 重载 `osal_task_create`，其 `period` 参数即任务周期 ms（裸机无优先级概念，该参数被**重解释**为周期）。
- **关闭（靠 xtask 自己）**：不编译封装，直接调 `xscheduler_task_create` / `x_scheduler_poll` 等 xtask 原生 API。
- 裸机 C API `osal_task_create` / `osal_task_create_handle` 恒返回 `OSAL_ERR_NOTSUPP`。

Bare-metal task creation is controlled by `CONFIG_OSAL_NULL_TASK_CPP` (depends on `SYSTEM_CPP`, on by default):
- **On (unified path)**: the C++ overload `osal_task_create` in `osal_null.h`; its `period` parameter is the task period in ms (bare-metal has no priority concept — the argument is **reinterpreted** as period).
- **Off (raw xtask)**: the wrapper is not compiled; call `xscheduler_task_create` / `x_scheduler_poll` directly.
- The bare-metal C API `osal_task_create` / `osal_task_create_handle` always returns `OSAL_ERR_NOTSUPP`.

同一套业务常量在切换后端时**必须**重新换算，否则会出现「高优先级任务饿死」或倒挂。
The same business constants **must** be re-mapped when switching backends, or you get "high-priority starvation" or inverted priorities.

---

## 4. 同步与 ISR / Synchronization and ISR

- 只使用 `osal.h` 中标明可用于 ISR 的 API（若有）；不确定则假设 **不可** 在 ISR 拿 mutex。
  Only use APIs in `osal.h` marked ISR-safe (if any); when in doubt, assume mutexes are **not** safe in ISRs.
- Spinlock 实现由 `CONFIG_OSAL_SPINLOCK_IRQ_DISABLE` / `ATOMIC` 选择；AMP 下倾向 atomic。
  The spinlock implementation is selected by `CONFIG_OSAL_SPINLOCK_IRQ_DISABLE` / `ATOMIC`; prefer atomic under AMP.
- 禁止业务直接 `#include` `semphr.h` / `rthw.h`。
  Business code must not `#include` `semphr.h` / `rthw.h` directly.

---

## 5. 启动差异 / Startup Differences

| 后端 / Backend | `system_init_complete` 之后<br>After `system_init_complete` |
| :--- | :--- |
| NULL | `for(;;) mini_tree_system_loop();` |
| FreeRTOS | `vTaskStartScheduler();` |
| RT-Thread | `rt_system_scheduler_start();` |

不要在 NULL 配置下链接并调用 RTOS 调度器入口。
Don't link or call RTOS scheduler entry points under a NULL configuration.

---

## 6. 容量与内存 / Capacity and Memory

- **裸机队列池（仅 OSAL_NULL）**：`CONFIG_OSAL_NULL_MAX_QUEUES` 为**基础队列数**（默认 0，不占内存）；开启 `CONFIG_EVENT_BUS` 时**自动 +1**（EventBus 需要一个队列）。手动用 `osal_queue_create` → 在 Kconfig 设基础数。单队列缓冲 `CONFIG_OSAL_NULL_QUEUE_BUF_SZ`（默认 2048 B）。
  **Bare-metal queue pool (OSAL_NULL only)**: `CONFIG_OSAL_NULL_MAX_QUEUES` is the **base queue count** (default 0, no RAM); enabling `CONFIG_EVENT_BUS` **auto-adds 1** (EventBus needs a queue). Manual `osal_queue_create` → set the base in Kconfig. Per-queue buffer `CONFIG_OSAL_NULL_QUEUE_BUF_SZ` (2048 B).
- `CONFIG_OSAL_MUTEX_POOL_SIZE` 需覆盖 `DEV_ID_COUNT`（设备锁）及业务锁。
  `CONFIG_OSAL_MUTEX_POOL_SIZE` must cover `DEV_ID_COUNT` (device locks) plus business locks.
- **RTOS 堆由 Kconfig 控制**：FreeRTOS 动态堆 `CONFIG_FREERTOS_HEAP_SIZE`（默认 8 KB）、RT-Thread 独立静态堆 `CONFIG_RTT_HEAP_SIZE`（默认 32 KB）。
  **RTOS heaps are Kconfig-gated**: FreeRTOS dynamic heap `CONFIG_FREERTOS_HEAP_SIZE` (8 KB), RT-Thread static heap `CONFIG_RTT_HEAP_SIZE` (32 KB).
- 任务栈大小随后端栈开销变化，切换后重测水位。
  Task stack size varies with backend stack overhead; re-measure headroom after switching.

---

## 7. 检查清单 / Checklist

- [ ] `.config` 与生成 `config.h` 一致
- [ ] `.config` and the generated `config.h` agree
- [ ] 无同时编入两个 OSAL `.c`
- [ ] No two OSAL `.c` files compiled at once
- [ ] 优先级表已按后端换算
- [ ] Priority table re-mapped for the backend
- [ ] 裸机任务创建路径符合预期（`CONFIG_OSAL_NULL_TASK_CPP`：统一 C++ 重载 or 直接 xtask）
- [ ] Bare-metal task-creation path is as intended (`CONFIG_OSAL_NULL_TASK_CPP`: unified C++ overload or raw xtask)
- [ ] 启动路径匹配后端
- [ ] Startup path matches the backend
- [ ] 日志后端（PRINTF/OSAL）仍符合预期
- [ ] Log backend (PRINTF/OSAL) still behaves as expected

---

## 相关文档 / Related Docs

- [getting_started.md](getting_started.md) · [service_spec.md](service_spec.md)
- [design_decisions.md](design_decisions.md)
