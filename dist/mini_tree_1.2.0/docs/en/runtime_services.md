# Runtime Services

> Horizontal capabilities used after boot: event bus, VIRQ, system-language backends, buffer pools, plus optional watchdog / CRC scrubber / safe-state modules. Layer overview: [architecture.md](architecture.md).

| Item | Content |
| :--- | :--- |
| **Audience** | Business-task & bottom-half writers |
| **Prereq** | [getting_started.md](getting_started.md) boot · [service_spec.md](service_spec.md) |
| **Related** | [fast_path.md](fast_path.md) · [amp.md](amp.md) · [ecosystem.md](ecosystem.md) |

---

## Contents

1. [EventBus](#1-eventbus)
2. [VIRQ & Top/Bottom Halves](#2-virq-topbottom-halves)
3. [SYSTEM_C vs SYSTEM_CPP](#3-system_c-vs-system_cpp)
4. [BufferPool & algorithm/buffer](#4-bufferpool-algorithmbuffer)
5. [Optional Safety Modules (Bricks)](#5-optional-safety-modules-bricks)

---

## 1. EventBus

> **Optional module (off by default)**: `CONFIG_EVENT_BUS` (depends on `SYSTEM`). When on, `core/src/event_bus.c` is compiled and the `event_bus_*` APIs work; when off (default), it is not compiled and no `EVENT_SYS_*` events are posted.

Header: `core/include/event_bus.h` (plus the `event_bus.hpp` C++ wrapper).

### 1.1 Event IDs

| Range | Macro | Notes |
| :--- | :--- | :--- |
| Framework | `EVENT_SYS_BOOT` / `READY` / `FAULT` / `DEVICE_REMOVED` | Framework semantics; avoid in business code |
| User | `EVENT_USER_BASE` (`0x1000`) onwards | Business custom: `EVENT_USER_BASE + n` |

The framework only carries **ID + `uintptr_t arg`**; it never interprets business meaning.

### 1.2 API Highlights

| API | Notes |
| :--- | :--- |
| `event_bus_init` | Called early at cold boot (already on the `mini_tree_pre_os_init` path) |
| `event_bus_subscribe(id_min, id_max, cb, user)` | Range subscription |
| `event_bus_post` / `post_from_isr` | Task / ISR posting |
| `event_bus_start` / `stop` | Run control |
| `event_bus_seal` | **No further `subscribe` after seal** (typically after boot completes) |
| `event_bus_dropped_count` | Dropped-event counter when the queue is full |

Recommendations:

1. Subscribe after `pre_os_init` and before `seal` (or within a documented platform window).
2. ISRs must use only `post_from_isr`; keep callbacks light.
3. If `arg` is a pointer, it must outlive the callback (static/pooled; never a stack pointer).

Capacity: `CONFIG_EVENT_BUS_*` (see Kconfig Runtime); master switch `CONFIG_EVENT_BUS`.

---

## 2. VIRQ & Top/Bottom Halves

Header: `interrupt/interrupt.h`.

```text
HW IRQ
  → platform ISR (keep short)
  → interrupt_virtual_dispatch(virq) / top_half
  → auto-submit bottom half
  → interrupt_bottom_half_poll() or a bottom-half task
  → bottom-half callback (device_ioctl / EventBus / protocol)
```

| Concept | Notes |
| :--- | :--- |
| Virtual blocks | `system` / `tim` / `gpio` / `adc` / `uart` / `spi` / `i2c` / `i2s` / `user` etc. |
| Block size | `VIRTUAL_IRQ_BLOCK_SIZE` (power of two) |
| Bare-metal | Main loop polls `interrupt_bottom_half_poll` (usually via `mini_tree_system_loop`) |
| RTOS | Bottom-half task + semaphore wake (compile-time) |

ISR forbidden: `printf`, long-held locks, unbounded work — [fast_path.md](fast_path.md).

### 2.1 Disabling VIRQ

Master switch `CONFIG_VIRQ` (on by default); when off, `interrupt/interrupt.c` is not compiled and the system loop stops polling the bottom half.

**Confirm these are unneeded before disabling (trim board/driver code accordingly):**

| Feature | Effect When Off |
| :--- | :--- |
| Bare-metal time-slice scheduler (`time_slice/xtask`) | **SysTick default path is unaffected** (core exception, not via VIRQ); only when DTS explicitly sets `chosen { scheduler-tim = &timN; }` to use a generic TIM does disabling VIRQ break TIM IRQ routing and leave the scheduler with no tick source |
| ADC / I2S DMA bottom-half | `vfs-adc` / `i2s_bus` skip VIRQ registration & IRQ enable — async/DMA callbacks dead (polling drivers unaffected) |
| GPIO HW-interrupt routing | the virq_idx slot goes inert |
| Board/driver ISR | must not call `interrupt_virtual_dispatch()` / `interrupt_virtual_register()` or the link fails |

**Memory saving ≈ 1.3 KB RAM + 1 KB Flash** (three VIRQ tables 864 B + bottom-half poller 320 B + work item; FIFO depth `CONFIG_BOTTOM_HALF_QUEUE_DEPTH` costs 4 B/slot). See [memory_footprint.md](memory_footprint.md) §3.6.

---

## 3. SYSTEM_C vs SYSTEM_CPP

> **Optional module (default on)**: master switch `CONFIG_SYSTEM`. When off, neither `system_c/` nor `system_cpp/` is compiled (`CONFIG_SYSTEM_WDT` / `CONFIG_SYSTEM_SCRUBBER` / `CONFIG_EVENT_BUS` also depend on this switch).

When `CONFIG_SYSTEM` is on, Kconfig picks **one**: compile `system_c/` or `system_cpp/`.

| | `SYSTEM_C` | `SYSTEM_CPP` |
| :--- | :--- | :--- |
| Header | `system_c/include/system_init.h` | `system_cpp/include/system_init.hpp` |
| Phase 1 | `mini_tree_pre_os_init()` | `mini_tree::system_pre_os_init()` |
| Phase 2 | `mini_tree_start_tasks()` | `mini_tree::system_start_tasks()` |
| Finalize | `system_init_complete()` (shared C) | same |
| Bare loop | `mini_tree_system_loop()` | same (C API) |
| Deps | fewer | **ETL linked by default** (heap-free C++ base); root CMake often adds `-fno-rtti` / `-fno-exceptions` |

**How to choose:**

- Firmware is mostly C, no toolchain exception → `SYSTEM_C`.
- Existing C++ business / need `event_bus.hpp` or ETL → `SYSTEM_CPP` (the repo's default `.config` usually does).
- Southbound HAL/VFS stays **C ABI**; switching SYSTEM backends never changes the peripheral-stack language.

---

## 4. BufferPool & algorithm/buffer

| Component | Path | Use |
| :--- | :--- | :--- |
| BufferPool | `core/include/buffer_pool.h` | Fixed-size block pool; driver/protocol borrow-return |
| Ring & double buffer | `algorithm/buffer/` | `fifo_spsc`, `double_buffer_spsc` etc. |

Business code may use them directly; avoid complex allocation in ISRs (pool ISR-safety is documented per-header).

---

## 5. Optional Safety Modules (Bricks)

> These modules are **optional bricks**: linking them into the library is recommended (they compile into `mini_tree` by default), but **not enabling them does not block core development** — just turn off the Kconfig switches.

| Module | Function | Kconfig | Notes |
| :--- | :--- | :--- | :--- |
| Watchdog | `system_wdt`: IWDG / WWDG / TWDT | `CONFIG_SYSTEM_WDT` | framework boot-integrated watchdog (IWDG/TWDT + auto-feed + bootloop guard); app-programmable ones go through `vfs-iwdg`/`vfs-wwdg` (DTS) |
| CRC Scrubber | `system_scrubber`: background scan + CRC baseline | `CONFIG_SYSTEM_SCRUBBER` | bit-rot scan + CRC baseline (overwritten post-link by `post_build_crc.py`) |
| Safe State | `safe_state` + `critical_data` + `hal_platform_safety` | `CONFIG_SAFETY_SHUTDOWN` | shutdown callbacks, bootloop guard, NMI stamp, dual-inverted critical storage, hardware latch + fault LED/buzzer |
| CPU Stop | `hal_cpu_emergency_stop_all_cores` (`hal/amp`) | `CONFIG_CPU_CORES > 1` | Stop all cores on AMP |

Key points:

1. **Recommended**: link and enable these modules (default on for production); they are already part of the `mini_tree` library.
2. **Optional**: with the Kconfig switches off, the core (device model / VFS / OSAL / EventBus) keeps working.
3. Unrelated to **EventBus seal** — sealing is core runtime behavior, not an optional brick.

---

## Related Documents

- [architecture.md](architecture.md) · [getting_started.md](getting_started.md) · [fast_path.md](fast_path.md)
- [amp.md](amp.md) · [peripherals.md](peripherals.md) · [ecosystem.md](ecosystem.md)
