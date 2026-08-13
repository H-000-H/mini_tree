# ESP-IDF Porting Guide (CMake Integration)

> This branch is a **pure ESP-IDF component**: wired in as `components/mini_tree`, going through `cmake/esp_idf.cmake` on `ESP_PLATFORM`. Reference implementation: `platform/Espressif/esp32s3/` (`components/mini_tree` + `components/board_port.cmake` + `components/hal_esp32s3` + optional `components/driver_ws2812`).

| Item | Description |
| :--- | :--- |
| **Audience** | Anyone wiring this component into an ESP-IDF project |
| **Prereq.** | [getting_started.md](getting_started.md) (ESP-IDF integration) |
| **Related** | [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [design_decisions.md](design_decisions.md) |

---

## Table of Contents

1. [Overview: ESP-IDF Component Integration](#1-overview-esp-idf-component-integration)
2. [Project Layout](#2-project-layout)
3. [board_port.cmake Injection Contract](#3-boardportcmake-injection-contract)
4. [CMake Essentials Inside the Component](#4-cmake-essentials-inside-the-component)
5. [Dual-Track Kconfig and Platform Declaration](#5-dual-track-kconfig-and-platform-declaration)
6. [DTS and Generated Artifacts](#6-dts-and-generated-artifacts)
7. [HAL: ESP Shutdown + Board Strong Implementations](#7-halesp-shutdown--board-strong-implementations)
8. [Product Driver Layout (GLOB)](#8-product-driver-layout-glob)
9. [Dependencies and ETL](#9-dependencies-and-etl)
10. [Syncing with the shelf Repository](#10-syncing-with-the-shelf-repository)
11. [Acceptance Checklist](#11-acceptance-checklist)

---

## 1. Overview: ESP-IDF Component Integration

| Item | ESP-IDF |
| :--- | :--- |
| Wiring | `components/mini_tree` auto-scanned; on `ESP_PLATFORM` goes through `cmake/esp_idf.cmake` |
| Target | `idf_component_register(...)` |
| Artifact refs | `target_* (${COMPONENT_LIB} …)` |
| Paths | **Must** use `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR` (IDF two-phase; don't rely on `SOURCE_DIR` at requirements stage); `file(GLOB …)` **must not** add `CONFIGURE_DEPENDS` (requirements is script mode) |
| HAL impl. | Separate board component `hal_<soc>` (`WHOLE_ARCHIVE`); component stubs compile empty on ESP (see [§7](#7-halesp-shutdown--board-strong-implementations)) |
| Kconfig | `Kconfig.projbuild` enters `idf.py menuconfig`; `config.h` just forwards `sdkconfig.h`, real values come from `sdkconfig.h` |
| Linker script | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

---

## 2. Project Layout

Reference: `platform/Espressif/esp32s3/` (in the [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) repository)

```cmake
# Root CMakeLists.txt — just two lines; everything else is assembled by components/
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_board)
```

| Component / file | Path | Description |
| :--- | :--- | :--- |
| `mini_tree` | `components/mini_tree/` | **Pure-architecture** middleware + `drivers/*`; no SoC hard-coding |
| `board_port.cmake` | `components/board_port.cmake` | Board injection: `BOARD_DTS` / `BOARD_DTSI_DIR` / chip `-I/-D` / out-of-tree scan (see [§3](#3-boardportcmake-injection-contract)) |
| `board_<soc>` | `components/board_<soc>/` | Board `dts/` + `dtsi/` (**not** an IDF component, data only) |
| `hal_<soc>` | `components/hal_<soc>/` | HAL strong-implementation component (`WHOLE_ARCHIVE`, see [§7](#7-halesp-shutdown--board-strong-implementations)) |
| `driver_ws2812` | `components/driver_ws2812/` | **Optional**; the only product driver allowed to use vendor RMT/`led_strip` |
| `app` | `components/app/` | `REQUIRES mini_tree` (+ optional `driver_ws2812`) |

Dependency chain: `app` → `mini_tree` (+ optional `driver_ws2812`).

**One mini_tree, many MCUs**: the same `mini_tree` (symlink / submodule / copy) can hang under multiple board projects; each project ships its own `board_port.cmake` + `board_*` + `hal_*`. Alternatively set `MINI_TREE_BOARD_PORT` to point at any injection file.

**Do not** set `EXTRA_COMPONENT_DIRS` to the legacy `components/driver/` (deprecated).

---

## 3. board_port.cmake Injection Contract

The file answers "who is this project": where the board DTS is, what chip macros are, where out-of-tree drivers live. **All variables are consumed on the mini_tree side** (consumer annotated):

| Variable | Consumer | Purpose |
| :--- | :--- | :--- |
| `BOARD_DTS` | esp_idf.cmake → dtc-lite | Board device-tree master file; if unset, a placeholder board is used (builds, **no board nodes**) |
| `BOARD_DTSI_DIR` | esp_idf.cmake | dtsi fragment dir (`file(GLOB .../*.dtsi)` as DEPENDS — change re-runs dts) |
| `MINI_TREE_DTC_EXTRA_ARGS` | mini_tree/CMakeLists.txt (ESP branch) → DTC_LITE_ARGS | Chip-specific `-I` (IDF headers, guarded by `IS_DIRECTORY`) + target macros (`-DCONFIG_IDF_TARGET_<chip>=1`, makes `#ifdef` in dtsi work). The base `-I mini_tree/board` (dt-bindings search) is provided by mini_tree by default; here you **append only** chip items |
| `MINI_TREE_DTC_EXTRA_SCAN_DIRS` | esp_idf.cmake → `_DTC_SCAN_DIRS` | Out-of-tree product driver dirs (dtc-lite scans for `DRIVER_REGISTER` to build the probe table) |
| `MINI_TREE_DTC_EXTRA_DEPENDS` | esp_idf.cmake → `_DTC_DEPENDS` | Changing these files re-runs the dts generation |

Switching MCU = edit this file (or point `MINI_TREE_BOARD_PORT` elsewhere); mini_tree itself stays untouched.

### 3.1 How to Write It (practical template)

Based on `platform/Espressif/esp32s3/components/board_port.cmake` (spots marked `<chip>` are **what you change when switching MCU**):

```cmake
# SPDX-License-Identifier: Apache-2.0
# Board injection — answers "who is this project". Included by mini_tree/CMakeLists.txt (ESP_PLATFORM).
# Switching MCU = edit this file; or don't — point MINI_TREE_BOARD_PORT at another injection file.

# ── ① Path bases (fixed; no change when switching MCU) ──────────────────
get_filename_component(_BOARD_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)   # components/
get_filename_component(_BOARD_DIR  "${_BOARD_ROOT}/board_<soc>" ABSOLUTE)  # ← your board dts dir

# ── ② Board device tree ─────────────────────────────────────────────────
set(BOARD_DTS      "${_BOARD_DIR}/dts/board.dts")   # master dts (dtc-lite entry)
set(BOARD_DTSI_DIR "${_BOARD_DIR}/dtsi")            # dtsi fragment dir
file(GLOB MINI_TREE_BOARD_DTSI "${BOARD_DTSI_DIR}/*.dtsi")   # change re-runs dts

# ── ③ Chip-specific dtc args ────────────────────────────────────────────
# Base "-I mini_tree/board" (dt-bindings search) is provided by mini_tree by default;
# only chip-specific items go here.
if(DEFINED ENV{IDF_PATH})
    foreach(_d
        "$ENV{IDF_PATH}/components/esp_hal_gpio/<chip>/include"   # ← your chip
        "$ENV{IDF_PATH}/components/soc/<chip>/include"            # ← your chip
    )
        if(IS_DIRECTORY "${_d}")        # IDF layout changes between versions; add only if present (skip is fine)
            list(APPEND MINI_TREE_DTC_EXTRA_ARGS "-I${_d}")
        endif()
    endforeach()
    # Target macros: enable the #ifdef CONFIG_IDF_TARGET_<CHIP> branches inside dtsi
    list(APPEND MINI_TREE_DTC_EXTRA_ARGS
        "-DCONFIG_IDF_TARGET_<CHIP>=1"   # ← your chip (ALL CAPS, must match the #ifdef in dtsi)
        "-DIDF_TARGET_<CHIP>=1")
endif()

# ── ④ Out-of-tree product drivers (omit both lines if none) ─────────────
file(GLOB _OUT_DRV_SRCS "${_BOARD_ROOT}/driver_xxx/src/*.c")     # ← your out-of-tree driver
set(MINI_TREE_DTC_EXTRA_SCAN_DIRS "${_BOARD_ROOT}/driver_xxx/src")   # dtc scans DRIVER_REGISTER
set(MINI_TREE_DTC_EXTRA_DEPENDS  ${MINI_TREE_BOARD_DTSI} ${_OUT_DRV_SRCS} "${BOARD_DTS}")
```

**Writing checklist** (go through item by item when creating a new ESP board project):

1. Section ① is fixed; change the `board_<soc>` dir name in ② (`board_esp32c6`, etc.).
2. Section ③ chip header paths: one `components/<driver>/<chip>/include` per line; the `IS_DIRECTORY` guard is **intentional** — IDF layouts differ between versions, so skip instead of hard-failing.
3. Section ③ target macros: `CONFIG_IDF_TARGET_<CHIP>` must be **ALL CAPS** and match the `#ifdef` spelling inside dtsi (e.g. `CONFIG_IDF_TARGET_ESP32S3`); `IDF_TARGET_<CHIP>` is an alternative spelling — mirror whichever the dtsi uses.
4. Section ④: with no out-of-tree drivers, **omit** `MINI_TREE_DTC_EXTRA_SCAN_DIRS` (esp_idf.cmake falls back to its default scan set).
5. Don't forget the platform declaration `CONFIG_PLATFORM_ESP32=y` in `sdkconfig.defaults` (see [§5](#5-dual-track-kconfig-and-platform-declaration)).
6. HAL implementations go into the `hal_<soc>` component (see [§7](#7-halesp-shutdown--board-strong-implementations)); **this file doesn't change** — board_port.cmake and the HAL component are decoupled.

---

## 4. CMake Essentials Inside the Component

On `ESP_PLATFORM`, the root `CMakeLists.txt` sets up dtc extensions then `include(cmake/esp_idf.cmake)`:

```cmake
idf_component_register(
    SRCS
        ${OSAL_SRCS} ${HAL_SRCS} ${BOARD_SRCS} ${CORE_SRCS}
        ${DRIVER_SRCS}   # includes GLOB: drivers/*/src/*.c + vfs/bus
        ${SYSTEM_SRCS} ${GEN_SRCS}
    INCLUDE_DIRS
        "${MINI_TREE_DIR}"
        "${MINI_TREE_DIR}/board/include"
        # … vfs/bus/hal …
        ${_PRODUCT_DRV_INC_DIRS}   # drivers/*/include
        ${_PRODUCT_DRV_SRC_DIRS}   # drivers/*/src (e.g. st7789 headers live in src)
        "${GENERATED_BOARD_DIR}"
        …
    REQUIRES
        freertos
        esp_driver_gpio esp_driver_spi esp_driver_uart
        esp_driver_i2c esp_driver_twai
)
```

> `SRCS` includes GLOBbed `drivers/*/src/*.c` plus vfs/bus; `INCLUDE_DIRS` includes product-driver include/src and generated dirs. After that, attach gen dependencies, `CONFIG_OSAL_*`, `error_symbols.ld` to **`${COMPONENT_LIB}`**.

**HAL stub shutdown happens at source level** (`#if defined(ESP_PLATFORM)`), so `HAL_SRCS` keeps the full list; `esp_idf.cmake` does not trim source lists per chip. Details in [§7](#7-halesp-shutdown--board-strong-implementations).

---

## 5. Dual-Track Kconfig and Platform Declaration

| Track | Role |
| :--- | :--- |
| Component root `Kconfig` | Shows up in `idf.py menuconfig` → Component config → mini_tree |
| `sdkconfig` | Generated by `idf.py menuconfig`; business `CONFIG_*` come from `sdkconfig.h` |

On ESP builds the generated `config.h` just forwards `sdkconfig.h`; business `CONFIG_*` come from `sdkconfig.h`.

`cmake/esp_idf.cmake` soft-codes from `sdkconfig.h`: if `CONFIG_USB` is not explicitly `# ... is not set`, `bus/usb`, `vfs/usb`, `hal/usb` are compiled (board must supply usb_tusb_port glue and REQUIRE `esp_tinyusb` itself); otherwise none are. If the ESP board has no glue yet, `sdkconfig` should keep `# CONFIG_USB is not set`.

### Platform declaration: `CONFIG_PLATFORM_ESP32`

The Platform keeps only `PLATFORM_ESP32` (→ `CONFIG_PLATFORM_ESP32`, on by default). The platform is fixed to ESP32; no need to switch it in `sdkconfig.defaults` (the old `CONFIG_PLATFORM_ARM_CM4F` is removed):

- Compilation is managed by IDF itself (`ESP_PLATFORM` macro + `cmake/esp_idf.cmake`); this option does **not** directly drive the build;
- It declares "this project is ESP", pairing with the HAL shutdown mechanism ([§7](#7-halesp-shutdown--board-strong-implementations));
- Only the `OSAL_NULL` (bare-metal fallback) scheduler tick is platform-agnostic; there are no ARM / RISC-V platform options.

---

## 6. DTS and Generated Artifacts

| Item | ESP reference approach |
| :--- | :--- |
| `BOARD_DTS` | Injected by `board_port.cmake` (e.g. `board_esp32s3/dts/board.dts`); middleware default is a placeholder (`mini-tree,placeholder` — builds, but **no board nodes**) |
| Generated dirs | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree` |
| dtc-lite | Scans vfs/bus + **all** `drivers/*/src` + `MINI_TREE_DTC_EXTRA_SCAN_DIRS`; `-I mini_tree/board` resolves `dt-bindings/`; chip headers/macros via `MINI_TREE_DTC_EXTRA_ARGS` |

**Do not** put the board `dtsi/` into the dtc `-I`: they get treated as vendor headers, cpp-extracted for macros instead of inlined, leaving only the root node (`devices: 1`). dtsi resolves automatically via the `board_dir` of `BOARD_DTS`.

**Do not** hard-code a specific `IDF_TARGET`'s `soc/<chip>/include` in `cmake/esp_idf.cmake`.

Public-header rule unchanged: **product drivers must not** `#include` vendor SDKs (`driver_ws2812` excepted).

---

## 7. HAL: ESP Shutdown + Board Strong Implementations

### Mechanism (already in place on the shelf side — no porting work)

Every `hal/<name>/hal_<name>.c` weak stub file starts with:

```c
#if defined(ESP_PLATFORM)
/* ESP-IDF builds: this file compiles empty — hal_* provided by the board
 * component as strong symbols; missing ones are link errors, no silent
 * -ENOSYS. Non-ESP builds keep the weak stub fallback. */
#else
COMPAT_WEAK int hal_xxx(/*...*/) { return VFS_ERR_NOTSUPP; }
/* ... remaining stub functions ... */
#endif /* ESP_PLATFORM */
```

- **ESP builds**: stub files compile empty → bus/vfs references to `hal_*` are all **strong references** → a missing board implementation is a **direct link error** (`undefined reference to hal_xxx`) listing the missing symbols
- **Non-ESP builds**: behavior unchanged; weak stubs fall back as before

### Why weak/strong overriding doesn't work here

ESP-IDF is a componentized build: all code lives in static libraries, extracted **on demand** at link time. Once mini_tree's weak stubs satisfy the references from `spi_bus.c` etc., the linker will **not** extract strong overrides from the board component's library — zero objects extracted, every HAL silently lands on the stub, probes return `-ENOSYS` (`VFS_ERR_NOTSUPP`), a symptom that is very hard to track down.

Bare-metal projects (ST/CH32) don't have this problem: board strong implementations go straight into the final executable via `target_sources(executable PRIVATE ...)` (forced full link, strong naturally beats weak). Componentized IDF takes away that free target, so stubs must be shut down at the source level.

### Three mandatory things for the board component (e.g. reference `hal_esp32s3`)

1. **Implement** every `hal_*` referenced by the ESP build. Current reference set: 7 peripherals (`gpio/spi/uart/i2c/can/tim/adc`) + 5 system placeholders (`iwdg/storage/flash/usb/platform_safety`, may return failure for now). Note `hal_cpu_secondary_startup` is only referenced when `CONFIG_CPU_CORES > 1` — implement it before enabling dual-core AMP.
2. **`idf_component_register(WHOLE_ARCHIVE ...)`**: mini_tree's objects are extracted in the **second pass** of the link line (cyclic-dependency reordering), so its `hal_*` undefineds appear after the board library's normal scan point; WHOLE_ARCHIVE makes IDF append the board library at the **end** of the link line for one more pass. Without it the symptom is `undefined reference to hal_spi_*`.
3. **`board_port.cmake` injection** of project-specific info (board DTS path, IDF chip headers/target macros, out-of-tree driver dirs) — see [§3](#3-boardportcmake-injection-contract).

> USB HAL quirk: `hal/usb/hal_usb.h` does `#pragma GCC poison` on the `hal_usb_*` symbols for callers that lack an implementation; implementers must `#define HAL_USB_IMPL` before including it.

---

## 8. Product Driver Layout (GLOB)

Unified layout (37 product drivers):

```text
mini_tree/drivers/<chip>/
├── include/     # public headers (ioctl / regs / bridge)
└── src/         # *.c; private .h allowed
```

| Build entry | Sources | Include | dtc-lite scan |
| :--- | :--- | :--- | :--- |
| `cmake/esp_idf.cmake` | `drivers/*/src/*.c` | `drivers/*/include` + `drivers/*/src` | `_PRODUCT_DRV_SRC_DIRS` |
| `compile_flags.txt` | — | all `-Idrivers/*/include` (and header-bearing `src`) | — |

Out-of-tree exception: `components/driver_ws2812/src` is only scanned into dtc via `MINI_TREE_DTC_EXTRA_SCAN_DIRS` / `EXTRA_DEPENDS`; its sources still compile inside the `driver_ws2812` component (`WHOLE_ARCHIVE`).

The legacy `drivers/flash` (`winbond,w25q64`) is removed; use `drivers/w25qxx` (`winbond,w25qxx`).

---

## 9. Dependencies and ETL

Current `lib/`: only **ETL** is vendored in-repo; other blocks (TinyUSB, cJSON, LVGL, etc.) come through the **ESP-IDF component system** (`idf_component.yml` / registry), no more FetchContent / `mini_tree_link_*`.

- ESP drivers: declared in `idf_component_register(... REQUIRES …)`.
- Third-party components: declared in `idf_component.yml` (e.g. `esp_tinyusb`, `lvgl`, `esp_lvgl_port`), managed by the IDF Component Manager.
- ETL: vendored (`lib/etl`, include only), nothing to fetch; **don't** stuff `managed_components` into `EXTRA_COMPONENT_DIRS`.
- FreeRTOS: shipped by IDF; `CONFIG_OSAL_FREERTOS` talks to the IDF kernel — no vendored copy.

---

## 10. Syncing with the shelf Repository

This branch (ESP component) and the main mini_tree repo's generic layer should stay aligned.

Suggested flow:

1. Validate product drivers / DTS / CMake in the **ESP board project** (`idf.py build`).
2. **Sync content to the main repo** (`rsync` add/overwrite; **don't** `--delete` main-repo-only content).
3. Reverse: when copying main-repo generic-layer changes back to the board project, keep board-level `hal_esp32s3`, root project `CMakeLists.txt`, `driver_ws2812`, `board_port.cmake`.

This branch is a pure ESP component; its root `CMakeLists.txt` goes through `cmake/esp_idf.cmake`, with no dual-path entry.

---

## 11. Acceptance Checklist

- [ ] No stale `EXTRA_COMPONENT_DIRS=…/components/driver`
- [ ] `idf.py build` generates `board_probe.c` / `dt_config_gen.h`; all product-driver `DRIVER_REGISTER`s matched
- [ ] `drivers/*/src/*.c` GLOB covers all 37 product drivers, no per-file lists
- [ ] `app` `REQUIRES mini_tree` (+ `driver_ws2812` when needed)
- [ ] `drivers/flash` absent; Flash node is `winbond,w25qxx`
- [ ] OSAL matches IDF FreeRTOS; no dual kernels
- [ ] All `hal/*.c` stubs carry `#if defined(ESP_PLATFORM)` shutdown (stubs not compiled on ESP)
- [ ] Board `hal_<soc>` component has `WHOLE_ARCHIVE` and implements every `hal_*` the ESP build references
- [ ] `CONFIG_PLATFORM_ESP32` is on by default (platform fixed to ESP, no explicit declaration needed)

---

## Related Docs

- [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md) (ESP-IDF VFS mental mapping)
