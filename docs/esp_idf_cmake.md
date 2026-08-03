# ESP-IDF CMake 特殊集成 / ESP-IDF CMake Integration

> 相对本仓根目录「通用 CMake」（`add_subdirectory` + `add_library(mini_tree STATIC)`），
> ESP32 走 **IDF 组件** 路径。参考实现：
> `platform/Espressif/esp32s3/`（`components/mini_tree` + 可选 `components/driver_ws2812`）。
>
> Unlike the generic CMake at the repo root (`add_subdirectory` + `add_library(mini_tree STATIC)`),
> ESP32 uses the **IDF component** path. Reference implementation:
> `platform/Espressif/esp32s3/` (`components/mini_tree` + optional `components/driver_ws2812`).

| 项 / Item | 内容 / Description |
| :--- | :--- |
| **读者 / Audience** | 把本 shelf 接到 ESP-IDF 工程的人<br>Anyone wiring this shelf into an ESP-IDF project |
| **前置 / Prereq.** | [getting_started.md](getting_started.md)（通用 CMake）<br>[getting_started.md](getting_started.md) (generic CMake) |
| **相关 / Related** | [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md) · [design_decisions.md](design_decisions.md) |

---

## 目录 / Table of Contents

1. [和通用 CMake 的区别 / Differences from Generic CMake](#1-和通用-cmake-的区别-differences-from-generic-cmake)
2. [工程怎么挂上组件 / Wiring the Component into a Project](#2-工程怎么挂上组件-wiring-the-component-into-a-project)
3. [组件内 CMake 要点 / CMake Essentials Inside the Component](#3-组件内-cmake-要点-cmake-essentials-inside-the-component)
4. [产品驱动路径（GLOB）/ Product Driver Layout (GLOB)](#4-产品驱动路径glob-product-driver-layout-glob)
5. [Kconfig 双轨 / Dual-Track Kconfig](#5-kconfig-双轨-dual-track-kconfig)
6. [DTS / 生成物 / HAL](#6-dts--生成物--hal)
7. [依赖与 ETL / Dependencies and ETL](#7-依赖与-etl-dependencies-and-etl)
8. [与 ESP 板工程同步 / Syncing with the ESP Board Project](#8-与-esp-板工程同步-syncing-with-the-esp-board-project)
9. [验收清单 / Acceptance Checklist](#9-验收清单-acceptance-checklist)

---

## 1. 和通用 CMake 的区别 / Differences from Generic CMake

| 项 / Item | 本仓通用（ST / 裸工程等）<br>Generic (ST / bare-metal) | ESP-IDF |
| :--- | :--- | :--- |
| 接入 / Entry | `add_subdirectory(mini_tree)` | `components/mini_tree` 自动扫描；`ESP_PLATFORM` 时走 `cmake/esp_idf.cmake` 并 `return()`<br>Auto-scanned as `components/mini_tree`; on `ESP_PLATFORM` it includes `cmake/esp_idf.cmake` and `return()`s |
| 目标 / Target | `add_library(mini_tree STATIC)` | `idf_component_register(...)` |
| 引用生成物 / Generated refs | `target_* (mini_tree …)` | `target_* (${COMPONENT_LIB} …)` |
| 路径 / Paths | `CMAKE_CURRENT_LIST_DIR` | **必须**用 `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR`（IDF 两阶段；勿依赖 requirements 阶段的 `SOURCE_DIR`）；`file(GLOB …)` **不要**加 `CONFIGURE_DEPENDS`（requirements 为 script 模式）<br>**Must** use `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR` (IDF is two-phase; don't rely on `SOURCE_DIR` during requirements); **don't** add `CONFIGURE_DEPENDS` to `file(GLOB …)` (requirements runs in script mode) |
| 厂商头 / Vendor headers | `VENDOR_INC_DIRS` / `VENDOR_DEFINES` 给 dtc | 通常 **不用**；板级 HAL 在 `hal_esp32s3`，`REQUIRES` ESP 驱动<br>Usually **unneeded**; board HAL lives in `hal_esp32s3` and `REQUIRES` ESP drivers |
| HAL | 本仓 weak；板级强符号另链<br>Weak in the repo; board strong symbols linked separately | 板级 `hal_*_esp32*.c` 强符号<br>Board `hal_*_esp32*.c` strong symbols |
| Kconfig | 仅 `.config` + `genconfig.py` | 组件 `Kconfig` 进 `idf.py menuconfig`；ESP 下 `config.h` 常为空壳，真值走 `sdkconfig.h`<br>Component `Kconfig` feeds `idf.py menuconfig`; under ESP the generated `config.h` is usually an empty shell — the real values come from `sdkconfig.h` |
| 链接脚本 / Linker script | 板级 `-T` / `error_symbols.ld` | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

---

## 2. 工程怎么挂上组件 / Wiring the Component into a Project

参考：`platform/Espressif/esp32s3/CMakeLists.txt`（[Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) 仓库内）
Reference: `platform/Espressif/esp32s3/CMakeLists.txt` in the [Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) repo

```cmake
# components/mini_tree + hal_* + 可选 driver_ws2812 自动扫描
# 板级注入：components/board_port.cmake（DTS / 芯片 dtc -I/-D）
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_board)
```

> 上例：`components/mini_tree` + `hal_*` + 可选 `driver_ws2812` 自动扫描；板级经 `components/board_port.cmake` 注入（DTS / 芯片 dtc `-I/-D`）。
> Above: auto-scans `components/mini_tree` + `hal_*` + optional `driver_ws2812`; the board injects DTS / chip dtc `-I/-D` via `components/board_port.cmake`.

| 组件 / 文件 | 路径 | 说明 / Description |
| :--- | :--- | :--- |
| `mini_tree` | `components/mini_tree/` | **纯架构**中间件 + `drivers/*`；无 SoC 硬编码<br>**Pure-architecture** middleware + `drivers/*`; no SoC hard-coding |
| `board_port.cmake` | `components/board_port.cmake` | 板级注入：`BOARD_DTS` / `BOARD_DTSI_DIR` / 芯片 `-I/-D` / 树外扫描<br>Board injection: `BOARD_DTS` / `BOARD_DTSI_DIR` / chip `-I/-D` / out-of-tree scan |
| `board_<soc>` | `components/board_<soc>/` | 板级 `dts/` + `dtsi/`（**非** IDF 组件，仅数据）<br>Board `dts/` + `dtsi/` (**not** an IDF component, data only) |
| `hal_<soc>` | `components/hal_<soc>/` | HAL 强符号<br>HAL strong symbols |
| `driver_ws2812` | `components/driver_ws2812/` | **可选**；唯一允许厂商 RMT/`led_strip` 的产品驱动<br>**Optional**; the only product driver allowed to use vendor RMT/`led_strip` |
| `app` | `components/app/` | `REQUIRES mini_tree`（+ 可选 `driver_ws2812`）<br>`REQUIRES mini_tree` (+ optional `driver_ws2812`) |

**一份 mini 配多 MCU**：同一份 `mini_tree`（symlink / 子模块 / 拷贝）可挂在多个板工程下；每个工程自带 `board_port.cmake` + `board_*` + `hal_*`。也可设 `MINI_TREE_BOARD_PORT` 指向任意注入文件。

**One mini, many MCUs**: the same `mini_tree` (symlink / submodule / copy) can be attached to multiple board projects; each project carries its own `board_port.cmake` + `board_*` + `hal_*`. You can also point `MINI_TREE_BOARD_PORT` at any injection file.

**不要**再设 `EXTRA_COMPONENT_DIRS` 指向旧的 `components/driver/`（已废弃）。
**Don't** set `EXTRA_COMPONENT_DIRS` to the old `components/driver/` anymore (deprecated).

依赖链：`app` → `mini_tree`（+ 可选 `driver_ws2812`）。
Dependency chain: `app` → `mini_tree` (+ optional `driver_ws2812`).

---

## 3. 组件内 CMake 要点 / CMake Essentials Inside the Component

`ESP_PLATFORM` 时根 `CMakeLists.txt` 设置 dtc 扩展后 `include(cmake/esp_idf.cmake)`：
On `ESP_PLATFORM`, the root `CMakeLists.txt` sets up the dtc extensions and then `include(cmake/esp_idf.cmake)`:

```cmake
idf_component_register(
    SRCS
        ${OSAL_SRCS} ${HAL_SRCS} ${BOARD_SRCS} ${CORE_SRCS}
        ${DRIVER_SRCS}   # 含 GLOB：drivers/*/src/*.c + vfs/bus
        ${SYSTEM_SRCS} ${GEN_SRCS}
    INCLUDE_DIRS
        "${MINI_TREE_DIR}"
        "${MINI_TREE_DIR}/board/include"
        # … vfs/bus/hal …
        ${_PRODUCT_DRV_INC_DIRS}   # drivers/*/include
        ${_PRODUCT_DRV_SRC_DIRS}   # drivers/*/src（如 st7789 头在 src）
        "${GENERATED_BOARD_DIR}"
        …
    REQUIRES
        freertos
        esp_driver_gpio esp_driver_spi esp_driver_uart
        esp_driver_i2c esp_driver_twai
)
```

> 上例：`SRCS` 含 GLOB 到的 `drivers/*/src/*.c` 与 vfs/bus；`INCLUDE_DIRS` 含产品驱动 include/src 与生成目录。
> Above: `SRCS` includes the GLOBbed `drivers/*/src/*.c` plus vfs/bus; `INCLUDE_DIRS` includes the product-driver include/src dirs and the generated board dir.

其后对 **`${COMPONENT_LIB}`** 挂 gen 依赖、`CONFIG_OSAL_*`、`error_symbols.ld`。
Afterwards, attach the gen dependencies, `CONFIG_OSAL_*`, and `error_symbols.ld` to **`${COMPONENT_LIB}`**.

---

## 4. 产品驱动路径（GLOB）/ Product Driver Layout (GLOB)

统一布局（共 37 个产品驱动）：

Unified layout (37 product drivers):

```text
mini_tree/drivers/<chip>/
├── include/     # 对外头（ioctl / regs / bridge）
└── src/         # *.c；可含私有 .h
```

| 构建入口 / Entry | 源文件 / Sources | Include | dtc-lite 扫描 / Scan |
| :--- | :--- | :--- | :--- |
| `cmake/esp_idf.cmake` | `drivers/*/src/*.c` | `drivers/*/include` + `drivers/*/src` | `_PRODUCT_DRV_SRC_DIRS` |
| 根 `CMakeLists.txt`（非 ESP） | 同上 / same | 同上 / same | 同上 / same |
| `board/CMakeLists.txt`（独立 board 库） | — | — | `../drivers/*/src` |
| `compile_flags.txt` | — | 全部 `-Idrivers/*/include`（及含头的 `src`）<br>All `-Idrivers/*/include` (and `src` dirs that hold headers) | — |

树外例外：`components/driver_ws2812/src` 仅通过 `MINI_TREE_DTC_EXTRA_SCAN_DIRS` / `EXTRA_DEPENDS` 扫入 dtc；源文件仍编在 `driver_ws2812` 组件（`WHOLE_ARCHIVE`）。

Out-of-tree exception: `components/driver_ws2812/src` is only scanned into dtc via `MINI_TREE_DTC_EXTRA_SCAN_DIRS` / `EXTRA_DEPENDS`; the sources still compile inside the `driver_ws2812` component (`WHOLE_ARCHIVE`).

旧 `drivers/flash`（`winbond,w25q64`）已删除；Flash 用 `drivers/w25qxx`（`winbond,w25qxx`）。
The old `drivers/flash` (`winbond,w25q64`) has been removed; Flash uses `drivers/w25qxx` (`winbond,w25qxx`).

---

## 5. Kconfig 双轨 / Dual-Track Kconfig

| 轨道 / Track | 作用 / Role |
| :--- | :--- |
| 组件根 `Kconfig` | 出现在 `idf.py menuconfig` → Component config → mini_tree<br>Shows up in `idf.py menuconfig` → Component config → mini_tree |
| 组件 `.config` | 非 ESP / 选源文件时用；**同步到 shelf 时默认不覆盖**板级 `.config`<br>Used for non-ESP builds / source selection; **does not overwrite** the board `.config` when syncing to the shelf |

ESP 构建下生成的 `config.h` 常为占位；业务 `CONFIG_*` 以 `sdkconfig.h` 为准。

Under ESP builds the generated `config.h` is usually a placeholder; business `CONFIG_*` values come from `sdkconfig.h`.

`cmake/esp_idf.cmake` 与通用 CMake 一样按 `.config` 软编码：`CONFIG_USB` 未显式
`# ... is not set` 时编入 `bus/usb`、`vfs/usb`、`hal/usb`（板级需 usb_tusb_port glue 并
自行 REQUIRE `esp_tinyusb`）；裁剪则全部不编入。ESP 板若暂无 glue，.config 应保持
`# CONFIG_USB is not set`。

Like the generic CMake, `cmake/esp_idf.cmake` soft-codes off `.config`: unless `CONFIG_USB` is explicitly `# ... is not set`, it builds `bus/usb`, `vfs/usb`, and `hal/usb` (the board must provide the usb_tusb_port glue and REQUIRE `esp_tinyusb` itself); when trimmed, none of them build. If the ESP board has no glue yet, keep `# CONFIG_USB is not set` in `.config`.

---

## 6. DTS / 生成物 / HAL

| 项 / Item | ESP 参考做法 / ESP reference |
| :--- | :--- |
| `BOARD_DTS` | 由 `board_port.cmake` 注入（例：`board_esp32s3/dts/board.dts`）；中间件默认仅为占位（`mini-tree,placeholder`，可编过但**无板级节点**）<br>Injected by `board_port.cmake` (e.g. `board_esp32s3/dts/board.dts`); the middleware default is only a placeholder (`mini-tree,placeholder` — compiles but has **no board nodes**) |
| 生成目录 / Generated dirs | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree` |
| dtc-lite | 扫 vfs/bus + **全部** `drivers/*/src`；`-I mini_tree/board` 解析 `dt-bindings/`；芯片头/宏走 `MINI_TREE_DTC_EXTRA_ARGS`<br>Scans vfs/bus + **all** `drivers/*/src`; `-I mini_tree/board` resolves `dt-bindings/`; chip headers/macros go through `MINI_TREE_DTC_EXTRA_ARGS` |
| HAL | 板级 `hal_<soc>`；shelf 内仍为 weak 占位<br>Board `hal_<soc>`; still weak placeholders inside the shelf |

**勿**把板级 `dtsi/` 放进 dtc `-I`：会被当成厂商头走 cpp 抽宏而不内联，结果只剩根节点（`devices: 1`）。dtsi 靠 `BOARD_DTS` 所在 `board_dir` 自动解析。
**Don't** put the board `dtsi/` into dtc `-I`: it would be treated as vendor headers and macro-extracted by cpp instead of inlined, leaving only the root node (`devices: 1`). dtsi files are resolved automatically from the `board_dir` of `BOARD_DTS`.

**勿**在 `cmake/esp_idf.cmake` 写死某一 `IDF_TARGET` 的 `soc/<chip>/include`。
**Don't** hard-code any `IDF_TARGET`'s `soc/<chip>/include` inside `cmake/esp_idf.cmake`.

公共头规则不变：**产品驱动禁止** `#include` 厂商 SDK（`driver_ws2812` 除外）。
The public-header rule is unchanged: **product drivers must not** `#include` vendor SDKs (except `driver_ws2812`).

---

## 7. 依赖与 ETL / Dependencies and ETL

`lib/` 现状：随仓 vendor 仅 **FreeRTOS（v11.3.0）、RT-Thread（v5.3.0）、ETL**；其余积木全部走 FetchContent。

Current `lib/` state: only **FreeRTOS (v11.3.0), RT-Thread (v5.3.0), and ETL** are vendored; every other brick comes via FetchContent.

- ESP 驱动：写在 `idf_component_register(... REQUIRES …)`。
  ESP drivers: declared in `idf_component_register(... REQUIRES …)`.
- 链接期 FetchContent（调用 `mini_tree_link_*` 才拉取，local-or-fetch）：TinyUSB、lwIP、cJSON。ESP 下建议改用 IDF 组件 / Component Manager（如 `esp_tinyusb`、`managed_components`）。
  Config-time FetchContent (the root CMake includes `cmake/*.cmake` directly, local-or-fetch): TinyUSB, lwIP, cJSON. Under ESP, prefer IDF components / Component Manager instead (e.g. `esp_tinyusb`, `managed_components`).
- 链接期 FetchContent（按需 `mini_tree_link_*`，local-or-fetch on link）：LVGL、u8g2、littlefs、FatFs、SFUD、Mbed TLS、coreMQTT、coreHTTP、nanopb、miniz、MCUBoot、FreeModbus、libmodbus、CMSIS-DSP、MultiButton、EasyFlash、EasyLogger、FlashDB。
  Link-time FetchContent (`mini_tree_link_*`, local-or-fetch on link): LVGL, u8g2, littlefs, FatFs, SFUD, Mbed TLS, coreMQTT, coreHTTP, nanopb, miniz, MCUBoot, FreeModbus, libmodbus, CMSIS-DSP, MultiButton, EasyFlash, EasyLogger, FlashDB.
- ETL：随仓 vendor（`lib/etl`，仅 include + cmake），无需拉取；**不要**把 `managed_components` 塞进 `EXTRA_COMPONENT_DIRS`。
  ETL: vendored in-repo (`lib/etl`, include + cmake only), nothing to fetch; **don't** stuff `managed_components` into `EXTRA_COMPONENT_DIRS`.
- FreeRTOS：IDF 自带；`CONFIG_OSAL_FREERTOS` 对接 IDF 内核，勿再嵌 `lib/freeRTOS`。
  FreeRTOS: ships with IDF; `CONFIG_OSAL_FREERTOS` binds to the IDF kernel — don't embed `lib/freeRTOS` again.

---

## 8. 与 ESP 板工程同步 / Syncing with the ESP Board Project

本仓库（shelf：`/home/ning/project/shelf/mini_tree` 或平台仓内副本）与
`platform/Espressif/esp32s3/components/mini_tree` 应对齐。

This repo (shelf: `/home/ning/project/shelf/mini_tree`, or a copy inside the platform repo) and
`platform/Espressif/esp32s3/components/mini_tree` should stay in sync.

建议 / Recommendations:

1. 在 **ESP 板工程**完成产品驱动 / DTS / CMake 验收（`idf.py build`）。
   Complete product-driver / DTS / CMake acceptance in the **ESP board project** (`idf.py build`).
2. **内容同步到 shelf**（`rsync` 添加/覆盖，**不要** `--delete` 掉 shelf 独有如 `board/docs`；**排除** `.git` / `.config`）。
   **Sync content to the shelf** (`rsync` add/overwrite; **don't** `--delete` shelf-only items such as `board/docs`; **exclude** `.git` / `.config`).
3. 反向：shelf 改通用层后再拷回板工程时，保留板级 `hal_esp32s3`、根工程 `CMakeLists.txt`、`driver_ws2812`。
   Reverse: when copying generic-layer changes back to the board project, keep the board `hal_esp32s3`, the root project `CMakeLists.txt`, and `driver_ws2812`.

不要把 ESP 的 `idf_component_register` 整文件覆盖本仓「非 ESP」根 `CMakeLists.txt` 逻辑（两入口靠 `if(ESP_PLATFORM)` 分支共存）。

Don't overwrite the repo's non-ESP root `CMakeLists.txt` logic with the whole ESP `idf_component_register` file (the two entry points coexist via the `if(ESP_PLATFORM)` branch).

---

## 9. 验收清单 / Acceptance Checklist

- [ ] 无过时 `EXTRA_COMPONENT_DIRS=…/components/driver`
- [ ] No stale `EXTRA_COMPONENT_DIRS=…/components/driver`
- [ ] `idf.py build` 生成 `board_probe.c` / `dt_config_gen.h`，产品驱动 `DRIVER_REGISTER` 均已匹配
- [ ] `idf.py build` generates `board_probe.c` / `dt_config_gen.h`; every product-driver `DRIVER_REGISTER` matches
- [ ] `drivers/*/src/*.c` GLOB 覆盖全部 37 个产品驱动，无逐文件列表
- [ ] The `drivers/*/src/*.c` GLOB covers all 37 product drivers — no per-file lists
- [ ] `app` `REQUIRES mini_tree`（+ 需要时 `driver_ws2812`）
- [ ] `app` `REQUIRES mini_tree` (+ `driver_ws2812` when needed)
- [ ] `drivers/flash` 不存在；Flash 节点为 `winbond,w25qxx`
- [ ] `drivers/flash` doesn't exist; Flash nodes use `winbond,w25qxx`
- [ ] OSAL 与 IDF FreeRTOS 一致，无双内核
- [ ] OSAL matches IDF FreeRTOS — no dual kernel

---

## 相关文档 / Related Docs

- [getting_started.md](getting_started.md) · [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md)（ESP-IDF VFS 心智对照 / ESP-IDF VFS mental mapping）
