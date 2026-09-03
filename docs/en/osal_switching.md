# OSAL Backend Switching Notes

> Behavioral differences you must re-check when switching between mini-os / FreeRTOS / RT-Thread / bare-metal (NULL).

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
| `CONFIG_OSAL_NULL` | `osal/src/osal_null.c`<br>+ `osal/src/osal_task.cpp` (when `CONFIG_OSAL_NULL_TASK_CPP=y` **and** `!XTASK_NONE`) | `time_slice/task` (`xtask_coop.c` or `xtask_preempt.c`, picked by the `Kconfig.mini_tree` bare-metal scheduler choice `XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`; shares `xtask.h` API) | No scheduler (`XTASK_NONE`, hand-written `while(1)`) <br>**or** cooperative round-robin (default, `XTASK_COOP`)<br>**or** N+1 preemptive multi-priority (`XTASK_PREEMPT`) |
| `CONFIG_OSAL_MINI_OS` | `osal/src/osal_mini_os.c` | `lib/mini-os` (in-tree kernel, Cortex-M only; see [mini-os.md](mini-os.md)) | Preemptive (32-level ready bitmap, O(1)) |
| `CONFIG_OSAL_FREERTOS` | `osal/src/osal_freertos.c` | `lib/freeRTOS` (v11.3.0) | Preemptive |
| `CONFIG_OSAL_RTTHREAD` | `osal/src/osal_rtthread.c` | `lib/rtthread` (v5.3.0) | Preemptive |

The bare-metal backend (`CONFIG_OSAL_NULL`) picks one scheduler from the "bare-metal scheduler" choice in `Kconfig.mini_tree` (`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`); CMake injects `MINI_TREE_XTASK_*` macros to decide whether `xtask_coop.c` or `xtask_preempt.c` is compiled, with `#ifdef` as a second gate:
- **No scheduler** (`XTASK_NONE`) — nothing is compiled in; write your own `while(1)` loop. `OSAL_NULL_TASK_CPP` is auto-disabled by Kconfig, and the osal/system layer cannot link against xtask interfaces — the firmware degrades to a bare closure.
- **Cooperative** (default, `XTASK_COOP`) — `time_slice/task/xtask_coop.c`, round-robin time slices, non-preemptive.
- **Preemptive** (`XTASK_PREEMPT`) — `time_slice/task/xtask_preempt.c`, N+1 linked-list multi-priority (grouped priority + CLZ lookup; delayable / sleepable / preemptible; precise WFI to the earliest deadline when idle); **finished & compilable.**

The public surface is `osal/include/osal.h`. Business code and VFS should depend on this header only.

Current `lib/` state: **mini-os (in-tree), FreeRTOS (v11.3.0), RT-Thread (v5.3.0), and ETL** are vendored; TinyUSB / lwIP are config-time FetchContent, and the rest (littlefs, FatFs, MultiButton, MCUBoot, coreMQTT, LVGL, u8g2, FlashDB, SFUD, EasyFlash, EasyLogger) are link-time FetchContent (`mini_tree_link_*`).

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
| mini-os | **Lower** number = higher priority (same as RT-Thread, opposite of FreeRTOS) |
| NULL (cooperative, `XTASK_COOP`) | C API ignores priority arguments |
| NULL (preemptive, `XTASK_PREEMPT`) | N+1 linked-list multi-priority; higher number = higher priority |

Bare-metal task creation is controlled by `CONFIG_OSAL_NULL_TASK_CPP` (depends on `SYSTEM_CPP && !XTASK_NONE`, on by default):
- **On (unified path)**: the C++ overload `osal_task_create` in `osal_null.h`.
  - Cooperative: `period` is the task period in ms (bare metal has no priority concept — the argument is **reinterpreted** as period).
  - Preemptive: the same overload gains a `priority` parameter (higher = more urgent); `stack_size` is reused as the period on bare metal.
- **Off (raw xtask, or forced under `XTASK_NONE`)**: the wrapper is not compiled; call `xscheduler_task_create` / `x_scheduler_poll` directly.
- The bare-metal C API `osal_task_create` / `osal_task_create_handle` always returns `OSAL_ERR_NOTSUPP`.

