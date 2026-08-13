# Quick Start

> Integrate `mini_tree` into your platform project from scratch: dependencies → configuration → CMake → ignition.
>
> **Reference template**: [device-platform](https://github.com/H-000-H/device-platform) — mini_tree's companion platform example with a full port (DTS, HAL, `board_${IDF_TARGET}` convention auto-discovery, AMP).

| Item | Content |
| :--- | :--- |
| **Audience** | Platform and application engineers |
| **Prerequisites** | CMake, basic C; have a target board or at least be able to link a firmware |
| **Related** | [device_tree_porting.md](device_tree_porting.md) · [usage.md](usage.md) · [ecosystem.md](ecosystem.md) · [tools_guide.md](../tools_guide.md) |

---

## Table of Contents

1. [Dependencies](#1-dependencies)
2. [Acquisition & Layout](#2-acquisition-layout)
3. [Configuration System (Kconfig)](#3-configuration-system-kconfig)
4. [CMake Integration](#4-cmake-integration)
5. [Board DTS Override](#5-board-dts-override)
6. [Ignition Sequence](#6-ignition-sequence)
7. [IDE (clangd)](#7-ide-clangd)
8. [Acceptance Checklist](#8-acceptance-checklist)

---

## 1. Dependencies

| Dependency | Purpose | Notes |
| :--- | :--- | :--- |
| CMake ≥ 3.16 | Build the static library | Ninja / Make either works |
| Python 3 | genconfig, dtc-lite, gen_compile_db | — |
| `lark` | dtc-lite parsing | `pip install lark` |
| Built-in kconfiglib (`tools/_vendor/`) | menuconfig / guiconfig | Bundled in the repo, **no install needed**; both UIs in [tools_guide.md](../tools_guide.md) |
| clang-format ≥ 15 / clang-tidy (optional) | Code style and naming checks | see [coding_style.md](coding_style.md) |
| Platform toolchain + SDK | Real hardware | **Only** linked into the platform project, never into middleware public headers |

---

## 2. Acquisition & Layout

Vendor this repository as a subdirectory or submodule, e.g. `third_party/mini_tree`. Paths you will touch often:

| Path | Purpose |
| :--- | :--- |
| `CMakeLists.txt` | `add_subdirectory` entry point |
| `.config` / `Kconfig` | feature trimming |
| `board/dts/board.dts` | default placeholder (must be overridden by the platform) |
| `board/dtsi/` | node templates: `example-soc.dtsi` + `vfs/` (11) + `drivers/` (37), all-0 placeholders to copy & fill (see [driver_guide.md](driver_guide.md) §1) |
| `ide/stubs/` | IDE headers when there are no build artifacts |

---

## 3. Configuration System (Kconfig)

### 3.0 File Layout

Kconfig entry points come in two sets by build backend, both sourcing the same shared config tree `Kconfig.mini_tree` to avoid divergence:

| File | Path | Role | Used by |
| :--- | :--- | :--- | :--- |
| `Kconfig.mini_tree` | repo root | shared config tree (`menu "mini_tree Configuration" ... endmenu`), no `mainmenu` | sourced by both entry points |
| `Kconfig.non_esp` | repo root | non-ESP entry: `mainmenu` + `source "Kconfig.mini_tree"` (renamed to avoid IDF component-scan auto-discovery, which would double-source it alongside `Kconfig.projbuild`) | `tools/genconfig.py` / `menuconfig.py` / non-ESP `CMakeLists.txt` |
| `Kconfig.projbuild` | repo root | ESP-IDF entry: `orsource "Kconfig.mini_tree"` (relative to this file), injected into the top-level Kconfig tree by IDF confgen | ESP-IDF (`idf.py menuconfig` / `idf.py reconfigure`) |

Under the ESP path, `idf.py menuconfig` shows a "mini_tree Configuration" submenu at the top level; all `OSAL_*` / `SYSTEM_*` / `EVENT_BUS` switches are evaluated by IDF's `depends on` / `default` / `range` and written into `sdkconfig.h` — no manual `.config` editing needed.

### 3.1 Generate `config.h`

```bash
cd path/to/mini_tree
python3 tools/genconfig.py Kconfig build/generated/kconfig/mini_tree --config .config
```

The root `CMakeLists.txt` runs the same logic during the configure stage (the ESP path skips genconfig and lets IDF's `sdkconfig.h` inject `CONFIG_*` instead).

### 3.2 Common Options

| Menu | Symbol | Description |
| :--- | :--- | :--- |
| Platform | `PLATFORM_ARM_CM4F` etc. | architecture hint (paired with the toolchain) |
| Multi-core | `CPU_CORES` / `AMP_MODE` | 1=single core; 2=AMP |
| OSAL | `OSAL_NULL` / `FREERTOS` / `RTTHREAD` | runtime backend: bare-metal cooperative / FreeRTOS v11.3.0 / RT-Thread v5.3.0 |
| OSAL Capacity | `OSAL_NULL_MAX_QUEUES` (base queue count, +1 auto when EventBus on) / `OSAL_NULL_QUEUE_BUF_SZ` / `FREERTOS_HEAP_SIZE` / `RTT_HEAP_SIZE` | queue & heap RAM (backend-scoped) |
| System | `SYSTEM` / `SYSTEM_CPP` / `SYSTEM_C` | master switch (default on) + language backend |
| Log | `SYS_LOG_USE_PRINTF` / `OSAL` | `SYS_LOG*` backend |
| Board Features | `SYSTEM_WDT` / `SYSTEM_SCRUBBER` etc. | framework watchdog (on) / CRC scrubber (off), depends on `SYSTEM` |
| Runtime | `EVENT_BUS` / `EVENT_BUS_*` / `OSAL_MUTEX_POOL_SIZE` / `BOTTOM_HALF_QUEUE_DEPTH` | master switch + capacity |

`SYSTEM` is an optional module **enabled by default**; `EVENT_BUS` and `SYSTEM_CMD` are **off by default**: turning off `SYSTEM` trims `system_c/`, `system_cpp/` and EventBus together; turning on `EVENT_BUS` adds the pub/sub bus while keeping the two-phase boot and watchdogs.

The repository's bundled `.config` uses common defaults: `OSAL_NULL` + `SYSTEM`/`SYSTEM_CPP` + `SYSTEM_WDT` + `SYS_LOG_USE_PRINTF` (`EVENT_BUS` / `SYSTEM_CMD` / `SYSTEM_SCRUBBER` off).

---

## 4. CMake Integration

```cmake
# example platform project CMakeLists.txt
add_subdirectory(third_party/mini_tree)

add_executable(my_fw
    Core/Src/main.c
    platform/hal_gpio_stm32.c          # e.g. strong-symbol override
    platform/hal_uart_stm32.c
    # …
)

target_link_libraries(my_fw PRIVATE mini_tree)

# override device tree (required)
set(BOARD_DTS      "${CMAKE_SOURCE_DIR}/board/dts/my_board.dts" CACHE FILEPATH "" FORCE)
set(BOARD_DTSI_DIR "${CMAKE_SOURCE_DIR}/board/dtsi" CACHE PATH "" FORCE)

# vendor-header -I for dtsi #include macro expansion (as needed)
set(VENDOR_INC_DIRS "${CUBE_INC};${HAL_INC}" CACHE STRING "" FORCE)
```

### 4.1 Platform CACHE Variables / Options

| Variable | Type | Purpose |
| :--- | :--- | :--- |
| `BOARD_DTS` | `FILEPATH` | board-level entry `.dts` (**must** override the default placeholder) |
| `BOARD_DTSI_DIR` | `PATH` | dtsi search directory |
| `VENDOR_INC_DIRS` | `STRING` | vendor-header `-I` for dtc/cpp macro expansion |
| `VENDOR_DEFINES` | `STRING` | extra `-D` (rarely used) |
| ETL (`cmake/etl.cmake`) | — | **vendored in `lib/etl`** (include + cmake only); always linked by the root CMake (Fetch fallback if missing) |
| Other open-source bricks | — | TinyUSB / lwIP / cJSON are **config-time** FetchContent (root CMake directly `include`s their `cmake/*.cmake`); the rest (LVGL, u8g2, littlefs, FatFs, SFUD, Mbed TLS, coreMQTT, coreHTTP, nanopb, miniz, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB) use link-time FetchContent, enabled via `mini_tree_link_*`, fetching over the network on first use, see [ecosystem.md](ecosystem.md) |
| `mini_tree_add_rust_crate` | — | optional; see `cmake/rust.cmake` |
| `CONFIG_BUILD_DISASM` | Kconfig | adds a disassembly post-build step when enabled (`cmake/disasm.cmake`) |

Set `... CACHE ... FORCE` **before** `add_subdirectory(mini_tree)` to avoid locking in the default placeholder DTS on the first configure.

The `mini_tree` target will:

1. Run `genconfig.py`
2. Run dtc-lite (scan `DRIVER_REGISTER` in vfs/bus/drivers and generate the compile-time probe table)
3. Pick OSAL / SYSTEM sources per `.config`; link the vendored kernels in `lib/` (FreeRTOS v11.3.0 / RT-Thread v5.3.0)
4. Config-time bricks (TinyUSB / lwIP / cJSON) are directly `include`d by the root CMake via their `cmake/*.cmake`; the rest are enabled at link time by the product side via `mini_tree_link_*` (may fetch over the network on first use)

Language-backend comparison: [runtime_services.md](runtime_services.md#3-system_c-vs-system_cpp); USB board-level contract: [usb_tusb_port.md](usb_tusb_port.md); brick list: [ecosystem.md](ecosystem.md).

### 4.2 What about ESP-IDF?

**Do not** `add_subdirectory` this repository's root directly into IDF.
ESP uses `EXTRA_COMPONENT_DIRS` + `idf_component_register` (the component path is triggered by `ESP_PLATFORM`); see **[esp_idf_cmake.md](esp_idf_cmake.md)** (against the `platform/Espressif/esp32s3` platform repo).

---

## 5. Board DTS Override

The middleware's default `board/dts/board.dts` has **only an empty root node** and cannot drive real peripherals.

The platform must at least provide:

- an entry `.dts` (model/compatible, chosen, status)
- SoC / peripheral `.dtsi`
- vendor macros in nodes when needed (cpp-expanded via `VENDOR_INC_DIRS`)

Details and the compatible list are in [driver_guide.md](driver_guide.md).

---

## 6. Ignition Sequence

### 6.1 C (`system_init.h`)

```c
#include "system_init.h"
#include "config.h"

int main(void)
{
    /* platform: clocks, heap, console … */

    mini_tree_pre_os_init();
    /* optional: static init of business services */

    mini_tree_start_tasks();   /* probe + framework tasks */
    /* optional: create business tasks via osal_task_create */

    system_init_complete();

#if defined(CONFIG_OSAL_NULL)
    for (;;)
        mini_tree_system_loop();
#elif defined(CONFIG_OSAL_FREERTOS)
    vTaskStartScheduler();
#elif defined(CONFIG_OSAL_RTTHREAD)
    rt_system_scheduler_start();
#endif
    return 0;
}
```

### 6.2 C++ (`system_init.hpp`)

```cpp
#include "config.h"            // CONFIG_OSAL_* / CONFIG_XTASK_PREEMPT macros required by the headers below
#include "system_init.hpp"

mini_tree::system_pre_os_init();
/* optional: static init of business services (SystemCmd::get_instance().register_cmd(…) etc.) */
mini_tree::system_start_tasks();   /* probe + framework tasks */
/* optional: create business tasks via osal_task_create */

system_init_complete();
// then start the scheduler (vTaskStartScheduler / rt_system_scheduler_start / mini_tree_system_loop)
```

> Under bare-metal (`CONFIG_OSAL_NULL`), the C `osal_task_create` **always returns `OSAL_ERR_NOTSUPP`**:
> C++ projects should use the C++ overload in `osal_null.h` (`CONFIG_OSAL_NULL_TASK_CPP`, on by default;
> `period` is the task period in ms, `param1` is a caller-provided static `x_task*` TCB);
> C projects call `xscheduler_task_create` directly (see `time_slice/task/xtask.h`). No such limit on OS backends.
>
> **Preemptive note (`CONFIG_XTASK_PREEMPT=y`)**: the cooperative C++ overload is closed entirely via `#ifndef CONFIG_XTASK_PREEMPT`; C++ projects must also fall back to the native `xscheduler_task_create` API; the scheduler implementation switches to `xtask_preempt.c` (experimental, unfinished — may fail to compile when enabled).

Phase meanings are in [architecture.md §3](architecture.md#3-startup-sequence-two-phase-ignition).

---

## 7. IDE (clangd)

1. Open the **mini_tree repository root** in your editor (not just a subfolder).
2. Make sure the root `compile_flags.txt` exists, and **delete** any `compile_flags.txt` inside subdirectories.
3. ETL headers: already under `lib/etl`; clangd can use the root `compile_flags.txt` (which contains `-Ilib/etl/include`).
4. If you need `compile_commands.json`, generate it by running `python3 tools/gen_compile_db.py` at the mini_tree root (covers `.c/.cpp` sources and `.h/.hpp` header entries; a parent-project configure will not clobber it).
5. Command palette: `Clangd: Restart language server`.

Without a real build, placeholder headers in `ide/stubs/` (e.g. `config.h`, `board_nodes.h`) remove the red squiggles. Brick strategy: [ecosystem.md](ecosystem.md).

### 7.1 OS Choice (Linux / Windows)

This repo is a **CMake + clangd** based, cross-platform architecture. **At the compiler level Windows and Linux are identical** (same ARM GCC / Clang, same CMake flow), so development works on both:

| Aspect | Notes |
| :--- | :--- |
| **Linux (recommended)** | If you are already comfortable with this toolchain, **writing MCU code directly on Linux is faster and easier to manage** — the CMake / Python / header-generation flow runs quicker and cleaner on Linux, with tidier permissions and paths, and less friction in daily builds and dependency management. It also **prepares you for moving into Linux development** (servers, CI, and cross-compilation mostly live on Linux). |
| **Windows (equally supported)** | Windows works fully too — the compiler is no different and the project layout is identical; clangd / Keil Studio (Keil 6) also run this repo's CMake flow on Windows. Only the scripting and permission details are a bit less smooth than on Linux. |

> Bottom line: **prefer Linux, but Windows is fine**. Artifacts are identical on both; just pick whichever is convenient. Team members who want to practice Linux can switch over directly without affecting deliverables.

---

## 8. Acceptance Checklist

- [ ] `config.h` is generated and OSAL/SYSTEM macros match expectations
- [ ] dtc-lite produces `board_nodes.h` with `DEV_ID_COUNT` ≥ 1 (a real board should be far larger than the placeholder)
- [ ] after linking, HALs like GPIO/UART are platform implementations (not always `VFS_ERR_NOTSUPP`)
- [ ] `board_driver_probe_all` finishes without unexpected FATAL
- [ ] business tasks or the bare-metal loop run stably once interrupts are enabled

---

## Related Documents

- [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md)
- [osal_switching.md](osal_switching.md) · [faq.md](faq.md) · [ecosystem.md](ecosystem.md)
- [tools_guide.md](../tools_guide.md)
