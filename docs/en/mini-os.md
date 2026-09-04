# mini-os Kernel (lib/mini-os)

> The in-tree minimal real-time kernel of mini_tree (Cortex-M only, freestanding, no libc dependency), and the smallest RTOS backend among the four OSAL backends. This document covers its scheduler, time wheels, synchronization primitives, memory management and port layer, plus its integration wiring with mini_tree.

| Item | Content |
| :--- | :--- |
| **Readers** | Developers who need to understand/debug kernel behavior, do board wiring, or trim the kernel |
| **Prerequisites** | [architecture.md](architecture.md) (layering), [osal_switching.md](osal_switching.md) (backend switching), [getting_started.md](getting_started.md) (dual-track Kconfig) |
| **Source** | `lib/mini-os/` (`src/` kernel · `inc/` headers · `arch/arm/cortex-m/port/` port layer) |
| **License** | Apache-2.0 (design informed by FreeRTOS / RT-Thread / Zephyr / Linux) |

---

## Table of Contents

1. [Positioning and Feature Overview](#1-positioning-and-feature-overview)
2. [Scheduler](#2-scheduler)
3. [Threads and Synchronization Primitives](#3-threads-and-synchronization-primitives)
4. [Timers](#4-timers)
5. [Memory Management](#5-memory-management)
6. [Port Layer](#6-port-layer)
7. [Configuration System](#7-configuration-system)
8. [Integration with mini_tree](#8-integration-with-mini_tree)
9. [Measured Memory Footprint](#9-measured-memory-footprint)
10. [Standalone Build](#10-standalone-build)

---

## 1. Positioning and Feature Overview

mini-os is the in-tree minimal RTOS kernel. Design goals:

- **Smallest footprint**: lowest text/bss among the four OSAL backends (see [memory_footprint.md](memory_footprint.md) §4);
- **Freestanding**: no libc dependency (only self-contained headers like `stddef.h`), heap implemented in-tree;
- **Cortex-M only**: the port covers M0/M0+/M3/M4/M7 (the RISC-V port is an empty stub; ESP32 is Xtensa and unsupported);
- **GCC/Clang toolchains**: the kernel uses GNU extensions (`__attribute__((constructor))` etc.); Keil ARMCC cannot build it directly.

| Subsystem | Capabilities |
| :--- | :--- |
| Scheduling | 32-level preemption + O(1) ready bitmap + same-priority list rotation (time slicing optional, off by default) |
| Thread | Dynamic/static creation, deletion, suspend/resume, dynamic priority change (with PI roll-back), exit callback, idle hook; detach/join optional |
| Sync | Counting/binary semaphores (mutually convertible), recursive mutex + priority inheritance (chained propagation), 32-bit event group (optional) |
| Communication | Fixed-size message queue (blocking send/receive + ISR variants) |
| Timer | HARD (runs in tick/ISR context) + SOFT (runs in a dedicated service thread); independent timer time wheel |
| Memory | first-fit heap (split + adjacent coalescing + magic double-free detection) + optional slab; heap comes from the linker script |
| Critical sections | PRIMASK full mask, or BASEPRI threshold (`MINI_OS_IRQ_MAX_SYSCALL_PRIORITY`) |
| Engineering | No libc, C11 `_Generic` atomic fallback (both GCC and Clang builtin paths) |

---

## 2. Scheduler

Core implementation in `lib/mini-os/src/schedule.c` (~470 lines).

### 2.1 Ready bitmap + O(1) level selection

- 32 priority levels, **smaller number = higher priority** (same semantics as RT-Thread, opposite of FreeRTOS);
- The ready bitmap `g_priority` is a single 32-bit word; `mini_os_get_highest_priority()` uses CTZ (count trailing zeros) to pick the highest level in one instruction, O(1);
- One doubly-linked list per level, `g_ready_running_list[MINI_OS_PRIORITY]`; equal-priority threads rotate within the list (sentinel self-pointing convention: wrap-around successors skip the sentinel).

### 2.2 Thread time wheel

Thread delays / sync timeouts use a layered time wheel (`s_wheel[MINI_OS_TICK_WHEEL]`, slot count a power of two, default 32):

- Insert: `slot = (current + ticks) & MASK`, `round = (ticks - 1) >> CTZ(WHEEL)` (rounds counted first when spanning multiple laps);
- `mini_os_sync_wait_park()` uses **two nodes** to park in two places at once: `wait_node` onto the sync object's wait list, `list_node` onto the time wheel; the wake side does a double unlink to avoid double wake-up;
- SysTick handler order: IRQs off → thread wheel expiry wake-ups → time slicing (if `MINI_OS_TIME_SLICE`) → tick increment → timer wheel.

### 2.3 Exception priority arrangement

| Exception | Priority | Role |
| :--- | :--- | :--- |
| PendSV | `0xFF` (lowest) | Context switch (`port.S`, PSP + privileged mode) |
| SysTick | `0xFE` | Tick driving (thread wheel / time slice / timer wheel) |

- ISR-side wake-ups **never switch context by themselves**: `mini_os_schedule_yield_isr()` inspects the ready bitmap and pends PendSV only when a higher-priority thread is ready;
- All `*_isr` API variants (`mini_os_semaphore_post_isr` etc.) follow this convention; OSAL's `osal_yield_from_isr()` forwards to it.

### 2.4 Key threads and constructor priorities

| Entity | Priority | Notes |
| :--- | :--- | :--- |
| idle thread | `MINI_OS_PRIORITY - 1` | Fixed lowest level; also patrols the MSP stack sentinel |
| SOFT timer service thread | `MINI_OS_PRIORITY - 2` | One step above idle (never shares the lowest level), spawned lazily |
| FPU-enable constructor | 100 | `0-100` reserved for system constructors, runs first |
| CPUID probe constructor | 101 | See §6 |
| Stack sentinel constructor | 102 | See §6 |
| Idle thread self-init | 105 | |
| By-name registries (thread/semaphore/mutex) | 110-112 | Only with `MINI_OS_FIND_BY_NAME` |
| Timer module self-init | 113 | |

---

## 3. Threads and Synchronization Primitives

### 3.1 Semaphore (semaphore.c)

- Counting / binary semaphores, mutually convertible; `mini_os_semaphore_post_isr()` is the ISR-safe variant.

### 3.2 Mutex (mutex.c) — priority inheritance (PI)

mini-os PI is a **per-thread tracking** model:

- The owner saves the priority it had at lock time in `base_priority`, and every mutex it holds is chained into the thread's `hold_list` (via `mutex->hold_node`);
- Effective priority = `min(base_priority, highest waiter priority among all held mutexes)`;
- **Chained propagation**: when A waits on B's lock and B on C's, the boost propagates up the `wait_mutex` back-pointers, with the depth bounded by `MINI_OS_MUTEX_PI_CHAIN_MAX` (cycle protection);
- Dynamic priority change (`mini_os_thread_set_priority`) performs PI roll-back so it never conflicts with inherited state.

Other points:

- The mutex embeds a binary semaphore (`count = max = 1`) and supports recursive locking (`is_recuring` + depth, optional);
- `mini_os_mutex_delete()` goes through `kill_waiters`: waiters get `wait_done = FALSE` (the upper layer sees a TIMEOUT-like result) and their `wait_mutex` back-pointers are cleared, preventing use-after-free.

### 3.3 Event group (event.c, optional)

- 32-bit flags, OR / WHOLE (all-set) wait semantics, configurable auto-clear;
- Switch: `MINI_OS_EVENT` (off by default on its own, but `OSAL_EVENT` (default y) selects it — see §7);
- When off, `event.h`/`event.c` compile to nothing, and `event.c` is dropped from the source list entirely (not even an empty object file).

---

## 4. Timers

Implementation in `lib/mini-os/src/timer.c` (~450 lines). The core design is an **independent timer time wheel** (`s_timer_wheel`, never touches a TCB), decoupled from the thread wheel of §2.2:

| Type | Callback context | Notes |
| :--- | :--- | :--- |
| HARD timer | Runs directly in tick/ISR context | Callbacks must be very short (respect the [fast_path.md](fast_path.md) red lines) |
| SOFT timer | Runs in the dedicated service thread | Callbacks are queued on `s_soft_pending` |

Service-thread details:

- Static TCB + static stack (`MINI_OS_TIMER_THREAD_STACK_SIZE`, default 512 B), **spawned lazily** — created on the first SOFT timer start (the scheduler is not up during constructors, so a thread cannot be created there);
- Priority `MINI_OS_PRIORITY - 2`: never preempts application work and never shares the lowest level with idle.

Special wheel-insertion rule: when the target slot equals the currently serviced slot, insert at the **head** with an adjusted round, preventing a double trigger within the same tick; the callback is re-armed (periodic) or dis-armed (one-shot) **before** it runs, because the callback may stop/delete itself and the timer structure must not be touched after return.

---

## 5. Memory Management

Implementation in `lib/mini-os/src/memory.c` (~840 lines) + `inc/mem_heap.h`:

- **Allocation model**: `malloc/free` style — first-fit search + split (only when the remainder ≥ header + 16 bytes) + **adjacent free-block coalescing** (free scans the whole list, O(n), but yields lower fragmentation);
- **Double-free detection**: dual-value header magic — `0xA5A5A5A5` (ALLOC state) / `0x5A5A5A5A` (FREE state); double free / overrun corruption is immediately detectable;
- **O(1) free-size query**: `free_size` is maintained incrementally;
- **Heap source**: the linker script `lib/mini-os/mini-os-heap.ld` provides `__mini_os_heap_start` / `__mini_os_heap_end`; the heap sits between bss and the MSP stack and **does not count into bss** (important for the bss accounting in [memory_footprint.md](memory_footprint.md) §4);
- **Optional slab**: fixed-size classes 16/32/64/128/256 (plus 512 with `MINI_OS_SLAB_LONG`), page size 2 KiB (power of two ≤ 64 KiB); pages are carved from the heap by `1/MINI_OS_SLAB_PROPORTION` (default 1/4), or served from an independent static region via `MINI_OS_SLAB_STATIC`; requests above the largest class fall through to the free list.

> Unlike the other mini_tree backends: `osal_calloc/osal_free` (mini-os backend) use the mini-os own heap instead of libc, so the `s_rtt_heap`/`ucHeap`-style large bss arrays seen with RT-Thread/FreeRTOS do not exist here.
>
> **The memory module is reusable standalone (bare-metal)**: `memory.c` has no scheduler/port dependency and can be compiled into a bare-metal firmware as a single file — with `CONFIG_OSAL_NULL_MINI_OS_MEM` (default off) the bare-metal backend's `osal_malloc/osal_calloc/osal_free` switch from the libc heap to the mini-os heap; the heap zone is taken over lazily on the first allocation (`mini_os_heap_ensure_init()`, idempotent), no `.init_array` traversal needed. The board linker script must provide `__mini_os_heap_start/__mini_os_heap_end` (`INCLUDE mini-os-heap.ld`). The free list is unlocked (same as libc malloc) — never call from an ISR.

---

## 6. Port Layer

`lib/mini-os/arch/arm/cortex-m/port/` (`port.c` ~160 lines C + `port.S` ~510 lines assembly; `arch/risc-v/port` is an empty stub).

### 6.1 SVC callback mechanism

- The two globals `g_svc_cb` / `g_svc_arg` are read **directly by port.S** (hence not static); `mini_os_svc_set_callback()` installs them;
- `mini_os_svc_get_num()` recovers the SVC immediate from the stacked frame: a Thumb SVC is 2 bytes wide, the stacked PC points after it, `[pc-2] = 0xDF` opcode, `[pc-1] = imm8`.

### 6.2 CPUID probe (fail-fast)

The port assembly is core-specific; a wrong core corrupts contexts directly. A startup constructor (priority 101) reads `SCB->CPUID` (0xE000ED00) bits[15:4] (part number) and compares it against `MINI_OS_ARCH`:

| part number | Core |
| :--- | :--- |
| `0xC20` / `0xC60` | M0 / M0+ (ARMv6-M twins, accepted as a group) |
| `0xC23` | M3 |
| `0xC24` | M4 |
| `0xC27` | M7 |

On mismatch → halt in a loop (better to fail fast than to debug a corrupted PendSV).

### 6.3 FPU and stack sentinel

- **FPU enable** (M4F/M7 with `MINI_OS_USE_FPU`, on by default): constructor (priority 100) grants CPACR CP10/CP11 full access + `dsb/isb`, and must run before any thread; PendSV does a lazy s16-s31 save;
- **MSP stack-overflow sentinel** (`MINI_OS_STACK_OVERFLOW_CHECK`, off by default): a constructor (priority 102) plants the magic `0x060815` at `__mini_os_heap_end` (the heap/MSP boundary, the first casualty of an overflow); the idle thread re-checks it every loop and halts on mismatch.

### 6.4 Critical sections

Two compile-time alternatives: PRIMASK full mask, or a BASEPRI threshold that only masks IRQs not higher than `MINI_OS_IRQ_MAX_SYSCALL_PRIORITY`. The OSAL pool critical sections use the nestable `mini_os_irq_save/restore`.

---

## 7. Configuration System

Every option resolves through the same **three-tier chain** (reference implementation in `inc/mini_config.h`):

1. `CONFIG_<NAME>` — generated by mini_tree's Kconfig system (`config.h`);
2. `MINI_OS_<NAME>` — predefined by an external build system (command-line `-D` or a parent project);
3. Built-in default.

> **Note**: feature switches are always defined as `1`/`0`, so test with `#if`, never `#ifdef` (`#ifdef` is true for a disabled option too).

### 7.1 Kconfig surface (`Kconfig.mini_tree`, all `depends on OSAL_MINI_OS`)

| Option | Type / default | Notes |
| :--- | :--- | :--- |
| `MINI_OS_PRIORITY` | int / 32 | Number of priorities (smaller = higher), must be a multiple of 8; upper bound 32 (the bitmap is a single 32-bit word; ≥32 is UB) |
| `MINI_OS_DEFAULT_SYSTICK` | int / 1000 | Tick rate in Hz (default 1 tick = 1 ms) |
| `MINI_OS_CPU_CLOCK_HZ` | int / 72000000 | CPU clock, used to derive the SysTick reload |
| `MINI_OS_TICK_WHEEL` | int / 32 | Thread time-wheel slot count (power of two) |
| `MINI_OS_THREAD_MIN_STACK_SIZE` | int / 256 | Minimum thread stack (bytes) |
| `MINI_OS_DEFAULT_IDLE_STACK_SIZE` | int / 256 | Idle thread stack |
| `MINI_OS_TIMER_THREAD_STACK_SIZE` | int / 512 | SOFT-timer service thread stack (≥ min stack, multiple of 8) |
| `MINI_OS_TIME_SLICE` | bool / n | Round-robin time slicing (default: strict priority) |
| `MINI_OS_EVENT` | bool / n | 32-bit event group (`OSAL_EVENT` selects it by default) |
| `MINI_OS_THREAD_DETACH` | bool / n | detach/join (one switch, adds reclamation fields to every TCB) |
| `MINI_OS_FIND_BY_NAME` | bool / n | By-name registries for threads/semaphores/mutexes |
| `MINI_OS_LONG_TIME` | bool / n | 64-bit tick (via an extra wrap-around counter) |
| `MINI_OS_STACK_OVERFLOW_CHECK` | bool / n | MSP stack sentinel (requires mini-os-heap.ld) |
| `MINI_OS_USE_FPU` | bool / y | FPU context save (only visible on CM4F/CM7; do not turn off with hard-float) |
| `MINI_OS_SPINLOCK`(+`_ATOMIC`/`_YIELD`/`_NUM`) | bool / y | Header-only spinlock (off → OSAL falls back to IRQ masking); atomic mode is SMP-only |
| `ARCH` | (no prompt) | mini-os architecture id (0=M0/M0+ 1=M3 2=M4 3=M7), derived automatically from `PLATFORM_ARM_*`, **must not be set by hand** |

---

## 8. Integration with mini_tree

### 8.1 Selecting the backend

`CONFIG_OSAL_MINI_OS=y` (`make menuconfig`, or edit `.config` by hand and re-configure). Constraints:

- `depends on !PLATFORM_RISCV && !PLATFORM_ESP32` — Cortex-M only;
- `select USB_TUSB_OS_NONE` — TinyUSB does not run on mini-os (no mini-os backend for the USB stack yet);
- `OSAL_EVENT` (default y) automatically selects `MINI_OS_EVENT`.

### 8.2 Board wiring (mandatory)

| Wiring point | Requirement |
| :--- | :--- |
| `SysTick_Handler` | Forward to `mini_os_systick_handler()` |
| `PendSV_Handler` | Forward to `pendsv_handler()` (**lowercase**, symbol lives in port.S) |
| Linker script | `#include` `lib/mini-os/mini-os-heap.ld` providing `__mini_os_heap_start` / `__mini_os_heap_end` |
| Startup flow | Must iterate `.init_array` (satisfied by default on GCC/Clang) — the kernel self-initializes via constructors (heap/registries/idle/sentinel) |

### 8.3 OSAL mapping notes (`osal/src/osal_mini_os.c`)

| Topic | Semantics |
| :--- | :--- |
| Priorities | mini-os: smaller number = higher priority (same as RT-Thread, **opposite of FreeRTOS**); by convention each OSAL backend keeps its native kernel semantics |
| Error codes | `MINI_OS_ERR_*` matches `OSAL_ERR_*` numerically when `config.h`/`status.h` are visible — zero-overhead pass-through; only `MINI_OS_ERR_AGAIN` maps to `OSAL_ERR_TIMEOUT` |
| ISR mode | `*_isr` calls never switch context; `osal_yield_from_isr()` forwards to `mini_os_schedule_yield_isr()` |
| Object pool | Mutexes/semaphores embed kernel objects statically + an `osal_pool` slot pool; pool critical sections use `mini_os_irq_save/restore` |
| Scheduler start | `osal_scheduler_start()` first lazily boots the kernel (`schedule_init` + idle thread + SysTick), then starts the scheduler |
| Scheduler freeze | mini-os has no global suspend-all API; `osal_sched_freeze()` degrades to IRQ masking (same one-way freeze semantics as `osal_null`) |

### 8.4 Build integration

The root build `add_subdirectory(lib/mini-os)` in `lib/CMakeLists.txt` when `OSAL_BACKEND=MINI_OS`; mini-os' own CMakeLists declares `project(... C ASM)` (the only kernel library in-tree that does not rely on the root project enabling ASM; by contrast, rtthread used to silently drop `context_gcc.S` for lack of `enable_language(ASM)` — fixed). The event-group source file is compiled in conditionally based on `CONFIG_MINI_OS_EVENT`/`CONFIG_OSAL_EVENT` from `.config`; when off, not even an object file is produced.

---

## 9. Measured Memory Footprint

Full methodology and the four-backend comparison: [memory_footprint.md](memory_footprint.md) §4 (arm-none-eabi-gcc 13.3.1, minimal firmware + `--gc-sections`). mini-os rows (text/data/bss, bytes):

| Configuration | newlib-nano | full newlib |
| :--- | :--- | :--- |
| mini-os C | 14245 / 120 / 2620 | 38776 / 1772 / 2664 |
| mini-os C++ | 14381 / 128 / 3016 | 38912 / 1780 / 3064 |

- **Smallest text** of the four backends (~3 KB less than FreeRTOS, ~3.6 KB less than RT-Thread, nano figures);
- **Smallest bss, and it does not grow with heap config**: the heap lives in a linker region and does not count into bss (FreeRTOS `ucHeap` 8 KiB / RT-Thread `s_rtt_heap` 32 KiB both land in bss); even excluding configurable heaps, the framework bss is 3016 B (C++) — clearly below the others;
- The main extra cost is `SYSTEM_CPP` (+136 text / +396 bss, nano figures).

---

## 10. Standalone Build

mini-os can be built standalone (own `project()`, default `-mcpu=cortex-m3`, overridable via `MINI_OS_MCU`):

```sh
cd lib/mini-os
cmake --preset Debug
cmake --build --preset Debug
```

The output is the static library `libmini-os.a`. Standalone builds have no `.config`; the event group falls back to the built-in defaults in `mini_config.h`. Note the kernel uses GNU extensions — GCC/Clang toolchains only.

---

## Related Documents

- [memory_footprint.md](memory_footprint.md) — four-backend memory benchmark (§4)
- [osal_switching.md](osal_switching.md) — OSAL backend switching and semantic differences
- [fast_path.md](fast_path.md) — HARD-timer / ISR callback red lines
- [patterns.md](patterns.md) — mini_tree key mechanisms (xtask/time-slice bare-metal counterparts)
- `lib/mini-os/README.md` — official kernel feature list and three-tier configuration notes
