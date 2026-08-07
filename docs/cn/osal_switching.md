# OSAL 后端切换注意事项

> 在 FreeRTOS / RT-Thread / 裸机（NULL）之间切换时，必须复查的行为差异。

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
| `CONFIG_OSAL_NULL` | `osal/src/osal_null.c`<br>+ `osal/src/osal_task.cpp`（`CONFIG_OSAL_NULL_TASK_CPP=y` **且** `CONFIG_XTASK_PREEMPT=n` 时） | `time_slice/task`（`xtask_coop.c` 或 `xtask_preempt.c`, 由 `CONFIG_XTASK_PREEMPT` 二选一; 共用 `xtask.h` API） | 协作式时间片（裸机, 默认）<br>**或** N+1 抢占式（实验性, `CONFIG_XTASK_PREEMPT=y`） |
| `CONFIG_OSAL_FREERTOS` | `osal/src/osal_freertos.c` | `lib/freeRTOS`（v11.3.0） | 抢占 |
| `CONFIG_OSAL_RTTHREAD` | `osal/src/osal_rtthread.c` | `lib/rtthread`（v5.3.0） | 抢占 |

裸机后端 (`CONFIG_OSAL_NULL`) 的任务调度器有两套实现, 由 `CONFIG_XTASK_PREEMPT` 二选一, CMake 与源码 `#ifdef` 双重互斥:
- **协调式**（默认, `CONFIG_XTASK_PREEMPT=n`）— `time_slice/task/xtask_coop.c`, round-robin 时间片轮转, 不可抢占.
- **抢占式**（实验性, `CONFIG_XTASK_PREEMPT=y`）— `time_slice/task/xtask_preempt.c`, N+1 链表多优先级; **尚未完工**, 开启后可能编不过.

两套实现共用 `xtask.h` 对外 API (`xscheduler_task_create` / `x_scheduler_poll` / `xscheduler_start` 等), 调用方代码无需任何改动. `osal/src/osal_task.cpp` 与 `osal/include/osal_null.h` 中的协调式 C++ 重载通过 `#ifndef CONFIG_XTASK_PREEMPT` 同步门控——开启抢占式时该重载整段关闭, 为未来抢占式专用重载预留位置.

公共表面：`osal/include/osal.h`。业务与 VFS 应只依赖该头。

`lib/` 现状：随仓 vendor 仅 **FreeRTOS（v11.3.0）、RT-Thread（v5.3.0）、ETL**；TinyUSB / lwIP / cJSON 为配置期 FetchContent，其余（LVGL、littlefs、FatFs、Mbed TLS、coreMQTT、coreHTTP、nanopb、MCUBoot、FreeModbus、libmodbus、CMSIS-DSP、MultiButton、EasyFlash、EasyLogger、FlashDB、u8g2、SFUD、miniz）为链接期 FetchContent（`mini_tree_link_*`）。

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
| NULL (协调式) | C API 忽略优先级参数 |
| NULL (抢占式, `CONFIG_XTASK_PREEMPT=y`) | N+1 链表多优先级 (尚未完工) |

裸机任务创建路径由 `CONFIG_OSAL_NULL_TASK_CPP` 控制（依赖 `SYSTEM_CPP`，默认开启）：
- **开启（走统一）**：用 `osal_null.h` 的 C++ 重载 `osal_task_create`，其 `period` 参数即任务周期 ms（裸机无优先级概念，该参数被**重解释**为周期）。
- **关闭（靠 xtask 自己）**：不编译封装，直接调 `xscheduler_task_create` / `x_scheduler_poll` 等 xtask 原生 API。
- 裸机 C API `osal_task_create` / `osal_task_create_handle` 恒返回 `OSAL_ERR_NOTSUPP`。

> **抢占式开启 (`CONFIG_XTASK_PREEMPT=y`) 时**: 协调式重载 (`osal_task.cpp` + `osal_null.h` 中的 C++ 声明) 通过 `#ifndef CONFIG_XTASK_PREEMPT` 整段关闭, 因为抢占式有优先级概念, `period` 参数语义会变化. 抢占式专用重载尚未提供, 当前需要 C 工程直接调 `xscheduler_task_create` 等原生 API (共用 `xtask.h`).

同一套业务常量在切换后端时**必须**重新换算，否则会出现「高优先级任务饿死」或倒挂。

---

## 4. 同步与 ISR

- 只使用 `osal.h` 中标明可用于 ISR 的 API（若有）；不确定则假设 **不可** 在 ISR 拿 mutex。
- Spinlock 实现由 `CONFIG_OSAL_SPINLOCK_IRQ_DISABLE` / `ATOMIC` 选择；AMP 下倾向 atomic。
- 禁止业务直接 `#include` `semphr.h` / `rthw.h`。

---

## 5. 启动差异

| 后端 | `system_init_complete` 之后 |
| :--- | :--- |
| NULL | `for(;;) mini_tree_system_loop();` |
| FreeRTOS | `vTaskStartScheduler();` |
| RT-Thread | `rt_system_scheduler_start();` |

不要在 NULL 配置下链接并调用 RTOS 调度器入口。

---

## 6. 容量与内存

- **裸机队列池（仅 OSAL_NULL）**：`CONFIG_OSAL_NULL_MAX_QUEUES` 为**基础队列数**（默认 0，不占内存）；开启 `CONFIG_EVENT_BUS` 时**自动 +1**（EventBus 需要一个队列）。手动用 `osal_queue_create` → 在 Kconfig 设基础数。单队列缓冲 `CONFIG_OSAL_NULL_QUEUE_BUF_SZ`（默认 2048 B）。
- `CONFIG_OSAL_MUTEX_POOL_SIZE` 需覆盖 `DEV_ID_COUNT`（设备锁）及业务锁。
- **RTOS 堆由 Kconfig 控制**：FreeRTOS 动态堆 `CONFIG_FREERTOS_HEAP_SIZE`（默认 8 KB）、RT-Thread 独立静态堆 `CONFIG_RTT_HEAP_SIZE`（默认 32 KB）。
- 任务栈大小随后端栈开销变化，切换后重测水位。

---

## 7. 检查清单

- [ ] `.config` 与生成 `config.h` 一致
- [ ] 无同时编入两个 OSAL `.c`
- [ ] 裸机后端下 `CONFIG_XTASK_PREEMPT` 与预期一致 (协调式 vs 抢占式二选一; `xtask_coop.c` 与 `xtask_preempt.c` 互斥)
- [ ] 优先级表已按后端换算
- [ ] 裸机任务创建路径符合预期 (`CONFIG_OSAL_NULL_TASK_CPP`: 统一 C++ 重载 or 直接 xtask; 注意抢占式开启时 C++ 重载关闭)
- [ ] 启动路径匹配后端
- [ ] 日志后端（PRINTF/OSAL）仍符合预期

---

## 相关文档

- [getting_started.md](getting_started.md) · [service_spec.md](service_spec.md)
- [design_decisions.md](design_decisions.md)
