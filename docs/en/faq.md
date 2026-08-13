# Frequently Asked Questions

> The most common pitfalls in build, linking, clangd, probe, and OSAL switching.

| Item | Content |
| :--- | :--- |
| **Audience** | Everyone |
| **Related** | [getting_started.md](getting_started.md) · [problem_summary.md](problem_summary.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md) |

---

## clangd / IDE

### Whole-file errors, `compiler_compat.h` not found

1. Did you open the **mini_tree root directory**?
2. Is there a subdirectory `compile_flags.txt` overriding the root config? (delete the subdirectory one)
3. Restart Clangd.

### `SYS_LOG backend not configured`

`config.h` (or `ide/stubs/config.h`) must define `CONFIG_SYS_LOG_USE_PRINTF` or another log backend.

### `device_id_t` / `DEV_ID_COUNT` Unknown

The dtc-lite generated headers are missing. The IDE relies on `ide/stubs/board_nodes.h`; for real builds, add the generated directory to `-I`.

### ETL / `etl/string.h` not found

ETL is the **upper-layer C++ base library**, vendored under `lib/etl` (include + cmake only), linked into `mini_tree` by default at configure time (with a Fetch fallback in `cmake/etl.cmake` if missing).

If the IDE still reports `etl/string.h` not found: confirm `lib/etl/include` exists, or refresh `compile_commands.json` / the root `compile_flags.txt` (which already contains `-Ilib/etl/include`). See [ecosystem.md](ecosystem.md).

### What IDE should I use?

For daily work use **VSCode-based** editors/IDEs (**VS Code / Cursor / Qoder**), with clangd (see [getting_started.md §7](getting_started.md) and [debug_monitor.md](debug_monitor.md)). This branch does not follow up on traditional Keil or other non-VSCode platforms.

---

## Build & Linking

### HAL calls always return `VFS_ERR_NOTSUPP`

The platform strong symbols are not linked in, so the middleware weak empty implementations are still used. Check the target source list and link order.

### `hal_usb_*` poisoned

The implementation file must `#define HAL_USB_IMPL` before `#include "hal_usb.h"`.

### Calling `hal_can_*` etc. reports poisoned

Use `can_bus_*` instead; only the bus implementation file defines `CAN_BUS_IMPL` (or the corresponding macro).

### `ERR_PTR` / linking fails on `ERR_SECTION_BASE`

Merge `error_symbols.ld` or have the platform provide an equivalent `PROVIDE(ERR_SECTION_BASE=…)`.

---

## Device Tree & Probe

### Many failures in `board_driver_probe_all`

Check the DTS `status`, whether clock properties expand to 0, whether `compatible` matches `DRIVER_REGISTER`, and whether dependency `deps` probe first.

### Changed a driver macro but the table did not change

Clean the build directory, make sure CMake depends on that `.c`, and let dtc-lite rerun. Also confirm `lark` is installed (`pip install lark`) for dtc-lite.

---

## Runtime

### Bare-metal has no scheduling

Under `CONFIG_OSAL_NULL`, use `mini_tree_system_loop` + the bare-metal scheduler (`x_scheduler` / `x_task`, see `time_slice/task/xtask.h`; cooperative `xtask_coop.c` and preemptive `xtask_preempt.c` are mutually exclusive, gated by `CONFIG_XTASK_PREEMPT`); do not call `vTaskStartScheduler`.

### Priority behavior is inverted after switching RTOS

See [osal_switching.md](osal_switching.md): FreeRTOS and RT-Thread have opposite numeric priority semantics.

### Fails after reset, works after power-on

Check global constructors, WDT, peripheral clock gating, and link sections; the checklist is in [problem_summary.md](problem_summary.md).

---

## Related Documents

- [debug_monitor.md](debug_monitor.md) · [device_tree_porting.md](device_tree_porting.md)
- [usage.md](usage.md) · [ecosystem.md](ecosystem.md)
