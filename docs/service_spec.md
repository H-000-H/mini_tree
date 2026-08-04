# 服务编写规范 / Service Specification（应用层解耦 / App-Layer Decoupling）

> 业务代码允许依赖什么、禁止依赖什么；如何挂到两段式启动上。
> What business code may or may not depend on; how to hook into the two-phase boot.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 应用 / 服务作者 / App & service authors |
| **前置 / Prereq** | [getting_started.md](getting_started.md) § 点火 / boot |
| **相关 / Related** | [fast_path.md](fast_path.md) · [peripherals.md](peripherals.md) · [api_compatibility.md](api_compatibility.md) |

---

## 目录 / Contents

1. [允许的依赖 / Allowed Dependencies](#1-允许的依赖--allowed-dependencies)
2. [禁止的依赖 / Forbidden Dependencies](#2-禁止的依赖--forbidden-dependencies)
3. [I/O 与错误处理 / I/O & Error Handling](#3-io-与错误处理--io--error-handling)
4. [任务与同步 / Tasks & Synchronization](#4-任务与同步--tasks--synchronization)
5. [与启动顺序的关系 / Boot Order](#5-与启动顺序的关系--boot-order)
6. [检查清单 / Checklist](#6-检查清单--checklist)

---

## 1. 允许的依赖 / Allowed Dependencies

| 头 / 模块 / Header & Module | 用途 / Use |
| :--- | :--- |
| `device.h` | 查找设备、open/read/write/ioctl / device lookup & I/O |
| `status.h` | `VFS_OK` / `VFS_ERR_*` |
| `osal.h` | 任务、锁、队列、延时、日志级别配合 / tasks, locks, queues, delays, log levels |
| `event_bus.h` / `event_bus.hpp` | 发布订阅 / pub-sub |
| `buffer_pool.h` · `algorithm/buffer` | 缓冲 / buffers |
| `system_log.h` | `SYS_LOGI/W/E` |
| 业务自身头 / own headers | — |

---

## 2. 禁止的依赖 / Forbidden Dependencies

| 禁止 / Forbidden | 原因 / Why |
| :--- | :--- |
| `hal_*.h`（业务里 / in business code） | 破坏分层；应走 device/vfs / breaks layering; go through device/vfs |
| `FreeRTOS.h` / `rtthread.h`（业务里 / in business code） | 应走 OSAL，便于切后端 / use OSAL for backend portability |
| 厂商寄存器头 / Vendor register headers | 不可移植 / not portable |
| 随意 `malloc` / `printf` / `memset` / ad-hoc | 可能被 `compiler_compat_poison` 毒杀；用 `COMPAT_MEM_*` / 池 / may be poisoned; use `COMPAT_MEM_*` / pools |

---

## 3. I/O 与错误处理 / I/O & Error Handling

```c
struct device *dev = device_find("uart0"); /* 名以 DTS 为准 / name per DTS */
if (!dev)
    return VFS_ERR_NODEV;

int ret = device_open(dev, NULL);
if (ret != VFS_OK)
    return ret;

ret = device_write(dev, buf, len, 100 /* ms */);
(void)device_close(dev);
return ret;
```

约定 / Conventions：

- 所有 HAL/VFS/bus 风格 API：`int` 返回值，**禁止**忽略（注意 `COMPAT_WARN_UNUSED_RESULT`）。
  All HAL/VFS/bus-style APIs return `int`; **never ignore** the result (mind `COMPAT_WARN_UNUSED_RESULT`).
- 超时用毫秒；`OSAL_WAIT_FOREVER` 仅在明确可接受阻塞处使用。
  Timeouts in ms; use `OSAL_WAIT_FOREVER` only where blocking is explicitly acceptable.
- ioctl 的 `cmd` 与参数布局由各 `vfs-*.h` 定义；汇总见 [peripherals.md](peripherals.md)。
  Each `vfs-*.h` defines its `cmd` & argument layout; summarized in [peripherals.md](peripherals.md).

---

## 4. 任务与同步 / Tasks & Synchronization

- 创建任务：`osal_task_create` / `osal_task_create_handle`（不要直接 `xTaskCreate`）。
  裸机例外：C API 恒返回 `OSAL_ERR_NOTSUPP`，任务创建走 `osal_null.h` 的 C++ 重载
  `osal_task_create`（`CONFIG_OSAL_NULL_TASK_CPP`，默认开启）或直接 `xscheduler_task_create`。
  Create tasks via `osal_task_create` / `osal_task_create_handle` (never raw `xTaskCreate`).
  Bare-metal exception: the C API always returns `OSAL_ERR_NOTSUPP`; create tasks through the
  C++ overload in `osal_null.h` (`CONFIG_OSAL_NULL_TASK_CPP`, on by default) or `xscheduler_task_create` directly.
- 注意 **优先级数值语义随 OSAL 后端变化**（见 [osal_switching.md](osal_switching.md)）。
  Priority-value semantics **depend on the OSAL backend** (see [osal_switching.md](osal_switching.md)).
- 设备锁由 `device_*` 包装持有；业务勿对同一设备再叠一层易死锁的锁顺序。
  Device locks are held by the `device_*` wrappers; don't stack a deadlock-prone lock order on the same device.
- ISR 里只做短工作；其余投递 EventBus / 下半部 / 任务。
  ISRs do short work only; hand the rest to EventBus / bottom halves / tasks.

---

## 5. 与启动顺序的关系 / Boot Order

| 时机 / Phase | 适合做的事 / Suitable Work |
| :--- | :--- |
| `pre_os_init` 之前 / Before | 仅平台极早期（时钟）/ platform-early only (clocks) |
| `pre_os` 后、`start_tasks` 前 / After pre_os, before start_tasks | 静态服务构造、无依赖 probe 的准备 / static service construction, dependency-free probe prep |
| `start_tasks` 后 / After | 依赖已 probe 设备的业务、`device_find` / business on probed devices, `device_find` |
| `system_init_complete` 后 / After | 允许中断；再启动调度器 / interrupts enabled; then start the scheduler |

---

## 6. 检查清单 / Checklist

- [ ] 业务 `.c` 无 `hal_` / 厂商 / 原生 RTOS 头 / no `hal_`, vendor, or raw RTOS headers in business `.c`
- [ ] 所有 `device_*` 返回值有处理 / every `device_*` return value handled
- [ ] 无在 ISR 打日志或拿 mutex / no logging or mutex-taking in ISRs
- [ ] 切 OSAL 后重测优先级与栈 / re-test priorities & stacks after switching OSAL

---

## 相关文档 / Related Documents

- [fast_path.md](fast_path.md) · [osal_switching.md](osal_switching.md)
- [architecture.md](architecture.md)
