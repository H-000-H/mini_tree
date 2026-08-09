# Memory & Flash Benchmarks

> Compiled artifact sizes and section layout (toolchain-dependent). For production builds, enable `CONFIG_SYS_LOG_LEVEL=0` (logging off) / `CONFIG_BUILD_SIZE=1` (size build) / `CONFIG_BUILD_SIZE_REPORT=1` (emit report); keep `CONFIG_BUILD_NO_LTO=0` (LTO on, strongly recommended).
>
> Report script: `tools/build_size.py`. Use `--format=html` for a distribution view, or `--format=baseline` for a baseline comparison. See [getting_started.md](getting_started.md) §4.3.

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
| `err_section` | error symbol table (`ERR_SECTION_BASE`); placed separately when `CONFIG_ERR_SECTION=1` — dedicated ROM area or RAM mirror |
| `*.noinit` | RAM not initialized by the runtime (WDT/RTC) |
| `.log_*` | logging registry (removed when logging is off) |

> WDT and `safe_state` belong to `system`; compiled in under `CONFIG_WDT=1` / `CONFIG_SAFE_STATE=1`. At flash time, ensure they are not physically overwritten along with interrupts and `err_section`.

---

## 2. Controls

| Kconfig | Effect |
| :--- | :--- |
| `CONFIG_SYS_LOG_LEVEL` | 0 = logging off (kept by default build) |
| `CONFIG_BUILD_SIZE` | 1 = size build (strip debug info) |
| `CONFIG_BUILD_SIZE_REPORT` | 1 = emit report |
| `CONFIG_BUILD_NO_LTO` | 0 = LTO on (recommended default) |
| `CONFIG_ERR_SECTION` | 1 = error symbols in a separate section (see §1) |
| `CONFIG_WDT` / `CONFIG_SAFE_STATE` | WDT / safe_state compiled in (see §1) |

---

## 3. Benchmarks

> Units in KiB. Example numbers; they drift with refactors and new bricks (retest periodically).

| Config | text | rodata | data | bss | flash total | Note |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| Minimal (framework + `osal` only) | 6.2 | 1.1 | 0.3 | 2.4 | 7.6 | `CONFIG_OSAL_NULL` + empty board |
| + device model | 11.8 | 2.0 | 0.6 | 4.1 | 13.8 | all of `board/` |
| + one VFS device (uart) | 15.3 | 2.6 | 0.8 | 5.2 | 18.7 | `vfs/uart` |
| + FreeRTOS backend | 19.1 | 3.3 | 1.1 | 6.9 | 23.5 | `CONFIG_OSAL_FREERTOS` |
| + WDT + safe_state | 20.4 | 3.5 | 1.2 | 7.3 | 24.9 | `CONFIG_WDT` + `CONFIG_SAFE_STATE` |

> Above table estimated with GCC `-Os` + LTO. Enabling logging (`CONFIG_SYS_LOG_LEVEL>0`) adds ~3–8 KiB `rodata`/`text` per tier; turning it off saves the most.
>
> Scope note: this table is **flash total** (text+rodata+data), a different metric from the "whole library 85.3→28.0 KB / default minimum ≈ 2.8 KB" **static RAM (bss+data) floor** narrative in [CHANGELOG.md](../../CHANGELOG.md) (a historical compression result counting RAM only). They are not directly comparable; the latest RAM floor is the `bss`/`data` columns above.

---

## 4. Scheduler Comparison (minimal-firmware measured)

> Measured with `arm-none-eabi-gcc 14.2.1`, `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -ffunction-sections -fdata-sections` + `--gc-sections`. Minimal firmware = `startup` + `main` (system layer) + linking the whole `mini_tree` library (RTOS kernel included), linked with an STM32F4-like script. Units in bytes; `RAM total = data + bss`.

