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
| `CONFIG_OSAL_NULL` | `osal/src/osal_null.c` | `time_slice/task/xtask` | 协作式时间片 |
| `CONFIG_OSAL_FREERTOS` | `osal/src/osal_freertos.c` | `lib/freeRTOS`（V11.3.0） | 抢占 |
| `CONFIG_OSAL_RTTHREAD` | `osal/src/osal_rtthread.c` | `lib/rtthread`（v5.3.0） | 抢占 |

公共表面：`osal/include/osal.h`。业务与 VFS 应只依赖该头。

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
| NULL | 忽略优先级参数 |

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

- `CONFIG_OSAL_MUTEX_POOL_SIZE` 需覆盖 `DEV_ID_COUNT`（设备锁）及业务锁。  
- FreeRTOS heap 编号、RT-Thread 堆配置在**平台**工程。  
- 任务栈大小随后端栈开销变化，切换后重测水位。  

---

## 7. 检查清单

- [ ] `.config` 与生成 `config.h` 一致  
- [ ] 无同时编入两个 OSAL `.c`  
- [ ] 优先级表已按后端换算  
- [ ] 启动路径匹配后端  
- [ ] 日志后端（PRINTF/OSAL）仍符合预期  

---

## 相关文档

- [getting_started.md](getting_started.md) · [service_spec.md](service_spec.md)  
- [design_decisions.md](design_decisions.md)
