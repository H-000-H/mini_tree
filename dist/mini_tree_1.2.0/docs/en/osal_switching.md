# OSAL Backend Switching Notes

> Behavioral differences you must re-check when switching between FreeRTOS / RT-Thread / bare-metal (NULL).

| Item | Description |
| :--- | :--- |
| **Audience** | People editing `.config` or maintaining multiple backends |
| **Prereq.** | [getting_started.md](getting_started.md) |
| **Related** | [faq.md](faq.md) · [architecture.md](architecture.md) |

---

## Table of Contents

1. [Backend Comparison](#1-backend-comparison)
2. [Switching Steps](#2-switching-steps)
3. [Priority and Scheduling](#3-priority-and-scheduling)
4. [Synchronization and ISR](#4-synchronization-and-isr)
5. [Startup Differences](#5-startup-differences)
6. [Capacity and Memory](#6-capacity-and-memory)
7. [Checklist](#7-checklist)

---

## 1. Backend Comparison

| Macro | Implementation | Link deps | Task model |
| :--- | :--- | :--- | :--- |
| `CONFIG_OSAL_NULL` | `osal/src/osal_null.c`<br>+ `osal/src/osal_task.cpp` (when `CONFIG_OSAL_NULL_TASK_CPP=y` **and** `CONFIG_XTASK_PREEMPT=n`) | `time_slice/task` (`xtask_coop.c` or `xtask_preempt.c`, picked by `CONFIG_XTASK_PREEMPT`; shares `xtask.h` API) | Cooperative time slices (bare-metal, default)<br>**or** N+1 preemptive (experimental, `CONFIG_XTASK_PREEMPT=y`) |
| `CONFIG_OSAL_FREERTOS` | `osal/src/osal_freertos.c` | `lib/freeRTOS` (v11.3.0) | Preemptive |
| `CONFIG_OSAL_RTTHREAD` | `osal/src/osal_rtthread.c` | `lib/rtthread` (v5.3.0) | Preemptive |

The bare-metal backend (`CONFIG_OSAL_NULL`) ships two interchangeable schedulers under `time_slice/task/`, gated by `CONFIG_XTASK_PREEMPT` (mutual-exclusive at both CMake and `#ifdef` level, sharing the same `xtask.h` API — caller code unchanged):
- **Cooperative** (default, `CONFIG_XTASK_PREEMPT=n`) — `xtask_coop.c`, round-robin, non-preemptive.
- **Preemptive** (experimental, `CONFIG_XTASK_PREEMPT=y`) — `xtask_preempt.c`, N+1 linked-list multi-priority; **not finished yet — may fail to compile.**

The public surface is `osal/include/osal.h`. Business code and VFS should depend on this header only.

Current `lib/` state: only **FreeRTOS (v11.3.0), RT-Thread (v5.3.0), and ETL** are vendored; TinyUSB / lwIP / cJSON are config-time FetchContent, and the rest (LVGL, littlefs, FatFs, Mbed TLS, coreMQTT, coreHTTP, nanopb, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB, u8g2, SFUD, miniz) are link-time FetchContent (`mini_tree_link_*`).

---

## 2. Switching Steps

1. Change the OSAL choice in `mini_tree/.config` (mutually exclusive).
2. Re-run `genconfig.py` / re-run CMake.
3. **Full rebuild** (don't mix in a stale `config.h`).
4. Re-check priorities, startup, stacks, and ISRs per the sections below.
5. Exercise the critical peripherals and safety paths.

---

## 3. Priority and Scheduling

| Backend | Numeric semantics |
| :--- | :--- |
| FreeRTOS | **Higher** number = higher priority |
| RT-Thread | **Lower** number = higher priority |
| NULL (cooperative) | C API ignores priority arguments |
| NULL (preemptive, `CONFIG_XTASK_PREEMPT=y`) | N+1 linked-list multi-priority (not finished yet) |

Bare-metal task creation is controlled by `CONFIG_OSAL_NULL_TASK_CPP` (depends on `SYSTEM_CPP`, on by default):
- **On (unified path)**: the C++ overload `osal_task_create` in `osal_null.h`; its `period` parameter is the task period in ms (bare-metal has no priority concept — the argument is **reinterpreted** as period).
- **Off (raw xtask)**: the wrapper is not compiled; call `xscheduler_task_create` / `x_scheduler_poll` directly.
- The bare-metal C API `osal_task_create` / `osal_task_create_handle` always returns `OSAL_ERR_NOTSUPP`.

> **When `CONFIG_XTASK_PREEMPT=y`**: the cooperative overload (`osal_task.cpp` + the C++ declaration in `osal_null.h`) is closed entirely via `#ifndef CONFIG_XTASK_PREEMPT`, because preemptive scheduling introduces priority and the `period` argument's semantics change. A preemptive-specific overload is not yet provided; C projects must call `xscheduler_task_create` and other native APIs directly (shared via `xtask.h`).

The same business constants **must** be re-mapped when switching backends, or you get "high-priority starvation" or inverted priorities.

---

## 4. Synchronization and ISR

- Only use APIs in `osal.h` marked ISR-safe (if any); when in doubt, assume mutexes are **not** safe in ISRs.
- The spinlock implementation is selected by `CONFIG_OSAL_SPINLOCK_IRQ_DISABLE` / `ATOMIC`; prefer atomic under AMP.
- Business code must not `#include` `semphr.h` / `rthw.h` directly.

---

## 5. Startup Differences

| Backend | After `system_init_complete` |
| :--- | :--- |
| NULL | `for(;;) mini_tree_system_loop();` |
| FreeRTOS | `vTaskStartScheduler();` |
| RT-Thread | `rt_system_scheduler_start();` |

Don't link or call RTOS scheduler entry points under a NULL configuration.

---

## 6. Capacity and Memory

- **Bare-metal queue pool (OSAL_NULL only)**: `CONFIG_OSAL_NULL_MAX_QUEUES` is the **base queue count** (default 0, no RAM); enabling `CONFIG_EVENT_BUS` **auto-adds 1** (EventBus needs a queue). Manual `osal_queue_create` → set the base in Kconfig. Per-queue buffer `CONFIG_OSAL_NULL_QUEUE_BUF_SZ` (2048 B).
- `CONFIG_OSAL_MUTEX_POOL_SIZE` must cover `DEV_ID_COUNT` (device locks) plus business locks.
- **RTOS heaps are Kconfig-gated**: FreeRTOS dynamic heap `CONFIG_FREERTOS_HEAP_SIZE` (8 KB), RT-Thread static heap `CONFIG_RTT_HEAP_SIZE` (32 KB).
- Task stack size varies with backend stack overhead; re-measure headroom after switching.

---

## 7. Checklist

- [ ] `.config` and the generated `config.h` agree
- [ ] No two OSAL `.c` files compiled at once
- [ ] Under bare-metal, `CONFIG_XTASK_PREEMPT` matches expectations (cooperative vs preemptive are mutually exclusive; `xtask_coop.c` and `xtask_preempt.c` never compile together)
- [ ] Priority table re-mapped for the backend
- [ ] Bare-metal task-creation path is as intended (`CONFIG_OSAL_NULL_TASK_CPP`: unified C++ overload or raw xtask; note the C++ overload is closed when `CONFIG_XTASK_PREEMPT=y`)
- [ ] Startup path matches the backend
- [ ] Log backend (PRINTF/OSAL) still behaves as expected

---

## Related Docs

- [getting_started.md](getting_started.md) · [service_spec.md](service_spec.md)
- [design_decisions.md](design_decisions.md)
