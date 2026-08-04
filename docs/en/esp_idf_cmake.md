# ESP-IDF CMake Integration

> Unlike the generic CMake at the repo root (`add_subdirectory` + `add_library(mini_tree STATIC)`), ESP32 uses the **IDF component** path. Reference implementation: `platform/Espressif/esp32s3/` (`components/mini_tree` + optional `components/driver_ws2812`).

| Item | Description |
| :--- | :--- |
| **Audience** | Anyone wiring this shelf into an ESP-IDF project |
| **Prereq.** | [getting_started.md](getting_started.md) (generic CMake) |
| **Related** | [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md) · [design_decisions.md](design_decisions.md) |

---

## Table of Contents

1. [Differences from Generic CMake](#1-differences-from-generic-cmake)
2. [Wiring the Component into a Project](#2-wiring-the-component-into-a-project)
3. [CMake Essentials Inside the Component](#3-cmake-essentials-inside-the-component)
4. [Product Driver Layout (GLOB)](#4-product-driver-layout-glob)
5. [Dual-Track Kconfig](#5-dual-track-kconfig)
6. [DTS / Generated Artifacts / HAL](#6-dts-generated-artifacts-hal)
7. [Dependencies and ETL](#7-dependencies-and-etl)
8. [Syncing with the ESP Board Project](#8-syncing-with-the-esp-board-project)
9. [Acceptance Checklist](#9-acceptance-checklist)

---

## 1. Differences from Generic CMake

| Item | Generic (ST / bare-metal) | ESP-IDF |
| :--- | :--- | :--- |
| Entry | `add_subdirectory(mini_tree)` | `components/mini_tree` auto-scanned; on `ESP_PLATFORM` it includes `cmake/esp_idf.cmake` and `return()`s |
| Target | `add_library(mini_tree STATIC)` | `idf_component_register(...)` |
| Generated refs | `target_* (mini_tree …)` | `target_* (${COMPONENT_LIB} …)` |
| Paths | `CMAKE_CURRENT_LIST_DIR` | **Must** use `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR` (IDF is two-phase; don't rely on `SOURCE_DIR` during requirements); **don't** add `CONFIGURE_DEPENDS` to `file(GLOB …)` (requirements runs in script mode) |
| Vendor headers | `VENDOR_INC_DIRS` / `VENDOR_DEFINES` for dtc | Usually **unneeded**; board HAL lives in `hal_esp32s3` and `REQUIRES` ESP drivers |
| HAL | Weak in the repo; board strong symbols linked separately | Board `hal_*_esp32*.c` strong symbols |
| Kconfig | `.config` + `genconfig.py` only | Component `Kconfig` feeds `idf.py menuconfig`; under ESP the generated `config.h` is usually an empty shell — the real values come from `sdkconfig.h` |
| Linker script | Board `-T` / `error_symbols.ld` | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

---

## 2. Wiring the Component into a Project

Reference: `platform/Espressif/esp32s3/CMakeLists.txt` in the [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) repo

```cmake
# components/mini_tree + hal_* + optional driver_ws2812 auto-scanned
# board injects via components/board_port.cmake (DTS / chip dtc -I/-D)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_board)
```

> Above: auto-scans `components/mini_tree` + `hal_*` + optional `driver_ws2812`; the board injects DTS / chip dtc `-I/-D` via `components/board_port.cmake`.

| Component / file | Path | Description |
| :--- | :--- | :--- |
| `mini_tree` | `components/mini_tree/` | **Pure-architecture** middleware + `drivers/*`; no SoC hard-coding |
| `board_port.cmake` | `components/board_port.cmake` | Board injection: `BOARD_DTS` / `BOARD_DTSI_DIR` / chip `-I/-D` / out-of-tree scan |
| `board_<soc>` | `components/board_<soc>/` | Board `dts/` + `dtsi/` (**not** an IDF component, data only) |
| `hal_<soc>` | `components/hal_<soc>/` | HAL strong symbols |
| `driver_ws2812` | `components/driver_ws2812/` | **Optional**; the only product driver allowed to use vendor RMT/`led_strip` |
| `app` | `components/app/` | `REQUIRES mini_tree` (+ optional `driver_ws2812`) |

**One mini, many MCUs**: the same `mini_tree` (symlink / submodule / copy) can be attached to multiple board projects; each project carries its own `board_port.cmake` + `board_*` + `hal_*`. You can also point `MINI_TREE_BOARD_PORT` at any injection file.

**Don't** set `EXTRA_COMPONENT_DIRS` to the old `components/driver/` anymore (deprecated).

Dependency chain: `app` → `mini_tree` (+ optional `driver_ws2812`).

---

## 3. CMake Essentials Inside the Component

On `ESP_PLATFORM`, the root `CMakeLists.txt` sets up the dtc extensions and then `include(cmake/esp_idf.cmake)`:

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
        ${_PRODUCT_DRV_SRC_DIRS}   # drivers/*/src (e.g. st7789 header in src)
        "${GENERATED_BOARD_DIR}"
        …
    REQUIRES
        freertos
        esp_driver_gpio esp_driver_spi esp_driver_uart
        esp_driver_i2c esp_driver_twai
)
```

> Above: `SRCS` includes the GLOBbed `drivers/*/src/*.c` plus vfs/bus; `INCLUDE_DIRS` includes the product-driver include/src dirs and the generated board dir.

Afterwards, attach the gen dependencies, `CONFIG_OSAL_*`, and `error_symbols.ld` to **`${COMPONENT_LIB}`**.

---

## 4. Product Driver Layout (GLOB)

Unified layout (37 product drivers):

```text
mini_tree/drivers/<chip>/
├── include/     # public headers (ioctl / regs / bridge)
└── src/         # *.c; may include private .h
```

| Entry | Sources | Include | dtc-lite scan |
| :--- | :--- | :--- | :--- |
| `cmake/esp_idf.cmake` | `drivers/*/src/*.c` | `drivers/*/include` + `drivers/*/src` | `_PRODUCT_DRV_SRC_DIRS` |
| Root `CMakeLists.txt` (non-ESP) | same | same | same |
| `board/CMakeLists.txt` (standalone board lib) | — | — | `../drivers/*/src` |
| `compile_flags.txt` | — | all `-Idrivers/*/include` (and `src` dirs that hold headers) | — |

Out-of-tree exception: `components/driver_ws2812/src` is only scanned into dtc via `MINI_TREE_DTC_EXTRA_SCAN_DIRS` / `EXTRA_DEPENDS`; the sources still compile inside the `driver_ws2812` component (`WHOLE_ARCHIVE`).

The old `drivers/flash` (`winbond,w25q64`) has been removed; Flash uses `drivers/w25qxx` (`winbond,w25qxx`).

---

## 5. Dual-Track Kconfig

| Track | Role |
| :--- | :--- |
| Component root `Kconfig` | Shows up in `idf.py menuconfig` → Component config → mini_tree |
| Component `.config` | Used for non-ESP builds / source selection; **does not overwrite** the board `.config` when syncing to the shelf |

Under ESP builds the generated `config.h` is usually a placeholder; business `CONFIG_*` values come from `sdkconfig.h`.

Like the generic CMake, `cmake/esp_idf.cmake` soft-codes off `.config`: unless `CONFIG_USB` is explicitly `# ... is not set`, it builds `bus/usb`, `vfs/usb`, and `hal/usb` (the board must provide the usb_tusb_port glue and REQUIRE `esp_tinyusb` itself); when trimmed, none of them build. If the ESP board has no glue yet, keep `# CONFIG_USB is not set` in `.config`.

---

## 6. DTS / Generated Artifacts / HAL

| Item | ESP reference |
| :--- | :--- |
| `BOARD_DTS` | Injected by `board_port.cmake` (e.g. `board_esp32s3/dts/board.dts`); the middleware default is only a placeholder (`mini-tree,placeholder` — compiles but has **no board nodes`) |
| Generated dirs | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree` |
| dtc-lite | Scans vfs/bus + **all** `drivers/*/src`; `-I mini_tree/board` resolves `dt-bindings/`; chip headers/macros go through `MINI_TREE_DTC_EXTRA_ARGS` |
| HAL | Board `hal_<soc>`; still weak placeholders inside the shelf |

**Don't** put the board `dtsi/` into dtc `-I`: it would be treated as vendor headers and macro-extracted by cpp instead of inlined, leaving only the root node (`devices: 1`). dtsi files are resolved automatically from the `board_dir` of `BOARD_DTS`.

**Don't** hard-code any `IDF_TARGET`'s `soc/<chip>/include` inside `cmake/esp_idf.cmake`.

The public-header rule is unchanged: **product drivers must not** `#include` vendor SDKs (except `driver_ws2812`).

---

## 7. Dependencies and ETL

Current `lib/` state: only **FreeRTOS (v11.3.0), RT-Thread (v5.3.0), and ETL** are vendored; every other brick comes via FetchContent.

- ESP drivers: declared in `idf_component_register(... REQUIRES …)`.
- Config-time FetchContent (the root CMake includes `cmake/*.cmake` directly, local-or-fetch): TinyUSB, lwIP, cJSON. Under ESP, prefer IDF components / Component Manager instead (e.g. `esp_tinyusb`, `managed_components`).
- Link-time FetchContent (`mini_tree_link_*`, local-or-fetch on link): LVGL, u8g2, littlefs, FatFs, SFUD, Mbed TLS, coreMQTT, coreHTTP, nanopb, miniz, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB.
- ETL: vendored in-repo (`lib/etl`, include + cmake only), nothing to fetch; **don't** stuff `managed_components` into `EXTRA_COMPONENT_DIRS`.
- FreeRTOS: ships with IDF; `CONFIG_OSAL_FREERTOS` binds to the IDF kernel — don't embed `lib/freeRTOS` again.

---

## 8. Syncing with the ESP Board Project

This repo (shelf: `/home/ning/project/shelf/mini_tree`, or a copy inside the platform repo) and `platform/Espressif/esp32s3/components/mini_tree` should stay in sync.

Recommendations:

1. Complete product-driver / DTS / CMake acceptance in the **ESP board project** (`idf.py build`).
2. **Sync content to the shelf** (`rsync` add/overwrite; **don't** `--delete` shelf-only items such as `board/docs`; **exclude** `.git` / `.config`).
3. Reverse: when copying generic-layer changes back to the board project, keep the board `hal_esp32s3`, the root project `CMakeLists.txt`, and `driver_ws2812`.

Don't overwrite the repo's non-ESP root `CMakeLists.txt` logic with the whole ESP `idf_component_register` file (the two entry points coexist via the `if(ESP_PLATFORM)` branch).

---

## 9. Acceptance Checklist

- [ ] No stale `EXTRA_COMPONENT_DIRS=…/components/driver`
- [ ] `idf.py build` generates `board_probe.c` / `dt_config_gen.h`; every product-driver `DRIVER_REGISTER` matches
- [ ] The `drivers/*/src/*.c` GLOB covers all 37 product drivers — no per-file lists
- [ ] `app` `REQUIRES mini_tree` (+ `driver_ws2812` when needed)
- [ ] `drivers/flash` doesn't exist; Flash nodes use `winbond,w25qxx`
- [ ] OSAL matches IDF FreeRTOS — no dual kernel

---

## Related Docs

- [getting_started.md](getting_started.md) · [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md) (ESP-IDF VFS mental mapping)