| Scheduler | system backend | text | data | bss | RAM total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| Bare `while` (`XTASK_NONE`) | none | 86 | 0 | 0 | 0 |
| Cooperative `XTASK_COOP` | C | 31240 | 1744 | 944 | 2688 |
| Cooperative `XTASK_COOP` | C++ | 31240 | 1744 | 944 | 2688 |
| Preemptive `XTASK_PREEMPT` | C | 31368 | 1744 | 1388 | 3132 |
| Preemptive `XTASK_PREEMPT` | C++ | 31368 | 1744 | 1388 | 3132 |
| FreeRTOS | C | 34312 | 1752 | 1304 | 3056 |
| FreeRTOS | C++ | 34696 | 1752 | 1656 | 3408 |
| RT-Thread | C | 40228 | 1892 | 1164 | 3056 |
| RT-Thread | C++ | 40612 | 1892 | 1516 | 3408 |
| ThreadX | C | 34120 | 1752 | 940 | 2692 |
| ThreadX | C++ | 34504 | 1752 | 1292 | 3044 |
| uC/OS-II | C | 32992 | 1744 | 872 | 2616 |
| uC/OS-II | C++ | 33376 | 1744 | 1224 | 2968 |
| uC/OS-III | C | 33448 | 1744 | 1404 | 3148 |
| uC/OS-III | C++ | 33832 | 1744 | 1756 | 3500 |

> Scope: under `XTASK_NONE`, `OSAL_NULL_TASK_CPP` is auto-disabled by Kconfig (`depends on !XTASK_NONE`) and the osal/system layer depends on the xtask interface (`osal_null.h` unconditionally includes `xtask.h`), so with no implementation it cannot link; the firmware degrades to a minimal closure (startup + hand-written main loop) without the system/osal layer. RTOS backends' `text` already includes their respective kernels; numbers include the whole library (board device model, etc.) — **relative deltas** are the meaningful comparison. The bare-metal scheduler tri-state (`XTASK_NONE`/`XTASK_COOP`/`XTASK_PREEMPT`) is selected via the `Kconfig.mini_tree` choice; CMake injects `MINI_TREE_XTASK_*` macros to decide whether `xtask_coop.c` or `xtask_preempt.c` is compiled. Preemptive and cooperative expose the identical API (`xscheduler_task_create`/`x_scheduler_poll`/`xscheduler_start`), so caller code switches transparently.

Conclusions:
1. Bare `while` is smallest (86 B text, zero RAM) — at the cost of writing all scheduling logic yourself.
2. Bare-metal xtask (coop/preempt, ~31 KB text) is ~1.7 KB smaller than the smallest RTOS kernel (uC/OS-II ~33 KB) and needs **no per-task stack** (run-to-completion, task stack reuses the main-loop stack); preempt adds a task pool to bss (~444 B: 8×48 B slots + bitmap/list heads).
3. RTOS kernel cost ordering: uC/OS-II < uC/OS-III < ThreadX < FreeRTOS < RT-Thread (text 33.0 → 40.2 KB).
4. C vs C++ system backend: identical for bare-metal (coop/preempt); under RTOS, C++ costs ~+300–380 B text and +350–440 B bss more than C. **Pick the C backend for minimum size.**
5. Per-task extra cost: RTOS needs a TCB + dedicated task stack (stack sized per app, counted separately); xtask only has a static TCB (coop 28 B / preempt 48 B pool slot), no stack.

---

## 5. Trimming Advice

1. Turn off logging (`CONFIG_SYS_LOG_LEVEL=0`) — each `LOG_*` macro occupies space; this saves the most.
2. Enable LTO (`CONFIG_BUILD_NO_LTO=0`) — link-time merge of duplicates and dead-code elimination.
3. Picking the `CONFIG_OSAL_NULL` backend (bare metal) is smallest, but you must implement scheduling yourself.
4. Don't compile unused VFS / HAL: dependencies are driven by the CMake source set, so unreferenced ones never reach the binary.
5. `err_section` only needs `CONFIG_ERR_SECTION=1` when a dedicated ROM area / diagnostics are genuinely required.

---

## Related Docs

- [getting_started.md](getting_started.md) · [design_decisions.md](design_decisions.md)
