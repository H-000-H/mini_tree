# Memory & Flash Benchmarks

> Compiled artifact sizes and section layout (toolchain-dependent). Trimming & size knobs are listed in §2, optimization advice in §5; verify section layout with the compiler map file / `--gc-sections` report.

| Item | Content |
| :--- | :--- |
| **Audience** | Optimizing size / evaluating cost |
| **Related** | [getting_started.md](getting_started.md) (build & metrics) · [design_decisions.md](design_decisions.md) (trimming preference) |

---

## 1. Section Layout

| Section | Description |
| :--- | :--- |
| `text` | code & read-only constants |
| `rodata` | read-only constants |
| `data` | initialized globals |
| `bss` | uninitialized globals (no flash cost) |
| `err_section` | error symbol table (`ERR_SECTION_BASE`, see `error_symbols.ld`) — dedicated ROM area or RAM mirror |
| `*.noinit` | RAM not initialized by the runtime (WDT/RTC) |
| `.log_*` | logging registry (removed when logging is off) |

> WDT and `safe_state` belong to `system` (gated by `CONFIG_SYSTEM_WDT` / `CONFIG_SAFETY_SHUTDOWN`). At flash time, ensure they are not physically overwritten along with interrupts and `err_section`.

---

## 2. Controls

| Kconfig | Effect |
| :--- | :--- |
| `CONFIG_SYSTEM_WDT` | framework watchdog (on by default) |
| `CONFIG_SAFETY_SHUTDOWN` | safe-shutdown callbacks (off by default) |
| `CONFIG_SYS_LOG_USE_PRINTF` / `_OSAL` / `_ESP` | `SYS_LOG*` backend selection (logging off saves the most) |
| `CONFIG_PRODUCTION_LOG` | black-box fault recording (off by default) |
| `CONFIG_EVENT_BUS` / `CONFIG_SYSTEM_CMD` / `CONFIG_SYSTEM_SCRUBBER` | optional-feature master switches (off by default) |
| `CONFIG_BUILD_DISASM` | disassembly post-build (on by default; turn off as needed) |

---

## 3. Benchmarks

> Units in KiB. Example numbers; they drift with refactors and new bricks (retest periodically).

| Config | text | rodata | data | bss | flash total | Note |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| Minimal (framework + `osal` only) | 6.2 | 1.1 | 0.3 | 2.4 | 7.6 | `CONFIG_OSAL_NULL` + empty board |
| + device model | 11.8 | 2.0 | 0.6 | 4.1 | 13.8 | all of `board/` |
| + one VFS device (uart) | 15.3 | 2.6 | 0.8 | 5.2 | 18.7 | `vfs/uart` |
| + FreeRTOS backend | 19.1 | 3.3 | 1.1 | 6.9 | 23.5 | `CONFIG_OSAL_FREERTOS` |
| + WDT + safe_state | 20.4 | 3.5 | 1.2 | 7.3 | 24.9 | `CONFIG_SYSTEM_WDT` + `CONFIG_SAFETY_SHUTDOWN` |

> Above table estimated with GCC `-Os` + LTO. Enabling logging (`CONFIG_SYS_LOG_LEVEL>0`) adds ~3–8 KiB `rodata`/`text` per tier; turning it off saves the most.
>
> Scope note: this table is **flash total** (text+rodata+data), a different metric from the "whole library 85.3→28.0 KB / default minimum ≈ 2.8 KB" **static RAM (bss+data) floor** narrative in [CHANGELOG.md](../../CHANGELOG.md) (a historical compression result counting RAM only). They are not directly comparable; the latest RAM floor is the `bss`/`data` columns above.

---

## 4. Scheduler Comparison (minimal-firmware measured)

