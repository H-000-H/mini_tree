# OSAL 后端切换注意事项

> 在 mini-os / FreeRTOS / RT-Thread / 裸机（NULL）之间切换时，必须复查的行为差异。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 改 `.config` 或维护多后端的人 |
| **前置** | [getting_started.md](getting_started.md) |
| **相关** | [faq.md](faq.md) · [architecture.md](architecture.md) |

---

## 目录

1. [后端对照表](#1-后端对照表)
2. [切换步骤](#2-切换步骤)
3. [优先级与调度](#3-优先级与调度)
4. [同步与 ISR](#4-同步与-isr)
5. [启动差异](#5-启动差异)
6. [容量与内存](#6-容量与内存)
7. [检查清单](#7-检查清单)

---

## 1. 后端对照表

| 宏 | 实现文件 | 链接依赖 | 任务模型 |
| :--- | :--- | :--- | :--- |
| `CONFIG_OSAL_NULL` | `osal/src/osal_null.c`<br>+ `osal/src/osal_task.cpp`（`CONFIG_OSAL_NULL_TASK_CPP=y` **且** `!XTASK_NONE` 时） | `time_slice/task`（`xtask_coop.c` 或 `xtask_preempt.c`, 由 `Kconfig.mini_tree` 裸机调度器 choice 三选一 `XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`; 共用 `xtask.h` API） | 无调度（`XTASK_NONE`, 自写 while）<br>**或** 协作式时间片（裸机, 默认 `XTASK_COOP`）<br>**或** N+1 抢占式（多优先级, `XTASK_PREEMPT`） |
| `CONFIG_OSAL_MINI_OS` | `osal/src/osal_mini_os.c` | `lib/mini-os`（自研内核，仅 Cortex-M；详见 [mini-os.md](mini-os.md)） | 抢占（32 级就绪位图 O(1)） |
| `CONFIG_OSAL_FREERTOS` | `osal/src/osal_freertos.c` | `lib/freeRTOS`（v11.3.0） | 抢占 |
| `CONFIG_OSAL_RTTHREAD` | `osal/src/osal_rtthread.c` | `lib/rtthread`（v5.3.0） | 抢占 |

裸机后端 (`CONFIG_OSAL_NULL`) 的任务调度器由 `Kconfig.mini_tree` 的「裸机调度器」choice 三选一（`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`）, CMake 据 `.config` 注入 `MINI_TREE_XTASK_*` 宏决定编译 `xtask_coop.c` 或 `xtask_preempt.c`, 源码 `#ifdef` 双重互斥:
- **无调度**（`XTASK_NONE`）— 不编入任何调度器, 应用层自写 `while(1)` 大循环; `OSAL_NULL_TASK_CPP` 由 Kconfig 自动关闭, osal/system 层依赖 xtask 接口无法链接, 固件退化为裸闭包.
- **协调式**（默认, `XTASK_COOP`）— `time_slice/task/xtask_coop.c`, round-robin 时间片轮转, 不可抢占.
- **抢占式**（`XTASK_PREEMPT`）— `time_slice/task/xtask_preempt.c`, N+1 链表多优先级（分组优先级 + CLZ 定位, 可延迟/可休眠/可抢占, 无就绪时精确 WFI）; 已完整实现可编译.

两套实现共用 `xtask.h` 对外 API (`xscheduler_task_create` / `x_scheduler_poll` / `xscheduler_start` 等), 调用方代码无需任何改动. `osal/src/osal_task.cpp` 与 `osal/include/osal_null.h` 中的 C++ 重载按 `CONFIG_XTASK_PREEMPT` 分两个分支:
- 协调式分支: `osal_task_create` 的 `period` 即任务周期 ms（裸机无优先级概念）.
- 抢占式分支: 同签名重载新增 `priority` 参数（数值越大越优先）, `stack_size` 在裸机下复用为周期.

公共表面：`osal/include/osal.h`。业务与 VFS 应只依赖该头。

`lib/` 现状：随仓 vendor 仅 **mini-os（自研）、FreeRTOS（v11.3.0）、RT-Thread（v5.3.0）、ETL**；TinyUSB / lwIP 为配置期 FetchContent，其余（littlefs、FatFs、MultiButton、MCUBoot、coreMQTT、LVGL、u8g2、FlashDB、SFUD、EasyFlash、EasyLogger）为链接期 FetchContent（`mini_tree_link_*`）。

---

## 2. 切换步骤

1. 改 `mini_tree/.config` 中 OSAL choice（互斥）。
2. 重新 `genconfig.py` / 重跑 CMake。
3. **全量重编**（勿混用旧 `config.h`）。
4. 按下文复查优先级、启动、栈、ISR。
5. 跑一遍关键外设与安全路径。

---

## 3. 优先级与调度

| 后端 | 数值语义 |
| :--- | :--- |
| FreeRTOS | 数值 **越大** 优先级越高 |
| RT-Thread | 数值 **越小** 优先级越高 |
| mini-os | 数值 **越小** 优先级越高（同 RT-Thread，与 FreeRTOS 相反） |
| NULL (协调式, `XTASK_COOP`) | C API 忽略优先级参数 |
| NULL (抢占式, `XTASK_PREEMPT`) | N+1 链表多优先级, 数值越大越优先 |

裸机任务创建路径由 `CONFIG_OSAL_NULL_TASK_CPP` 控制（依赖 `SYSTEM_CPP && !XTASK_NONE`, 默认开启）：
- **开启（走统一）**：用 `osal_null.h` 的 C++ 重载 `osal_task_create`.
  - 协调式: `period` 参数即任务周期 ms（裸机无优先级概念, 该位置被**重解释**为周期）.
  - 抢占式: 同重载新增 `priority` 参数（数值越大越优先）, `stack_size` 在裸机下复用为周期.
- **关闭（靠 xtask 自己, 或 `XTASK_NONE` 时强制）**：不编译封装, 直接调 `xscheduler_task_create` / `x_scheduler_poll` 等 xtask 原生 API.
- 裸机 C API `osal_task_create` / `osal_task_create_handle` 恒返回 `OSAL_ERR_NOTSUPP`.

> **抢占式开启 (`XTASK_PREEMPT=y`) 时**: C++ 重载仍提供, 但签名切换为带 `priority` 的分支（`stack_size` 复用为周期）; 协调式与抢占式分支在 `osal_task.cpp` 内以 `CONFIG_XTASK_PREEMPT` 区分, 无需业务改调用. 也可走 `XTASK_NONE` 直接裸写 `while` 循环.

同一套业务常量在切换后端时**必须**重新换算，否则会出现「高优先级任务饿死」或倒挂。

---

## 4. 同步与 ISR

- 只使用 `osal.h` 中标明可用于 ISR 的 API（若有）；不确定则假设 **不可** 在 ISR 拿 mutex。
- Spinlock 实现由 `CONFIG_OSAL_SPINLOCK_IRQ_DISABLE` / `ATOMIC` 选择；AMP 下倾向 atomic。
- 禁止业务直接 `#include` `semphr.h` / `rthw.h`。

### 4.1 FreeRTOS 接管 SVC / PendSV 中断与 CubeMX 代码的冲突（STM32 实测）

**现象**：切到 `CONFIG_OSAL_FREERTOS` 后，链接期报 `multiple definition of 'SVC_Handler'` 和 `multiple definition of 'PendSV_Handler'`。

**根因**：FreeRTOS 的任务上下文切换依赖两个内核中断，通过 `FreeRTOSConfig.h` 的宏把处理函数直接占住中断向量表：

```c
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
```

而 CubeMX 代码生成器（`stm32f1xx_it.c` 等）会**强定义**同名的 `SVC_Handler` 和 `PendSV_Handler` 作为中断服务程序。FreeRTOS 的强符号与 CubeMX 的强符号撞车，链接器报重定义。

**修复（在板级工程侧，不在 mini_tree 仓库内）**：把 CubeMX 生成的 `stm32f1xx_it.c` 里这两个函数改为 `__weak`，让 FreeRTOS 的强符号覆盖它们。这是 STM32 + FreeRTOS 集成的标准做法：

```c
__weak void SVC_Handler(void) { }
__weak void PendSV_Handler(void) { }
```

> 注意：mini_tree 仓库只提供 OSAL 封装与 RTOS 内核，板级中断向量表 / `stm32f1xx_it.c` 由使用方的板级工程提供。该修复应在你自己的板级工程里做，不要回提到中间件。
> 若改用 RT-Thread 后端，同理需确认 `rt_hw_context_switch` / `rt_hw_context_switch_interrupt` 使用的中断（通常是 PendSV）未被板级强符号抢占。
> 若改用 mini-os 后端，板级接线同理需注意：`SysTick_Handler` 转 `mini_os_systick_handler()`、`PendSV_Handler` 转 `pendsv_handler()`（小写，符号在 port.S），且链接脚本需包含 `lib/mini-os/mini-os-heap.ld`。

---

## 5. 启动差异

| 后端 | `system_init_complete` 之后 |
| :--- | :--- |
| NULL | `for(;;) mini_tree_system_loop();` |
| FreeRTOS | `vTaskStartScheduler();` |
| RT-Thread | `rt_system_scheduler_start();` |
| mini-os | `mini_os_schedule_start();`（`osal_scheduler_start` 内先惰性引导内核：`schedule_init` + idle 线程 + SysTick，再启动调度器） |

不要在 NULL 配置下链接并调用 RTOS 调度器入口。

---

## 6. 容量与内存

- **裸机队列池（仅 OSAL_NULL）**：`CONFIG_OSAL_NULL_MAX_QUEUES` 为**基础队列数**（默认 0，不占内存）；开启 `CONFIG_EVENT_BUS` 时**自动 +1**（EventBus 需要一个队列）。手动用 `osal_queue_create` → 在 Kconfig 设基础数。单队列缓冲 `CONFIG_OSAL_NULL_QUEUE_BUF_SZ`（默认 2048 B）。
- `CONFIG_OSAL_MUTEX_POOL_SIZE` 需覆盖 `DEV_ID_COUNT`（设备锁）及业务锁。
- **RTOS 堆由 Kconfig 控制**：FreeRTOS 动态堆 `CONFIG_FREERTOS_HEAP_SIZE`（默认 8 KB）、RT-Thread 独立静态堆 `CONFIG_RTT_HEAP_SIZE`（默认 32 KB）；mini-os 堆来自链接脚本区（`__mini_os_heap_start`/`__mini_os_heap_end`，无 Kconfig 堆大小，**不计入 bss**，容量改链接脚本）。
- 任务栈大小随后端栈开销变化，切换后重测水位。

---

## 7. 检查清单

- [ ] `.config` 与生成 `config.h` 一致
- [ ] 无同时编入两个 OSAL `.c`
- [ ] 裸机后端下「裸机调度器」choice 与预期一致 (`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT` 三选一; `xtask_coop.c` 与 `xtask_preempt.c` 互斥)
- [ ] 优先级表已按后端换算 (NULL 抢占式: 数值越大越优先)
- [ ] 裸机任务创建路径符合预期 (`CONFIG_OSAL_NULL_TASK_CPP`: 统一 C++ 重载 or 直接 xtask; `XTASK_NONE` 时强制关闭; 抢占式下重载带 `priority` 分支)
- [ ] 启动路径匹配后端
- [ ] 日志后端（PRINTF/OSAL）仍符合预期

---

## 相关文档

- [getting_started.md](getting_started.md) · [service_spec.md](service_spec.md)
- [mini-os.md](mini-os.md)（mini-os 内核专题） · [memory_footprint.md](memory_footprint.md)（四后端内存基准）
- [design_decisions.md](design_decisions.md)
