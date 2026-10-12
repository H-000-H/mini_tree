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
| `status.h` | `MINI_OK` / `MINI_ERR_*` |
| `mini_backend.h` | tasks, locks, queues, delays, log levels |
| `event_bus.h` | pub-sub |
| `algorithm/buffer` | buffers |
| `system_log.h` | `MT_LOG_ERROR/WARN/INFO` |
| own headers | — |

---

## 2. Forbidden Dependencies

| Forbidden | Why |
| :--- | :--- |
| `hal_*.h` (in business code) | breaks layering; go through device/vfs |
| `FreeRTOS.h` / `rtthread.h` (in business code) | **recommended** to use native kernel APIs directly (not forced); `mini_backend.h` is only an optional convenience for in-tree code |
| Vendor register headers | not portable |
| ad-hoc `malloc` / `printf` / `memset` | may be poisoned; use `COMPAT_MEM_*` / pools |

---

## 3. I/O & Error Handling

```c
struct device *dev = device_find("uart0"); /* name per DTS */
/* device_find returns an error pointer (ERR_PTR) on failure, not NULL; check with IS_ERR */
if (IS_ERR(dev))
    return PTR_ERR(dev);

int ret = device_open(dev, NULL);
if (ret != MINI_OK)
    return ret;

ret = device_write(dev, buf, len, 100 /* ms */);
(void)device_close(dev);
return ret;
```

Conventions:

- HAL/bus APIs (`hal_*`/`*_bus_*`) return `int`; **never ignore by default** (`CONFIG_COMPILER_WARN_UNUSED_RESULT=y`) to improve reliability of internal layers — use `MINI_IGNORE_RESULT()` to silence intentionally. Exception: fire-and-forget actions with no failure path by semantics (e.g. status flags inside ISRs, noreturn entries) may stay `void`.
- device/VFS APIs (`device_open/read/write/ioctl`, etc.) return `int`; **ignoring is allowed by default** (`DEVICE_WARN_UNUSED_RESULT` is off by default); enable Kconfig `DEVICE_WARN_UNUSED_RESULT` to enforce strict checking, after which **never ignore** the result.
- Timeouts in ms; use `MINI_WAIT_FOREVER` only where blocking is explicitly acceptable.
- Each `vfs-*.h` defines its `cmd` & argument layout; summarized in [peripherals.md](peripherals.md).

---

## 4. Tasks & Synchronization

- Create tasks: business code is recommended to use native kernel APIs directly (e.g. `xTaskCreate` / `rt_thread_create`), or the optional `mini_task_create` wrapper from `mini_backend.h`; in-tree code uses `mini_backend.h` uniformly.
  Bare-metal exception: the C API always returns `MINI_ERR_NOTSUPP`; create tasks through the
  C++ overload in `mini_backend.h` (`CONFIG_XTASK_PREEMPT`, on by default) or `xscheduler_task_create` directly.
  **Under preemptive (`XTASK_PREEMPT=y`)**: the C++ overload is still provided but switches to the `priority` branch (`stack_size` reused as the period); you may also use `xscheduler_task_create` directly (shared `xtask.h`).
- Priority-value semantics **depend on the OS backend** (see [backend_switching.md](backend_switching.md)).
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

- [ ] no `hal_` or vendor headers in business `.c` (raw RTOS headers are allowed in business code — native APIs recommended)
- [ ] every `device_*` return value handled
- [ ] no logging or mutex-taking in ISRs
- [ ] re-test priorities & stacks after switching the backend (CONFIG_OS_*)

---

## Related Documents

- [fast_path.md](fast_path.md) · [backend_switching.md](backend_switching.md)
- [architecture.md](architecture.md)
- [app_cpp_guide.md](app_cpp_guide.md) (upper-layer C++ restrictions and recommendations)