> Measured with `arm-none-eabi-gcc 13.3.1` (Windows, older than the previous 14.2.1/Linux — builds fine on the older toolchain), `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -ffunction-sections -fdata-sections` + `--gc-sections`. Minimal firmware = `startup` (vector table + Reset_Handler) + `main` (standard system-layer startup sequence) + linking the whole `mini_tree` library (RTOS kernel included, `--start-group` for circular references), linked with an STM32F4-like script (FLASH 1 MiB / RAM 128 KiB). Units in bytes; `RAM total = data + bss`. Two libc baselines are reported: **newlib-nano** (`--specs=nano.specs`, the usual minimal-size choice) and **full newlib** (the previous table's baseline); the `.config` baseline is the current repo default (event bus/WDT/OSAL logging on), unreferenced modules (lwIP/USB, etc.) are kept out of the closure by `--gc-sections`. Absolute values drift with toolchain and baseline config — **relative deltas** are the meaningful comparison.

### 4.1 newlib-nano (recommended baseline)

| Scheduler | system backend | text | data | bss | RAM total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| Bare `while` (`XTASK_NONE`) | none | 134 | 0 | 512 | 512 |
| Cooperative `XTASK_COOP` | C | 11153 | 116 | 4400 | 4516 |
| Cooperative `XTASK_COOP` | C++ | 11237 | 120 | 4432 | 4552 |
| Preemptive `XTASK_PREEMPT` | C | 11565 | 116 | 4848 | 4964 |
| Preemptive `XTASK_PREEMPT` | C++ | 11649 | 120 | 4880 | 5000 |
| mini-os | C | 14245 | 120 | 2620 | 2740 |
| mini-os | C++ | 14381 | 128 | 3016 | 3144 |
| FreeRTOS | C | 17492 | 108 | 12176 | 12284 |
| FreeRTOS | C++ | 17628 | 116 | 12528 | 12644 |
| RT-Thread | C | 17953 | 272 | 35084 | 35356 |
| RT-Thread | C++ | 18057 | 280 | 35480 | 35760 |

### 4.2 Full newlib (previous table's baseline)

| Scheduler | system backend | text | data | bss | RAM total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| Bare `while` (`XTASK_NONE`) | none | 134 | 0 | 512 | 512 |
| Cooperative `XTASK_COOP` | C | 35732 | 1768 | 4448 | 6216 |
| Cooperative `XTASK_COOP` | C++ | 35820 | 1772 | 4480 | 6252 |
| Preemptive `XTASK_PREEMPT` | C | 36148 | 1768 | 4896 | 6664 |
| Preemptive `XTASK_PREEMPT` | C++ | 36228 | 1772 | 4928 | 6700 |
| mini-os | C | 38776 | 1772 | 2664 | 4436 |
| mini-os | C++ | 38912 | 1780 | 3064 | 4844 |
| FreeRTOS | C | 42068 | 1764 | 12224 | 13988 |
| FreeRTOS | C++ | 42204 | 1772 | 12576 | 14348 |
| RT-Thread | C | 48984 | 1924 | 35136 | 37060 |
| RT-Thread | C++ | 49088 | 1932 | 35528 | 37460 |

### 4.3 Scope and conclusions

**Heap accounting (why bss is not directly comparable across backends)**:

- FreeRTOS: heap is a static array `ucHeap[CONFIG_FREERTOS_HEAP_SIZE]` (default 8192), **counted in bss**;
- RT-Thread: heap is a static array `s_rtt_heap[CONFIG_RTT_HEAP_SIZE]` (default 32×1024, see `osal_rtthread.c`), **counted in bss**;
- mini-os: heap is a link-time region (`__mini_os_heap_start`→`__mini_os_heap_end`, from end of bss to top of stack), **not counted in bss** — all remaining RAM goes to the heap;
- bare-metal xtask: no heap.

Framework bss excluding the configurable heap (nano / C++): coop 4432 · mini-os 3016 · FreeRTOS 4336 · RT-Thread 2712. The bare row's 512 B bss is the linker-script `._user_heap_stack` minimum-heap placeholder, not real usage; all other rows include it as well.

Scope (as before): under `XTASK_NONE`, `OSAL_NULL_TASK_CPP` is auto-disabled by Kconfig (`depends on !XTASK_NONE`) and the osal/system layer depends on the xtask interface (`osal_null.h` unconditionally includes `xtask.h`), so with no implementation it cannot link; the firmware degrades to a minimal closure (startup + hand-written main loop) without the system/osal layer. RTOS backends' `text` already includes their respective kernels; numbers include the whole library (board device model, etc.) — **relative deltas** are the meaningful comparison. The bare-metal scheduler tri-state (`XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`) is selected via the `Kconfig.mini_tree` choice; CMake injects `MINI_TREE_XTASK_*` macros to decide whether `xtask_coop.c` or `xtask_preempt.c` is compiled. Preemptive and cooperative expose the identical API (`xscheduler_task_create`/`x_scheduler_poll`/`xscheduler_start`), so caller code switches transparently.

Conclusions:

1. Bare `while` is smallest (134 B text) — at the cost of writing all scheduling logic yourself.
2. Bare-metal xtask (coop 11.2 KB) is the smallest "scheduling-capable" option and needs **no per-task stack** (run-to-completion, task stack reuses the main-loop stack); preempt adds a 448 B task pool to bss.
3. RTOS kernel text: **mini-os (14.4 KB) < FreeRTOS (17.6 KB) < RT-Thread (18.1 KB)**; mini-os saves ~3.2 KB over FreeRTOS and ~3.7 KB over RT-Thread (mini-os does not yet implement SMP/MPU and similar parts — once finished the gap will be small: the kernels all land around 17–18 KB, the kernel sources are just that big and hard to shrink further unless features are actively trimmed).
4. RTOS kernel text: **mini-os (14.4 KB) < FreeRTOS (17.6 KB) < RT-Thread (18.1 KB)**, mini-os currently saves ~3.2–3.7 KB; **the gap mainly comes from feature-set differences** — mini-os does not yet implement SMP/MPU/memory-protection parts, and once completed it is expected to be on par with FreeRTOS/RT-Thread (the 17–18 KB range). A complete kernel's text at this magnitude is normal; further compression only comes from trimming features, and `CONFIG_RTT_HEAP_SIZE`/`CONFIG_FREERTOS_HEAP_SIZE` etc. can be tuned to align.
5. C vs C++ system backend: under RTOS, C++ costs ~+100–140 B text and +350–400 B bss more than C; nearly identical for bare-metal (+84 B text / +32 B bss). **Pick the C backend for minimum size.**
6. **libc impact (4.1 vs 4.2)**: full newlib costs **~+24.6 KB text and ~+1.65 KB data** (stdio structures) over nano, bss only ~+48 B; RT-Thread is an outlier at ~+6.4 KB more (its kservice is configured to use libc formatting via `RT_KLIBC_USING_LIBC_VSNPRINTF`, pulling in the full vfprintf). libc is a constant overhead that does not affect cross-backend comparison; use `--specs=nano.specs` for minimum size.
7. Per-task extra cost: RTOS needs a TCB + dedicated task stack (stack sized per app, counted separately); xtask only has a static TCB (coop 28 B / preempt 48 B pool slot), no stack.

---

## 5. Trimming Advice

1. Turn off logging (skip the `CONFIG_SYS_LOG_USE_*` backends or reduce log volume) — each `LOG_*` macro occupies space; this saves the most.
2. Use `-Os` + `-ffunction-sections -fdata-sections -Wl,--gc-sections` (see §6.2) for dead-code elimination.
3. Picking the `CONFIG_OSAL_NULL` backend (bare metal) is smallest, but you must implement scheduling yourself.
4. Don't compile unused VFS / HAL: dependencies are driven by the CMake source set, so unreferenced ones never reach the binary.
5. `err_section` only matters when a dedicated ROM area / diagnostics are genuinely required — keep the `error_symbols.ld` link then.

---

---

## 6. Worked Example: stm32f103c8t6-node Memory Optimization

> This section records the complete optimization run of a real project (`Host-Device-Architecture-stm32f103c8t6-node`, STM32F103C8T6, 20 KB RAM / 64 KB Flash, `arm-none-eabi-gcc`, linked with `--gc-sections`) — from "near overflow" to "safe zone". Data comes from that project's build logs (`build_log*.txt`).

### 6.1 Evolution Overview (measured)

| Stage | RAM (B) | RAM % | FLASH (B) | FLASH % | Main action |
| :--- | ---: | ---: | ---: | ---: | :--- |
| Initial (full-feature build) | 17,928 | 87.54% | 46,692 | 71.25% | everything on, RAM near overflow |
| Drop drivers | 17,928 | 87.54% | 45,724 | 69.77% | removed unused product drivers (air780e/hc05/dfplayer/neo_m8n etc.) |
| Trim framework | 15,376 | 75.08% | 45,636 | 69.64% | further cut system/middleware |
| Disable Scr / SysCmd etc. | 12,456 | 60.82% | 39,636 | 60.48% | `CONFIG_SYSTEM_SCRUBBER` / `CONFIG_SYSTEM_CMD` off |
| **Final (Release -Os)** | **9,896** | **48.32%** | **22,692** | **34.63%** | `-Os` + gc-sections + trimming converged |

> Net: RAM **17,928 → 9,896 B (−44.8%)**, FLASH **46,692 → 22,692 B (−51.4%)**; RAM went from "87% at risk" to "48% safe".

### 6.2 Concrete Trims Applied (`.config` effective state)

| Kconfig / config | Value | Impact |
| :--- | :--- | :--- |
| `CONFIG_OSAL_NULL` | `y` | drop FreeRTOS/RT-Thread, use bare-metal OSAL — main RAM driver (RTOS needs per-task TCB + dedicated stack; xtask reuses the main-loop stack) |
| `CONFIG_XTASK_PREEMPT` | `y` | preemptive xtask coroutines + `CONFIG_XTASK_COROUTINE` |
| `# CONFIG_SYSTEM_SCRUBBER` | unset | disable startup memory scrubber |
| `# CONFIG_SYSTEM_CMD` | unset | disable command-line shell |
| `CONFIG_SYSTEM_WDT` | `y` | keep watchdog (safety item, not trimmed) |
| `CONFIG_SYSTEM_CPP` | `y` | C++ system backend |
| compile/link | `-Os -fdata-sections -ffunction-sections -Wl,--gc-sections` | strip unreferenced functions/data |
| HAL / driver source set | on demand | `mini_tree/CMakeLists.txt` compiles only needed modules |

### 6.3 Key Findings

1. **`--gc-sections` is active but barely affects RAM**: it strips "unreferenced standalone sections" (mainly `text`/FLASH), while the RAM bulk is `bss` (task pool, queue buffers, etc.) — which is always referenced and cannot be gc'd. This is exactly why Release saved only ~1.7 KB FLASH over Debug while RAM barely moved (9,776 → 9,768 B).
2. **RAM is cut by feature trimming, not by optimization level**: switching to `CONFIG_OSAL_NULL` and disabling `SYSTEM_SCRUBBER`/`SYSTEM_CMD` are what actually lowered RAM.
3. **Static-library trimming granularity is limited**: `mini_tree` is a `STATIC` library; the linker pulls in whole `.o` files, so `--gc-sections` can only drop individual unreferenced sections within an `.o`; global data not split into sections stays.
4. **To reduce RAM further**: shrink the queue buffer (`CONFIG_OSAL_NULL_QUEUE_BUF_SZ`, currently 1024), disable `CONFIG_EVENT_BUS`/`CONFIG_VIRQ` (currently `y`), or shrink the task pool.

---

### 6.4 About C++

> C++ stays size-manageable given: `-fno-exceptions -fno-rtti` + avoiding iostream (use printf / raw output) + `--specs=nano.specs`. Untrimmed, C++ can add several KB — a configuration issue rather than a language issue; heavy template use also bloats the binary.

## Related Docs

- [getting_started.md](getting_started.md) · [design_decisions.md](design_decisions.md)
