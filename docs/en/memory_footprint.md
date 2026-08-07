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

## 4. Trimming Advice

1. Turn off logging (`CONFIG_SYS_LOG_LEVEL=0`) — each `LOG_*` macro occupies space; this saves the most.
2. Enable LTO (`CONFIG_BUILD_NO_LTO=0`) — link-time merge of duplicates and dead-code elimination.
3. Picking the `CONFIG_OSAL_NULL` backend (bare metal) is smallest, but you must implement scheduling yourself.
4. Don't compile unused VFS / HAL: dependencies are driven by the CMake source set, so unreferenced ones never reach the binary.
5. `err_section` only needs `CONFIG_ERR_SECTION=1` when a dedicated ROM area / diagnostics are genuinely required.

---

## Related Docs

- [getting_started.md](getting_started.md) · [design_decisions.md](design_decisions.md)
