# ESP-IDF Porting Guide (CMake Integration)

> Unlike the generic CMake at the repo root (`add_subdirectory` + `add_library(mini_tree STATIC)`), ESP32 uses the **IDF component** path. Reference implementation: `platform/Espressif/esp32s3/` (root `CMakeLists.txt` `EXTRA_COMPONENT_DIRS` registration preference + `components/board_${IDF_TARGET}` + `components/hal_esp32s3` + optional `components/driver_ws2812`).

| Item | Description |
| :--- | :--- |
| **Audience** | Anyone wiring this shelf into an ESP-IDF project |
| **Prereq.** | [getting_started.md](getting_started.md) (generic CMake) |
| **Related** | [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [design_decisions.md](design_decisions.md) |

---

## Table of Contents

1. [Overview: Generic CMake vs ESP-IDF](#1-overview-generic-cmake-vs-esp-idf)
2. [Project Layout](#2-project-layout)
3. [Auto-Derivation Contract (replaces board_port.cmake)](#3-auto-derivation-contract-replaces-boardportcmake)
4. [CMake Essentials Inside the Component](#4-cmake-essentials-inside-the-component)
5. [Dual-Track Kconfig and Platform Declaration](#5-dual-track-kconfig-and-platform-declaration)
6. [DTS and Generated Artifacts](#6-dts-and-generated-artifacts)
7. [HAL: ESP Shutdown + Board Strong Implementations](#7-halesp-shutdown--board-strong-implementations)
8. [Product Driver Layout (GLOB)](#8-product-driver-layout-glob)
9. [Dependencies and ETL](#9-dependencies-and-etl)
10. [Syncing with the shelf Repository](#10-syncing-with-the-shelf-repository)
11. [Acceptance Checklist](#11-acceptance-checklist)

---

## 1. Overview: Generic CMake vs ESP-IDF

| Item | Generic (ST / bare-metal) | ESP-IDF |
| :--- | :--- | :--- |
| Wiring | `add_subdirectory(mini_tree)` | Registered via `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt` (official component-discovery entry) with a preference: `managed_components/mini_tree` (vendored copy, same dir as cjson/led_strip) if present, otherwise fall back to the shelf absolute path (dev mainline, edits take effect immediately); on `ESP_PLATFORM` goes through `cmake/esp_idf.cmake` and `return()`s |
| Target | `add_library(mini_tree STATIC)` | `idf_component_register(...)` |
| Artifact refs | `target_* (mini_tree …)` | `target_* (${COMPONENT_LIB} …)` |
| Paths | `CMAKE_CURRENT_LIST_DIR` | **Must** use `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR` (IDF two-phase; don't rely on `SOURCE_DIR` at requirements stage); `file(GLOB …)` **must not** add `CONFIGURE_DEPENDS` (requirements is script mode) |
| HAL impl. | Board `target_sources(executable …)` into final elf; strong beats weak naturally | Separate board component `hal_<soc>` (`WHOLE_ARCHIVE`); shelf stubs compile empty on ESP (see [§7](#7-halesp-shutdown--board-strong-implementations)) |
| Kconfig | Only `.config` + `genconfig.py` | Component `Kconfig` enters `idf.py menuconfig`; `config.h` is usually a placeholder on ESP, real values come from `sdkconfig.h` |
| Linker script | Board `-T` / `error_symbols.ld` | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

---

## 2. Project Layout

Reference: `platform/Espressif/esp32s3/` (in the [device-platform](https://github.com/H-000-H/device-platform) repository)

```cmake
# Root CMakeLists.txt — just two lines; everything else is assembled by components/
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_board)
```

```cmake
# Root CMakeLists.txt — mini_tree registration preference (EXTRA_COMPONENT_DIRS
# is the official component-discovery entry):
#   1) managed_components/mini_tree exists → use the vendored copy (same dir as
#      cjson/led_strip; can be a git clone / release snapshot; note that
#      `idf.py fullclean` deletes the whole managed_components/)
#   2) otherwise → fall back to the shelf absolute path (dev mainline, edits take
#      effect immediately)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/managed_components/mini_tree/CMakeLists.txt")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/managed_components/mini_tree")
else()
    list(APPEND EXTRA_COMPONENT_DIRS "/path/to/mini_tree")
endif()
```

| Component / file | Path | Description |
| :--- | :--- | :--- |
| `mini_tree` | root `CMakeLists.txt` `EXTRA_COMPONENT_DIRS` (managed copy preferred, shelf fallback) | **Pure-architecture** middleware + `drivers/*`; no SoC hard-coding |
| `board_<soc>` | `components/board_<soc>/` | Board `dts/` + `dtsi/` **real component** (empty `idf_component_register`); directory name must match `board_${IDF_TARGET}` — mini_tree auto-discovers it by that convention (see [§3](#3-auto-derivation-contract-replaces-boardportcmake)) |
| `hal_<soc>` | `components/hal_<soc>/` | HAL strong-implementation component (`WHOLE_ARCHIVE`, see [§7](#7-halesp-shutdown--board-strong-implementations)) |
| `driver_ws2812` | `components/driver_ws2812/` | **Optional**; the only product driver allowed to use vendor RMT/`led_strip` |
| `app` | `components/app/` | `REQUIRES mini_tree` (+ optional `driver_ws2812`) |

Dependency chain: `app` → `mini_tree` (+ optional `driver_ws2812`).

**One mini_tree, many MCUs**: the same `mini_tree` can be referenced by multiple board projects (`EXTRA_COMPONENT_DIRS` preference registration); each project ships its own `board_<soc>` + `hal_<soc>`. Board injection needs **no injection file** — see [§3](#3-auto-derivation-contract-replaces-boardportcmake).

**Do not** set `EXTRA_COMPONENT_DIRS` to the legacy `components/driver/` (deprecated).

---

## 3. Auto-Derivation Contract (replaces board_port.cmake)

Board injection no longer depends on any "injection file" (`board_port.cmake` / `MINI_TREE_BOARD_PORT` removed). `cmake/esp_idf.cmake` derives **everything at configure time**:

| Derived item | Source | Notes |
| :--- | :--- | :--- |
| Chip dtc `-I/-D` | `idf_build_get_property(IDF_TARGET)` | Builds `components/esp_hal_gpio/<chip>/include`, `components/soc/<chip>/include` automatically (`IS_DIRECTORY`-guarded, skipped when the IDF layout changes); `-DCONFIG_IDF_TARGET_<CHIP>=1` / `-DIDF_TARGET_<CHIP>=1` for `#ifdef` branches in dtsi |
| dt-bindings base `-I` | fixed `-I mini_tree/board` | Provided by esp_idf.cmake; do not put the dtsi dir here |
| Board DTS | convention `components/board_${IDF_TARGET}` | `dts/board.dts` + `dtsi/` inside the component dir; not found → placeholder board (builds, **no board nodes**); **found but misconfigured → configure-time error** (fail-loud, no silent placeholder) |
| Out-of-tree product drivers | convention `components/*/src` | dtc-lite auto-scans `DRIVER_REGISTER` into the probe table; `src/` dirs without the macro (e.g. `hal_*`) are merely parsed once more, harmless |
| Feature switches | IDF `CONFIG_*` (sdkconfig) | `CONFIG_SYSTEM` / `CONFIG_EVENT_BUS` / `CONFIG_SYSTEM_CMD` / `CONFIG_USB` all come from `Kconfig.mini_tree`, visible in menuconfig, pinnable in `sdkconfig.defaults` (the ESP path no longer reads `.config`) |

Switching MCU = create a `board_<new-chip>` component + `hal_<new-chip>` component; mini_tree itself stays untouched.

**Escape hatch**: `MINI_TREE_DTC_EXTRA_ARGS` still accepts non-standard chip header paths (not needed for normal projects).

### 3.1 Board Component Layout (practical template)

```text
components/board_esp32s3/
├── CMakeLists.txt        # empty idf_component_register() — a pure-data component so IDF discovers it
├── dts/board.dts         # master dts (dtc-lite entry, conventional path)
└── dtsi/                 # board/SoC dtsi fragments (conventional dir; changes re-run dts)
```

```cmake
# components/board_esp32s3/CMakeLists.txt
idf_component_register()
```

**Writing checklist** (go through item by item when creating a new ESP board project):

1. Directory name must be `board_${IDF_TARGET}` (e.g. `board_esp32s3`, `board_esp32c6`), otherwise mini_tree can't find it → placeholder board (builds, **no board nodes**).
2. Layout must be `dts/board.dts` + `dtsi/`; a found component with the wrong layout fails at **configure time** (deliberate, to avoid silent degradation).
3. Chip headers/macros are **not written by you** — derived from `IDF_TARGET`; any `#ifdef CONFIG_IDF_TARGET_<CHIP>` in dtsi must be **ALL CAPS** to match the derived macro.
4. Out-of-tree drivers live in `components/*/src`; a `DRIVER_REGISTER` there enters the probe table automatically.
5. Don't forget the platform declaration `CONFIG_PLATFORM_ESP32=y` in `sdkconfig.defaults` (see [§5](#5-dual-track-kconfig-and-platform-declaration)).
6. HAL implementations go into the `hal_<soc>` component (see [§7](#7-halesp-shutdown--board-strong-implementations)), decoupled from the board component.

---

## 4. CMake Essentials Inside the Component

On `ESP_PLATFORM`, the root `CMakeLists.txt` only `include(cmake/esp_idf.cmake)` (chip/board/out-of-tree drivers are all auto-derived inside esp_idf.cmake):

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

**HAL stub shutdown happens at source level** (`#if defined(ESP_PLATFORM)`), so `HAL_SRCS` keeps the full list (same as generic CMake); `esp_idf.cmake` does not trim source lists per chip. Details in [§7](#7-halesp-shutdown--board-strong-implementations).

---

## 5. Dual-Track Kconfig and Platform Declaration

| Track | Role |
| :--- | :--- |
| Component root `Kconfig` | Shows up in `idf.py menuconfig` → Component config → mini_tree |
| Component `.config` | Used for non-ESP / source selection; **don't overwrite** board `.config` when syncing to shelf |

On ESP builds the generated `config.h` is usually a placeholder; business `CONFIG_*` come from `sdkconfig.h`.

`cmake/esp_idf.cmake` reads the IDF `CONFIG_*` CMake variables directly (no more `.config` soft-coding on ESP): `CONFIG_USB` controls whether `bus/usb`, `vfs/usb`, `hal/usb` are compiled (board must supply usb_tusb_port glue and REQUIRE `esp_tinyusb` itself). Pin the switches in `sdkconfig.defaults` (see [§3](#3-auto-derivation-contract-replaces-boardportcmake)).

### Platform declaration: `CONFIG_PLATFORM_ESP32`

The Platform choice adds `PLATFORM_ESP32` (→ `CONFIG_PLATFORM_ESP32`). **ESP-IDF projects MUST select it explicitly** (in `sdkconfig.defaults`: `CONFIG_PLATFORM_ESP32=y` / `# CONFIG_PLATFORM_ARM_CM4F is not set`):

- Compilation is managed by IDF itself (`ESP_PLATFORM` macro + `cmake/esp_idf.cmake`); this option does **not** directly drive the build;
- It declares "this project is ESP", pairing with the HAL shutdown mechanism ([§7](#7-halesp-shutdown--board-strong-implementations));
- Non-ESP bare-metal builds must keep the ARM / RISC-V options (default `PLATFORM_ARM_CM4F` only matters for non-ESP).

---

## 6. DTS and Generated Artifacts

| Item | ESP reference approach |
| :--- | :--- |
| `BOARD_DTS` | Auto-discovered: `components/board_${IDF_TARGET}/dts/board.dts` (see [§3](#3-auto-derivation-contract-replaces-boardportcmake)); when no board component is found the middleware default is a placeholder (`mini-tree,placeholder` — builds, but **no board nodes**) |
| Generated dirs | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree` |
| dtc-lite | Scans vfs/bus + **all** `drivers/*/src` + project `components/*/src`; `-I mini_tree/board` resolves `dt-bindings/`; chip headers/macros derived from `IDF_TARGET` |

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
3. **Name the board component `board_${IDF_TARGET}` with the conventional layout** (`dts/board.dts` + `dtsi/`) — everything else (chip headers/macros, out-of-tree driver scan) is auto-derived, see [§3](#3-auto-derivation-contract-replaces-boardportcmake).

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
| Root `CMakeLists.txt` (non-ESP) | same | same | same |
| `board/CMakeLists.txt` (standalone board lib) | — | — | `../drivers/*/src` |
| `compile_flags.txt` | — | all `-Idrivers/*/include` (and header-bearing `src`) | — |

Out-of-tree exception: `components/driver_ws2812/src` is auto-scanned into dtc via the `components/*/src` convention (see [§3](#3-auto-derivation-contract-replaces-boardportcmake)); its sources still compile inside the `driver_ws2812` component (`WHOLE_ARCHIVE`).

The legacy `drivers/flash` (`winbond,w25q64`) is removed; use `drivers/w25qxx` (`winbond,w25qxx`).

---

## 9. Dependencies and ETL

Current `lib/`: only **FreeRTOS (v11.3.0), RT-Thread (v5.3.0), ETL** are vendored in-repo; everything else goes through FetchContent.

- ESP drivers: declared in `idf_component_register(... REQUIRES …)`.
- Configure-time FetchContent (root CMake `include(cmake/*.cmake)`, local-or-fetch): TinyUSB, lwIP, cJSON. On ESP prefer IDF components / Component Manager (`esp_tinyusb`, `managed_components`).
- Link-time FetchContent (on-demand `mini_tree_link_*`, local-or-fetch on link): LVGL, u8g2, littlefs, FatFs, SFUD, Mbed TLS, coreMQTT, coreHTTP, nanopb, miniz, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB.
- ETL: vendored (`lib/etl`, include + cmake only), nothing to fetch; **don't** stuff `managed_components` into `EXTRA_COMPONENT_DIRS`.
- FreeRTOS: shipped by IDF; `CONFIG_OSAL_FREERTOS` talks to the IDF kernel — do not embed `lib/freeRTOS` again.

---

## 10. Relationship with the shelf Repository

Board projects reference mini_tree via the `EXTRA_COMPONENT_DIRS` **registration preference** in the root `CMakeLists.txt`: `managed_components/mini_tree` (vendored copy) preferred, otherwise the shelf absolute path (dev mainline — edits to the shelf take effect immediately, no syncing, and there is no `components/mini_tree` copy anymore).

- The shelf holds only the generic layer (middleware / HAL stubs / drivers / cmake / docs); board-level stuff (`board_<soc>` / `hal_<soc>` / `driver_*`) lives in each board project's repository.
- **Vendored mode** (drop a copy into `managed_components/mini_tree`): same dir and same "third-party component" look as cjson/led_strip; but it is a **manual copy** — it does not auto-sync shelf changes, and `idf.py fullclean` deletes the whole `managed_components/` (the copy must be re-placed). Switching the preference takes effect after `idf.py reconfigure`.
- Moving machines / directory trees: change the fallback absolute path in the root `CMakeLists.txt` (this project style uses absolute paths).

Do not let the ESP `idf_component_register` overwrite the non-ESP root `CMakeLists.txt` wholesale (the two entries coexist via the `if(ESP_PLATFORM)` branch).

---

## 11. Acceptance Checklist

- [ ] No stale `EXTRA_COMPONENT_DIRS=…/components/driver`
- [ ] `mini_tree` registered via `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt` (managed copy preferred, shelf fallback); no `components/mini_tree` copy remains
- [ ] `components/board_${IDF_TARGET}/` exists with `CMakeLists.txt` + `dts/board.dts` + `dtsi/`
- [ ] `idf.py build` generates `board_probe.c` / `dt_config_gen.h`; all product-driver `DRIVER_REGISTER`s matched (including out-of-tree `components/*/src`)
- [ ] `drivers/*/src/*.c` GLOB covers all product drivers, no per-file lists
- [ ] `app` `REQUIRES mini_tree` (+ `driver_ws2812` when needed)
- [ ] `drivers/flash` absent; Flash node is `winbond,w25qxx`
- [ ] OSAL matches IDF FreeRTOS; no dual kernels
- [ ] All `hal/*.c` stubs carry `#if defined(ESP_PLATFORM)` shutdown (stubs not compiled on ESP)
- [ ] Board `hal_<soc>` component has `WHOLE_ARCHIVE` and implements every `hal_*` the ESP build references
- [ ] `sdkconfig.defaults` contains `CONFIG_PLATFORM_ESP32=y` + pinned feature switches (`CONFIG_SYSTEM=y` / `CONFIG_USB=y` / `# CONFIG_EVENT_BUS is not set` / `# CONFIG_SYSTEM_CMD is not set`)
- [ ] The dtc command line contains the derived chip `-I/-D` and `-I mini_tree/board` (see `build/build.ninja`)

---

## Related Docs

- [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md) (ESP-IDF VFS mental mapping)
