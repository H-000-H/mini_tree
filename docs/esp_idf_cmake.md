# ESP-IDF CMake 特殊集成

> 相对本仓根目录「通用 CMake」（`add_subdirectory` + `add_library(mini_tree STATIC)`），  
> ESP32 走 **IDF 组件** 路径。参考实现：  
> `platform/Espressif/esp32s3/`（`components/mini_tree` + 可选 `components/driver_ws2812`）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 把本 shelf 接到 ESP-IDF 工程的人 |
| **前置** | [getting_started.md](getting_started.md)（通用 CMake） |
| **相关** | [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md) · [design_decisions.md](design_decisions.md) |

---

## 目录

1. [和通用 CMake 的区别](#1-和通用-cmake-的区别)
2. [工程怎么挂上组件](#2-工程怎么挂上组件)
3. [组件内 CMake 要点](#3-组件内-cmake-要点)
4. [产品驱动路径（GLOB）](#4-产品驱动路径glob)
5. [Kconfig 双轨](#5-kconfig-双轨)
6. [DTS / 生成物 / HAL](#6-dts--生成物--hal)
7. [依赖与 ETL](#7-依赖与-etl)
8. [与 ESP 板工程同步](#8-与-esp-板工程同步)
9. [验收清单](#9-验收清单)

---

## 1. 和通用 CMake 的区别

| 项 | 本仓通用（ST / 裸工程等） | ESP-IDF |
| :--- | :--- | :--- |
| 接入 | `add_subdirectory(mini_tree)` | `components/mini_tree` 自动扫描；`ESP_PLATFORM` 时走 `cmake/esp_idf.cmake` |
| 目标 | `add_library(mini_tree STATIC)` | `idf_component_register(...)` |
| 引用生成物 | `target_* (mini_tree …)` | `target_* (${COMPONENT_LIB} …)` |
| 路径 | `CMAKE_CURRENT_LIST_DIR` | **必须**用 `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR`（IDF 两阶段；勿依赖 requirements 阶段的 `SOURCE_DIR`）；`file(GLOB …)` **不要**加 `CONFIGURE_DEPENDS`（requirements 为 script 模式） |
| 厂商头 | `VENDOR_INC_DIRS` / `VENDOR_DEFINES` 给 dtc | 通常 **不用**；板级 HAL 在 `hal_esp32s3`，`REQUIRES` ESP 驱动 |
| HAL | 本仓 weak；板级强符号另链 | 板级 `hal_*_esp32*.c` 强符号 |
| Kconfig | 仅 `.config` + `genconfig.py` | 组件 `Kconfig` 进 `idf.py menuconfig`；ESP 下 `config.h` 常为空壳，真值走 `sdkconfig.h` |
| 链接脚本 | 板级 `-T` / `error_symbols.ld` | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

---

## 2. 工程怎么挂上组件

参考：`Heterogeneous-Multicore/platform/Espressif/esp32s3/CMakeLists.txt`

```cmake
# components/mini_tree + hal_* + 可选 driver_ws2812 自动扫描
# 板级注入：components/board_port.cmake（DTS / 芯片 dtc -I/-D）
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_board)
```

| 组件 / 文件 | 路径 | 说明 |
| :--- | :--- | :--- |
| `mini_tree` | `components/mini_tree/` | **纯架构**中间件 + `drivers/*`；无 SoC 硬编码 |
| `board_port.cmake` | `components/board_port.cmake` | 板级注入：`BOARD_DTS` / `BOARD_DTSI_DIR` / 芯片 `-I/-D` / 树外扫描 |
| `board_<soc>` | `components/board_<soc>/` | 板级 `dts/` + `dtsi/`（**非** IDF 组件，仅数据） |
| `hal_<soc>` | `components/hal_<soc>/` | HAL 强符号 |
| `driver_ws2812` | `components/driver_ws2812/` | **可选**；唯一允许厂商 RMT/`led_strip` 的产品驱动 |
| `app` | `components/app/` | `REQUIRES mini_tree`（+ 可选 `driver_ws2812`） |

**一份 mini 配多 MCU**：同一份 `mini_tree`（symlink / 子模块 / 拷贝）可挂在多个板工程下；每个工程自带 `board_port.cmake` + `board_*` + `hal_*`。也可设 `MINI_TREE_BOARD_PORT` 指向任意注入文件。

**不要**再设 `EXTRA_COMPONENT_DIRS` 指向旧的 `components/driver/`（已废弃）。

依赖链：`app` → `mini_tree`（+ 可选 `driver_ws2812`）。

---

## 3. 组件内 CMake 要点

`ESP_PLATFORM` 时根 `CMakeLists.txt` 设置 dtc 扩展后 `include(cmake/esp_idf.cmake)`：

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

其后对 **`${COMPONENT_LIB}`** 挂 gen 依赖、`CONFIG_OSAL_*`、`error_symbols.ld`。

---

## 4. 产品驱动路径（GLOB）

统一布局：

```text
mini_tree/drivers/<chip>/
├── include/     # 对外头（ioctl / regs / bridge）
└── src/         # *.c；可含私有 .h
```

| 构建入口 | 源文件 | Include | dtc-lite 扫描 |
| :--- | :--- | :--- | :--- |
| `cmake/esp_idf.cmake` | `drivers/*/src/*.c` | `drivers/*/include` + `drivers/*/src` | `_PRODUCT_DRV_SRC_DIRS` |
| 根 `CMakeLists.txt`（非 ESP） | 同上 | 同上 | 同上 |
| `board/CMakeLists.txt`（独立 board 库） | — | — | `../drivers/*/src` |
| `compile_flags.txt` | — | 全部 `-Idrivers/*/include`（及含头的 `src`） | — |

树外例外：`components/driver_ws2812/src` 仅通过 `MINI_TREE_DTC_EXTRA_SCAN_DIRS` / `EXTRA_DEPENDS` 扫入 dtc；源文件仍编在 `driver_ws2812` 组件（`WHOLE_ARCHIVE`）。

旧 `drivers/flash`（`winbond,w25q64`）已删除；Flash 用 `drivers/w25qxx`（`winbond,w25qxx`）。

---

## 5. Kconfig 双轨

| 轨道 | 作用 |
| :--- | :--- |
| 组件根 `Kconfig` | 出现在 `idf.py menuconfig` → Component config → mini_tree |
| 组件 `.config` | 非 ESP / 选源文件时用；**同步到 shelf 时默认不覆盖**板级 `.config` |

ESP 构建下生成的 `config.h` 常为占位；业务 `CONFIG_*` 以 `sdkconfig.h` 为准。

---

## 6. DTS / 生成物 / HAL

| 项 | ESP 参考做法 |
| :--- | :--- |
| `BOARD_DTS` | 由 `board_port.cmake` 注入（例：`board_esp32s3/dts/board.dts`）；中间件默认仅为占位 |
| 生成目录 | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree` |
| dtc-lite | 扫 vfs/bus + **全部** `drivers/*/src`；`-I mini_tree/board` 解析 `dt-bindings/`；芯片头/宏走 `MINI_TREE_DTC_EXTRA_ARGS` |
| HAL | 板级 `hal_<soc>`；shelf 内仍为 weak 占位 |

**勿**把板级 `dtsi/` 放进 dtc `-I`：会被当成厂商头走 cpp 抽宏而不内联，结果只剩根节点（`devices: 1`）。dtsi 靠 `BOARD_DTS` 所在 `board_dir` 自动解析。  
**勿**在 `cmake/esp_idf.cmake` 写死某一 `IDF_TARGET` 的 `soc/<chip>/include`。

公共头规则不变：**产品驱动禁止** `#include` 厂商 SDK（`driver_ws2812` 除外）。

---

## 7. 依赖与 ETL

- ESP 驱动：写在 `idf_component_register(... REQUIRES …)`。  
- ETL：`idf_component.yml` / Fetch / 本地 `lib/etl`；**不要**把 `managed_components` 塞进 `EXTRA_COMPONENT_DIRS`。  
- FreeRTOS：IDF 自带；`CONFIG_OSAL_FREERTOS` 对接 IDF 内核，勿再嵌 `lib/freeRTOS`。

---

## 8. 与 ESP 板工程同步

本仓库（shelf：`/home/ning/project/shelf/mini_tree` 或平台仓内副本）与  
`platform/Espressif/esp32s3/components/mini_tree` 应对齐。

建议：

1. 在 **ESP 板工程**完成产品驱动 / DTS / CMake 验收（`idf.py build`）。  
2. **内容同步到 shelf**（`rsync` 添加/覆盖，**不要** `--delete` 掉 shelf 独有如 `board/docs`；**排除** `.git` / `.config`）。  
3. 反向：shelf 改通用层后再拷回板工程时，保留板级 `hal_esp32s3`、根工程 `CMakeLists.txt`、`driver_ws2812`。

不要把 ESP 的 `idf_component_register` 整文件覆盖本仓「非 ESP」根 `CMakeLists.txt` 逻辑（两入口靠 `if(ESP_PLATFORM)` 分支共存）。

---

## 9. 验收清单

- [ ] 无过时 `EXTRA_COMPONENT_DIRS=…/components/driver`  
- [ ] `idf.py build` 生成 `board_probe.c` / `dt_config_gen.h`，产品驱动 `DRIVER_REGISTER` 均已匹配  
- [ ] `app` `REQUIRES mini_tree`（+ 需要时 `driver_ws2812`）  
- [ ] `drivers/flash` 不存在；Flash 节点为 `winbond,w25qxx`  
- [ ] OSAL 与 IDF FreeRTOS 一致，无双内核  

---

## 相关文档

- [getting_started.md](getting_started.md) · [porting_guide.md](porting_guide.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)  
- [references.md](references.md)（ESP-IDF VFS 心智对照）