> **When `XTASK_PREEMPT=y`**: the C++ overload is still provided but switches to the `priority` branch (the cooperative/preemptive branches are split by `CONFIG_XTASK_PREEMPT` inside `osal_task.cpp` — no caller change needed). You may also use `XTASK_NONE` and write a raw `while` loop.

The same business constants **must** be re-mapped when switching backends, or you get "high-priority starvation" or inverted priorities.

---

## 4. Synchronization and ISR

- Only use APIs in `osal.h` marked ISR-safe (if any); when in doubt, assume mutexes are **not** safe in ISRs.
- The spinlock implementation is selected by `CONFIG_OSAL_SPINLOCK_IRQ_DISABLE` / `ATOMIC`; prefer atomic under AMP.
- Business code must not `#include` `semphr.h` / `rthw.h` directly.
- Switching to the mini-os backend needs the same board-wiring care: `SysTick_Handler` forwards to `mini_os_systick_handler()`, `PendSV_Handler` to `pendsv_handler()` (lowercase, symbol in port.S), and the linker script must include `lib/mini-os/mini-os-heap.ld`.

---

## 5. Startup Differences

| Backend | After `system_init_complete` |
| :--- | :--- |
| NULL | `for(;;) mini_tree_system_loop();` |
| FreeRTOS | `vTaskStartScheduler();` |
| RT-Thread | `rt_system_scheduler_start();` |
| mini-os | `mini_os_schedule_start();` (inside `osal_scheduler_start`, the kernel is lazily booted first: `schedule_init` + idle thread + SysTick, then the scheduler starts) |

Don't link or call RTOS scheduler entry points under a NULL configuration.

---

## 6. Capacity and Memory

- **Bare-metal queue pool (OSAL_NULL only)**: `CONFIG_OSAL_NULL_MAX_QUEUES` is the **base queue count** (default 0, no RAM); enabling `CONFIG_EVENT_BUS` **auto-adds 1** (EventBus needs a queue). Manual `osal_queue_create` → set the base in Kconfig. Per-queue buffer `CONFIG_OSAL_NULL_QUEUE_BUF_SZ` (2048 B).
- `CONFIG_OSAL_MUTEX_POOL_SIZE` must cover `DEV_ID_COUNT` (device locks) plus business locks.
- **RTOS heaps are Kconfig-gated**: FreeRTOS dynamic heap `CONFIG_FREERTOS_HEAP_SIZE` (8 KB), RT-Thread static heap `CONFIG_RTT_HEAP_SIZE` (32 KB); the mini-os heap comes from a linker region (`__mini_os_heap_start`/`__mini_os_heap_end`, no Kconfig heap size, **does not count into bss**, resize via the linker script).
- Task stack size varies with backend stack overhead; re-measure headroom after switching.

---

## 7. Checklist

- [ ] `.config` and the generated `config.h` agree
- [ ] No two OSAL `.c` files compiled at once
- [ ] Under bare-metal, the "bare-metal scheduler" choice matches expectations (`XTASK_NONE` / `XTASK_COOP` / `XTASK_PREEMPT`; `xtask_coop.c` and `xtask_preempt.c` are mutually exclusive)
- [ ] Priority table re-mapped for the backend (NULL preemptive: higher number = higher priority)
- [ ] Bare-metal task-creation path is as intended (`CONFIG_OSAL_NULL_TASK_CPP`: unified C++ overload or raw xtask; forced off under `XTASK_NONE`; preemptive overload has the `priority` branch)
- [ ] Startup path matches the backend
- [ ] Log backend (PRINTF/OSAL) still behaves as expected

---

## Related Docs

- [getting_started.md](getting_started.md) · [service_spec.md](service_spec.md)
- [mini-os.md](mini-os.md) (mini-os kernel deep-dive) · [memory_footprint.md](memory_footprint.md) (four-backend memory benchmark)
- [design_decisions.md](design_decisions.md)
