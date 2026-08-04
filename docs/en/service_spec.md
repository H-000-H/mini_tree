# Service Specification

> What business code may or may not depend on; how to hook into the two-phase boot.

| Item | Content |
| :--- | :--- |
| **Audience** | App & service authors |
| **Prereq** | [getting_started.md](getting_started.md) § boot |
| **Related** | [fast_path.md](fast_path.md) · [peripherals.md](peripherals.md) · [api_compatibility.md](api_compatibility.md) |

---

## Contents

1. [Allowed Dependencies](#1-allowed-dependencies)
2. [Forbidden Dependencies](#2-forbidden-dependencies)
3. [I/O & Error Handling](#3-io-error-handling)
4. [Tasks & Synchronization](#4-tasks-synchronization)
5. [Boot Order](#5-boot-order)
6. [Checklist](#6-checklist)

---

## 1. Allowed Dependencies

| Header & Module | Use |
| :--- | :--- |
| `device.h` | device lookup & I/O |
| `status.h` | `VFS_OK` / `VFS_ERR_*` |
| `osal.h` | tasks, locks, queues, delays, log levels |
| `event_bus.h` / `event_bus.hpp` | pub-sub |
| `buffer_pool.h` · `algorithm/buffer` | buffers |
| `system_log.h` | `SYS_LOGI/W/E` |
| own headers | — |

---

## 2. Forbidden Dependencies

| Forbidden | Why |
| :--- | :--- |
| `hal_*.h` (in business code) | breaks layering; go through device/vfs |
| `FreeRTOS.h` / `rtthread.h` (in business code) | use OSAL for backend portability |
| Vendor register headers | not portable |
| ad-hoc `malloc` / `printf` / `memset` | may be poisoned; use `COMPAT_MEM_*` / pools |

---

## 3. I/O & Error Handling

```c
struct device *dev = device_find("uart0"); /* name per DTS */
if (!dev)
    return VFS_ERR_NODEV;

int ret = device_open(dev, NULL);
if (ret != VFS_OK)
    return ret;

ret = device_write(dev, buf, len, 100 /* ms */);
(void)device_close(dev);
return ret;
```

Conventions:

- All HAL/VFS/bus-style APIs return `int`; **never ignore** the result (mind `COMPAT_WARN_UNUSED_RESULT`).
- Timeouts in ms; use `OSAL_WAIT_FOREVER` only where blocking is explicitly acceptable.
- Each `vfs-*.h` defines its `cmd` & argument layout; summarized in [peripherals.md](peripherals.md).

---

## 4. Tasks & Synchronization

- Create tasks via `osal_task_create` / `osal_task_create_handle` (never raw `xTaskCreate`).
  Bare-metal exception: the C API always returns `OSAL_ERR_NOTSUPP`; create tasks through the
  C++ overload in `osal_null.h` (`CONFIG_OSAL_NULL_TASK_CPP`, on by default) or `xscheduler_task_create` directly.
  **Under preemptive (`CONFIG_XTASK_PREEMPT=y`)**: the cooperative C++ overload is closed; use `xscheduler_task_create` directly (shared `xtask.h`).
- Priority-value semantics **depend on the OSAL backend** (see [osal_switching.md](osal_switching.md)).
- Device locks are held by the `device_*` wrappers; don't stack a deadlock-prone lock order on the same device.
- ISRs do short work only; hand the rest to EventBus / bottom halves / tasks.

---

## 5. Boot Order

| Phase | Suitable Work |
| :--- | :--- |
| Before `pre_os_init` | platform-early only (clocks) |
| After pre_os, before start_tasks | static service construction, dependency-free probe prep |
| After `start_tasks` | business on probed devices, `device_find` |
| After `system_init_complete` | interrupts enabled; then start the scheduler |

---

## 6. Checklist

- [ ] no `hal_`, vendor, or raw RTOS headers in business `.c`
- [ ] every `device_*` return value handled
- [ ] no logging or mutex-taking in ISRs
- [ ] re-test priorities & stacks after switching OSAL

---

## Related Documents

- [fast_path.md](fast_path.md) · [osal_switching.md](osal_switching.md)
- [architecture.md](architecture.md)
