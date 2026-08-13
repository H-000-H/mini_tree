# mini_tree Key Mechanisms Anatomy

> Eight mechanisms that run through the whole framework: **how they work, why they are designed this way, how to use them, and common pitfalls**. This document covers mechanisms only; it does not cover specific peripherals or board details.

| Item | Content |
| :--- | :--- |
| **Audience** | Engineers writing drivers, applications, or modifying the middleware |
| **Prerequisites** | Read [architecture.md](architecture.md) (layering and boot sequence) |
| **Related** | [fast_path.md](fast_path.md) (red lines) · [osal_switching.md](osal_switching.md) (OSAL backend switching) · [driver_guide.md](driver_guide.md) (driver authoring) · [runtime_services.md](runtime_services.md) |

---

## Table of Contents

1. [Compile-Time Registration Chain (pre_execution)](#1-compile-time-registration-chain-pre_execution)
2. [Two-Phase Boot - Why the Order Is Fixed](#2-two-phase-boot---why-the-order-is-fixed)
3. [Compile-Time Probe Table (DRIVER_REGISTER + dtc-lite)](#3-compile-time-probe-table-driver_register--dtc-lite)
4. [Single Time Base and Cooperative Scheduling (xtask)](#4-single-time-base-and-cooperative-scheduling-xtask)
5. [Long ISR Work Pattern (VIRQ top/bottom halves + lock-free channel)](#5-long-isr-work-pattern-virq-topbottom-halves--lock-free-channel)
6. [SPSC Lock-Free Channel and Double Buffer (algorithm/buffer)](#6-spsc-lock-free-channel-and-double-buffer-algorithmbuffer)
7. [Device Lifecycle State Machine (dev_lifecycle)](#7-device-lifecycle-state-machine-dev_lifecycle)
8. [Non-Blocking State Machines (Application-Layer Style)](#8-non-blocking-state-machines-application-layer-style)

---

## 1. Compile-Time Registration Chain (pre_execution)

### Mechanism

`core/include/compiler_compat.h` defines:

```c
#define pre_execution(x) __attribute__((constructor((x) + 100)))
```

`pre_execution(N)` emits a **GCC/Clang constructor function** that runs automatically before `main()`, ordered by priority. The larger `N`, the earlier it runs. All static initialization in the framework goes through this chain: **no hand-written init table, no runtime scanning**:

| Priority | Registration point | Initialization |
| :---: | :--- | :--- |
| `170` | `interrupt/interrupt.c` | Global bottom-half poller (FIFO + pending_drain) |
| `161` | `time_slice/task/xtask_preempt.c` | N+1 preemptive scheduler array (experimental, incomplete) |
| `160` | `time_slice/task/xtask_coop.c` | Cooperative scheduler `g_scheduler` (default) |
| `152` | `osal/src/osal_null.c` | Bare-metal queue pool |
| `151` | `osal/src/osal_null.c` | Bare-metal semaphore pool |
| `150` | `osal/src/osal_null.c` | Bare-metal mutex pool |

**Design intent**: pools, tables, and queues are ready before any business code touches them; the "larger number runs earlier" rule gives a natural dependency order (poller > scheduler > OSAL pools).

### Common Pitfalls

- Do not call runtime APIs such as `device_*` or `event_bus_post` inside a `pre_execution` function - `device_tree_init` has not run yet and the device table is still empty.
- Ordering of **same-priority** constructors across translation units is undefined; do not rely on it.

---

## 2. Two-Phase Boot - Why the Order Is Fixed

### Mechanism

Boot proceeds in four stages (C API in `system_c/include/system_init.h`):

| Stage | API | Typical work |
| :---: | :--- | :--- |
| 1 | `mini_tree_pre_os_init()` | Disable global interrupts, EventBus, safe_state, optional WDT, `device_tree_init` |
| — | (optional) business/platform prep | static config, extra registration |
| 2 | `mini_tree_start_tasks()` | `board_driver_probe_all`, TWDT, Flash Scrubber |
| 3 | `system_init_complete()` | Re-enable global interrupts |
| 4 | scheduler or bare-metal loop | FreeRTOS: ESP-IDF already starts the scheduler; bare-metal (`OSAL_NULL`): `mini_tree_system_loop` |

The C++ side (`mini_tree::system_pre_os_init()` / `system_start_tasks()`) mirrors stages 1/2 and finally calls `system_init_complete()` too.

### Why the Order Is Fixed

1. **`device_tree_init` must precede every device access**: the runtime instance tables (`device` / recursive mutex pool / `dev_lifecycle`) are static arrays, but each lock must be created via `osal_mutex_create_static_recursive`; nothing may touch `device_*` before that.
2. **Stage 1 must disable global interrupts**: during probe, `device_open` genuinely enables peripheral interrupts (NVIC), while VIRQ tables / bottom-half work may not be fully registered yet. Interrupts stay off until every ISR dependency is ready; `system_init_complete()` releases them uniformly.
3. **EventBus must exist first**: failed probe paths call `device_ops_unregister` → `event_bus_post(EVENT_SYS_DEVICE_REMOVED, ...)`; the event queue must already exist.
4. **Probe is in stage 2, not stage 1**: probe opens devices, logs, and on failure triggers `OSAL_PANIC` per criticality (needs `printf_output` and safe_state ready); those dependencies are only complete at the end of stage 1.
5. **Interrupts enable before the scheduler starts**: on RTOS paths, interrupts are re-enabled before `vTaskStartScheduler` so that interrupts firing at scheduler startup have a task context to land in.

### Common Pitfalls

- Calling `osal_delay_ms` between stages 1-2 (while global interrupts are off) depends on the tick interrupt and will hang - the `osal_null` backend has a tick-hang detector (§4), but RTOS backends do not.
- Do not probe devices inside stage 1: the logging/safety subsystems that `board_driver_probe_all` relies on are not initialized yet.

---

## 3. Compile-Time Probe Table (DRIVER_REGISTER + dtc-lite)

### Mechanism

`board/include/driver.h` defines:

```c
#define DRIVER_REGISTER(name, compat, probe_fn, remove_fn)                                         \
    int board_driver_probe_##name(struct device* pdev) { return probe_fn(pdev); }                  \
    int board_driver_remove_##name(struct device* pdev) { return remove_fn(pdev); }
```

Data flow:

```text
driver .c writes DRIVER_REGISTER(x, "compat,vendor", probe, remove)
  → dtc-lite scans the macro at compile time
  → generates probe/remove function tables + board_probe_order() + board_dev_find_* family
board_driver_probe_all()
  → takes devices in dependency-topology order → calls the compile-time function pointer directly
  → handles failures by criticality
```

Key points:

- **Zero strcmp at runtime**: the compatible string maps to a function pointer at compile time; runtime only looks up the table.
- **3-pass deferred probe**: `board_driver_probe_all` runs at most 3 passes; a driver returning `VFS_ERR_DEFER` (phandle dependency not ready) is retried next pass; if `deferred` stops shrinking it is a **stall**, and the stuck devices are permanently set to `DEVICE_STATUS_DISABLED`.
- **Failure grading** (`handle_probe_failure`): `DEVICE_CRIT_FATAL` → `OSAL_PANIC` safe shutdown; `DEVICE_CRIT_WARNING` → warn; `DEVICE_CRIT_IGNORE` → silent. Devices depending on a failed one are cascaded-disabled via `disable_dependents`.
- Drivers for unnamed nodes are silently disabled; named nodes without a driver are graded by criticality.

### Why Compile-Time Rather Than Runtime

- **Saves flash**: no compat string comparison code or match table.
- **Deterministic**: probe order is fixed at compile time from DTS dependency topology, independent of initialization order.
- **Auditable**: generated artifacts are plain C arrays in `generated/board/mini_tree/*` that can be inspected directly.

### Common Pitfalls

- Forgetting `DRIVER_REGISTER` in the driver `.c` → no entry in the generated table → device marked `DISABLED`, log shows "no generated probe".
- Returning `VFS_ERR_DEFER` that never resolves within 3 passes → stall → permanently disabled; make sure dependencies come earlier in probe order.
- The remove sequence (comment in `driver.h`) must be followed in order: `dev_lc_remove_start` → `device_ops_unregister` → `dev_lc_remove_drain` → teardown → `dev_lc_remove_finish` (see §7).

---

## 4. Single Time Base and Cooperative Scheduling (xtask)

### Mechanism

On the bare-metal backend (`CONFIG_OSAL_NULL`), the whole system has exactly one time source: `x_scheduler.tick_count`. `xscheduler_start()` selects the tick source in two levels — "chosen override first, else SysTick by default":

```text
① DTS explicitly sets chosen TIM (CHOSEN_SCHEDULER_TIM) → explicit override, generic TIM + VIRQ
  → xscheduler_start(): device_open → get hal_tim_device → register VIRQ(tim,0)
  → scheduler_tim_isr_top(): clear update flag + x_scheduler_tick(+tick_delay)   ← ISR only, nothing else

② No chosen → SysTick by default (Cortex-M architecture standard, zero-config)
  → hal_systick_init(DTC_GEN_TICK_RATE_HZ) configures SysTick (frequency via DTS, base hard-coded)
  → SysTick_Handler → hal_systick_irq_handler() + x_scheduler_tick(+tick_delay)  ← ISR only, nothing else

Non-ARM (RISC-V) has no SysTick; hal_systick_init returns NOTSUPP, so RISC-V boards must set chosen in DTS.
→ osal_time_ms() reads g_scheduler.tick_count directly                 ← one global clock
```

Task model (`time_slice/task/xtask.h`):

- `x_task`: intrusive linked-list node with fields `name` / `xTask_cb` / `period` / `next_running` / `is_running`.
- `xscheduler_task_create(sched, task, name, cb, period_ms)`: tail-inserts into the list; `next_running = current tick + period`.
- `x_task_run()` (called by `x_scheduler_poll()` in the main loop): iterates the list; enters only when `is_running` is false; uses `(int32_t)(now - next_running) >= 0` (**signed compare against uint32 wrap**) to decide expiry → runs the callback → `next_running = now + period`.

### Why This Design

- **Fixed time base, no drift**: `next_running = now + period` means callback execution time is not counted into the next period - no cumulative drift.
- **`is_running` is a re-entry guard, not an enable switch**: the comment states "only enters when not running", preventing a task re-entering from within its own callback; it is reset whether or not the task expired, so an un-expired branch cannot leave the task stuck as running.
- **Single global time base**: bare-metal `osal_time_ms()`, `osal_delay_ms()`, scheduler ticks, and bottom-half polling all share `g_scheduler.tick_count`; after switching to an RTOS, `osal_time_ms()` swaps to the RTOS tick with zero application changes.
- **Bare-metal delay has a deadlock backstop**: `osal_delay_ms` uses WFI busy-wait plus a tick-hang detector (breaks out after 10000 consecutive ticks without progress), preventing a hard deadlock if the time base never starts.

### Task Periods and Time Budget

Under cooperative scheduling all callbacks run **serially**, so this must hold:

```text
Σ(worst-case execution time of all callbacks) ≤ shortest task period
```

| Task period | Suggested callback budget | Typical use |
| :---: | :---: | :--- |
| 1 ms | ≤ 100 µs | high-rate sampling, PWM service |
| 5 ms | ≤ 1 ms | control loops, key scanning |
| 20 ms | ≤ 5 ms | state-machine advancement, protocol polling |
| 100 ms | ≤ 20 ms | slow peripheral scans, watchdog feeding |

When over budget, prefer in order: shorten blocking inside the callback (use a state machine, §8) → split tasks → move to `CONFIG_OSAL_FREERTOS` preemption (see `osal_switching.md`). The preemptive `xtask_preempt.c` (`CONFIG_XTASK_PREEMPT`) is experimental and incomplete; use an RTOS in production.

### protothread coroutine delays (PT_DELAY)

For "delay inside a callback without stalling other tasks", use the protothread macros in `xtask.h` (`PT_BEGIN`/`PT_DELAY`/`PT_END`). The task sleeps until its deadline while other tasks keep running, then resumes from the yield point — **no heap, no per-task stack** (just the `x_task.pt_line` field), at the cost of writing the callback as a state machine; **locals crossing a yield point are not preserved** (keep them in the TCB or statics).

> **Switch**: gated by Kconfig `CONFIG_XTASK_COROUTINE`, **on by default**. When on, use the pattern below; plain callbacks without PT macros behave exactly as before (backward compatible). When off, the PT_ macros are not defined and the scheduler skips coroutine handling — callbacks stay plain run-to-completion (periodic). Supported by both cooperative and preemptive schedulers.

```c
/* LED blink: on 100ms / off 100ms, without blocking other tasks */
static uint32_t s_led_blink_count;   /* cross-yield locals must be static or in TCB */

static void led_task_cb(x_task* task)
{
    PT_BEGIN(task);                    /* state-machine entry, must take `task` */

    for (;;)
    {
        led_on();
        s_led_blink_count++;
        PT_DELAY(task, 100);           /* yield 100ms, resume at the next line */

        led_off();
        PT_DELAY(task, 100);
    }

    PT_END(task);                      /* resets pt_line, back to the top (unreachable in a loop) */
}

/* Creation: period is the first dispatch delay; inner pacing is driven by PT_DELAY */
static x_task s_led_task;
/* xscheduler_task_create(&s_led_task, "led", led_task_cb, 0); */
```

Key points:
- Do **not** use `osal_delay_ms` (busy-wait, blocks the whole system) inside a callback; use `PT_DELAY(task, ms)` to yield instead.
- `PT_WAIT_UNTIL(task, cond)` / `PT_YIELD(task)` provide conditional waits / per-frame yields.
- Both schedulers (coop/preempt) check `pt_line` after the callback returns: non-zero means the coroutine is suspended and the deadline set by `PT_DELAY` is kept; zero means the next round advances by `period`.
- Backward compatible: plain callbacks without PT macros behave exactly as before.

### Common Pitfalls

- Calling `osal_delay_ms` (busy-wait) inside a callback stalls all periodic tasks; only allowed during initialization or for short timing; use `PT_DELAY` for in-callback delays (see the protothread section above).
- Callbacks must return quickly; no `while(1)` loops inside.
- `xscheduler_start()` must be called after `mini_tree_start_tasks()` (the comment states: VFS devices must be probed).

---

## 5. Long ISR Work Pattern (VIRQ top/bottom halves + lock-free channel)

### Mechanism

`interrupt/interrupt.h` provides **VIRQ virtual IRQ numbers + integrated bottom-half work queue**.

**VIRQ numbering**: blocks with `VIRTUAL_IRQ_BLOCK_SIZE = 8` (power of two); `VIRQ(block, idx)` computes the virtual IRQ number:

```text
VIRTUAL_IRQ_BLOCK_TABLE(X)  → system / tim / gpio / adc / uart / spi / i2c / i2s / user
```

**Registration and dispatch**:

```c
interrupt_virtual_register(VIRQ(tim, 0), scheduler_tim_isr_top, NULL, &ctx);
// top_half returning VFS_IRQ_ENTRY_BOTTOM (non-zero) → dispatch auto-submits bottom half
interrupt_virtual_dispatch(virq_num);   // called inside ISR
```

**Bottom-half work items** (`struct bottom_half_work`) use three atomic bits for **merge + rerun**:

```text
pending     queued or currently executing
executing   currently running (written by consumer only)
rerun       triggered again while fn() runs → rerun after completion, no event loss
```

Two consumer adapters:

| Path | Structure | Wake-up |
| :--- | :--- | :--- |
| Bare-metal (`CONFIG_OSAL_NULL`) | `bottom_half_poller`: fifo + `pending_drain` flag | main loop `interrupt_bottom_half_poll()`; ISR sets the flag, the main loop clears it before `run_pending` so a new ISR re-sets it - no lost wake-ups |
| RTOS | `bottom_half_task`: fifo + binary semaphore | dedicated task blocks on `osal_sem_wait`, ISR posts `post_from_isr` |

### The Full Pattern (how to do long work in ISRs)

```text
ISR (top_half, must be lightweight)
  ├─ read hardware flags / clear interrupt
  ├─ lock-free capture (write to SPSC FIFO, see §6)
  └─ return VFS_IRQ_ENTRY_BOTTOM  → dispatch submits bottom-half work
Main loop / bottom_half_task (bottom half, may be heavy)
  └─ protocol parsing, data processing, driver callbacks
```

### Why This Design

- ISRs forbid `printf` / mutexes / `malloc` / unbounded work (red lines in [fast_path.md](fast_path.md)); pushing heavy work to thread context means interrupt latency is decided by the top half alone.
- The `pending/executing/rerun` triple is used to **coalesce high-frequency triggers and avoid duplicate enqueueing**: a trigger during execution only sets `rerun`, and `bottom_half_run_pending` replays it afterwards. Note that a full FIFO makes submit fail and the work is dropped (see Common Pitfalls), so it is not lossless in every case.
- Bare-metal and RTOS share the same `bottom_half_submit_from_isr` / FIFO; only the wake-up mechanism differs.

### Common Pitfalls

- No `osal_mutex_lock` inside ISRs (returns `OSAL_ERR_ISR`); use `osal_spinlock` for critical sections.
- `bottom_half_run_pending` must be called in thread context; calling it inside an ISR returns immediately.
- A full FIFO makes submit return false and the work is dropped - size `BOTTOM_HALF_QUEUE_DEPTH` (power of two) against the worst-case interrupt rate.

---

## 6. SPSC Lock-Free Channel and Double Buffer (algorithm/buffer)

### Mechanism (`algorithm/buffer/buffer.h`)

`struct fifo_spsc`: a **strictly single-producer single-consumer** lock-free ring FIFO.

```c
struct fifo_spsc {
    fifo_data_type* buf;          /* 32-byte aligned */
    uint16_t size, mask;          /* size must be a power of two; mask = size-1 */
    ATTR_ALIGN(64) uint16_t w_ptr; /* write pointer, owns its cache line */
    ATTR_ALIGN(64) uint16_t r_ptr; /* read pointer, owns its cache line */
};
```

- **Lock-free**: reads/writes use only `__atomic` acquire/release ordering (`FIFO_LOAD_ACQUIRE` / `FIFO_STORE_RELEASE`).
- **Cache-line isolation**: `w_ptr` and `r_ptr` are each `ATTR_ALIGN(64)` to avoid false sharing (cache lines are 32/64 bytes on Cortex-M7/A, dual-core ESP32, etc.).
- **uint16_t overflow is the count**: `used space = w - r` (unsigned wrap); pointers are never trimmed, the physical index is `w & mask`.
- **Memory-ordering intent**: the writer stores `buf[w&mask]` first, then releases `w_ptr`; the reader acquires `w_ptr` before reading `buf` - the intent is that data becomes visible before the pointer is published. This relies on acquire/release semantics; verify the actual compiler/architecture support for that ordering.

### Who Writes, Who Reads

| Scenario | Producer | Consumer |
| :--- | :--- | :--- |
| ISR → main loop | ISR top half | main loop (bottom-half poll) |
| DMA → CPU | DMA-complete interrupt | business task |
| Bottom-half queue | internal to `interrupt.h` | `bottom_half_run_pending` |

### Double Buffer (`double_buffer_spsc`)

Read/write separation with swap switching: **DMA capture runs in parallel with CPU processing**. `double_buffer_write_data/read_data` each own one side; when DMA fills a block, the sides swap - the ADC / I2S DMA bottom halves (`g_adc_dma_bottom_half_work`) are the typical use.

### Common Pitfalls

- **Violating SPSC is undefined behavior**: multiple producers lose data / break ordering; multiple consumers double-consume. For multi-producer/multi-consumer use OSAL queues (locked).
- `fifo_init` requires `size` to be a power of two (`(size & (size-1)) != 0` returns immediately); wrong values fail silently.
- `fifo_data_type` is `uintptr_t`: it can hold 16-bit ADC samples or bottom-half work pointers (`interrupt.h` reuses it exactly that way).
- Block operations (`fifo_write_block/read_block`) handle cross-boundary memcpy around the ring; when `len` exceeds free space they truncate to `free_len` and return the actual count.

---

## 7. Device Lifecycle State Machine (dev_lifecycle)

### Mechanism (`board/src/dev_lifecycle.c`)

`struct dev_lifecycle` is a **CAS-sentinel, lock-free state machine** tracking the open count and in-flight I/O count:

```text
opens       open reference count
io_active   active I/O count
state       UNINITIALIZED → LIVE → REMOVING → (RESET)
```

- `open_begin` / `io_begin`: CAS loop increments; rejects immediately on `-1` (teardown locked) or non-`LIVE`.
- `remove_drain` (teardown drain, two-phase CAS):
  1. CAS `opens` 0→`-1` (`DEV_LC_LOCKED`); on failure (an open is still in flight) retry after `osal_delay_ms(1)` until it settles at zero;
  2. **once locked, `opens` stays `-1` and is never rolled back**; under the `state == REMOVING` gate, keep CAS-ing `io_active` 0→`-1` until it drains (failure only retries io_active).
- Design intent (source comment): state-machine gating (`state == REMOVING` is a precondition for entering drain; meanwhile both `open_begin`/`io_begin` check `state == LIVE`, so new counts do not arrive) + monotonic lock (opens never rolls back to 0, avoiding the transient-exposure window) + memory ordering (ACQUIRE/RELEASE/ACQ_REL) + no-ABA consideration (`-1` is only reset by `remove_finish`; monotonic operations usually do not reproduce the same value). When drain exits, both counters are stably `-1`, and concurrent open/io seeing `-1` are rejected. This logic relies on code review and is not formally verified.

### Usage Sequence (standard driver remove flow, comment in `driver.h`)

```c
dev_lc_remove_start(device_lc(pdev));      // state → REMOVING
device_ops_unregister(pdev);               // REMOVED + broadcast EVENT_SYS_DEVICE_REMOVED + clear ops under lock (TOCTOU guard)
dev_lc_remove_drain(device_lc(pdev), OSAL_WAIT_FOREVER);  // atomic polling, no lock held
... teardown ...
dev_lc_remove_finish(device_lc(pdev));     // RESET
```

### Why This Design

- **Safe hot-remove/unplug**: unloading is designed to wait until there is no in-flight open/I/O before drain exits, and it **does not wait while holding a lock** (atomic polling avoids blocking other threads / deadlock).
- **`device_ops_unregister` severs ops under the lock**: VFS entries like `device_write` do check-then-act inside the lock; the unloader clears `ops` under the same lock, blocking TOCTOU (thread A passed the status check while thread B unloads → NULL deref → HardFault).

### Common Pitfalls

- `remove_drain` returns `VFS_ERR_TIMEOUT` on timeout; with `OSAL_WAIT_FOREVER` a never-released open waits forever - business code must pair open/io.
- `dev_lc_open_begin` return semantics: 1 on first open, 0 on repeated open, negative error on failure - do not treat "repeated open" as an error.

---

## 8. Non-Blocking State Machines (Application-Layer Style)

### Background

On the bare-metal backend (`CONFIG_OSAL_NULL`), `osal_task_create` returns `OSAL_ERR_NOTSUPP` - **bare metal has no OS tasks**. The `osal_null.c` header comment is explicit:

> Complex tasks must use state machines and task switching (just use an OS for daily work unless memory-constrained or requiring extremely high efficiency).

So "do X after 500 ms" on bare metal cannot use blocking delays; instead:

```text
record timestamp → poll (now - start) → advance state when elapsed
```

### Skeleton

```c
typedef enum { S_IDLE, S_WAIT_DELAY, S_DONE } my_state_t;

static my_state_t  st;
static uint32_t    t_start;

void my_task_cb(x_task* t)          /* registered to xtask, period 5 ms */
{
    switch (st)
    {
    case S_IDLE:
        st = S_WAIT_DELAY;
        t_start = osal_time_ms();
        break;
    case S_WAIT_DELAY:
        if ((osal_time_ms() - t_start) >= 500U)   /* unsigned subtract, wrap-safe */
            st = S_DONE;
        break;
    case S_DONE:
        /* single-shot complete; return to S_IDLE to repeat */
        st = S_IDLE;
        break;
    }
}
```

Key points:

- **Use `osal_time_ms()` timestamps and poll**; do not block with `osal_delay_ms` - the callback never blocks and other periodic tasks are unaffected.
- `(now - start)` uses unsigned subtraction, **handling uint32 wrap naturally** (no breakage after 49.7 days).
- Break complex tasks into multiple states, advancing a little each period; this also satisfies the §4 time budget.

### Difference on RTOS Backends

After switching to `CONFIG_OSAL_FREERTOS`, `osal_delay_ms` is **real sleep** (task suspended, CPU released) and blocking is fine; the same state-machine code runs on both bare metal and RTOS - a portable lowest-common-denominator style.

### Common Pitfalls

- Busy-waiting `osal_delay_ms` inside an ISR or tick callback on bare metal - stalls the whole time base.
- Forgetting to reset the state at the terminal state - the task runs once and then spins forever.
- Do not use `volatile` for state variables - periodic tasks run serially on the same thread; plain `static` is enough.

---

## Related Documents

- [architecture.md](architecture.md) (layering and boot sequence) · [fast_path.md](fast_path.md) (ISR/hot-path red lines)
- [osal_switching.md](osal_switching.md) (OSAL backend switching) · [driver_guide.md](driver_guide.md) (driver authoring and remove lifecycle)
- [runtime_services.md](runtime_services.md) (EventBus / VIRQ / BufferPool) · [design_decisions.md](design_decisions.md) (design rationale)
