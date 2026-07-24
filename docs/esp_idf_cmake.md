# ESP-IDF CMake 特殊集成

> 相对本仓根目录「通用 CMake」（`add_subdirectory` + `add_library(mini_tree STATIC)`），  
> ESP32 走 **IDF 组件** 路径。参考实现：异构仓库  
> `platform/Espressif/esp32s3/`（组件常在 `components/components/mini_tree`）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 把本 shelf 接到 ESP-IDF 工程的人 |
| **前置** | [getting_started.md](getting_started.md)（通用 CMake） |
| **相关** | [porting_guide.md](porting_guide.md) · [design_decisions.md](design_decisions.md) · [ecosystem.md](ecosystem.md) |

---

## 目录

1. [和通用 CMake 的区别](#1-和通用-cmake-的区别)
2. [工程怎么挂上组件](#2-工程怎么挂上组件)
3. [组件内 CMake 要点](#3-组件内-cmake-要点)
4. [Kconfig 双轨](#4-kconfig-双轨)
5. [DTS / 生成物 / HAL](#5-dts--生成物--hal)
6. [依赖与 ETL](#6-依赖与-etl)
7. [从本 shelf 同步到 ESP 工程](#7-从本-shelf-同步到-esp-工程)
8. [验收清单](#8-验收清单)

---

## 1. 和通用 CMake 的区别

| 项 | 本仓通用（ST / 裸工程等） | ESP-IDF |
| :--- | :--- | :--- |
| 接入 | `add_subdirectory(mini_tree)` | `EXTRA_COMPONENT_DIRS` + **组件** |
| 目标 | `add_library(mini_tree STATIC)` | `idf_component_register(...)` |
| 引用生成物 | `target_* (mini_tree …)` | `target_* (${COMPONENT_LIB} …)` |
| 路径 | `CMAKE_CURRENT_LIST_DIR` | **必须**用 `CMAKE_CURRENT_LIST_DIR`（IDF 两阶段；勿依赖 requirements 阶段的 `SOURCE_DIR`） |
| 厂商头 | `VENDOR_INC_DIRS` / `VENDOR_DEFINES` 给 dtc | 通常 **不用**；HAL 编进组件，`REQUIRES` ESP 驱动 |
| HAL | 本仓 weak；板级强符号另链 | 组件内 `hal_*_esp32*.c` 等强符号 |
| Kconfig | 仅 `.config` + `genconfig.py` | 组件 `Kconfig` 进 `idf.py menuconfig`，**另**保留 `.config`→`config.h` |
| 链接脚本 | 板级 `-T` / `error_symbols.ld` | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

本仓根 `CMakeLists.txt` 开头已写明：目录布局对齐 ESP 组件基准，但**构建入口是通用静态库**，不是 `idf_component_register`。

---

## 2. 工程怎么挂上组件

在 **ESP 工程根** `CMakeLists.txt`、调用 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` **之前**：

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/components/components/app"
    "${CMAKE_SOURCE_DIR}/components/components/mini_tree")
# 不要把 managed_components 再写进 EXTRA_COMPONENT_DIRS（会重复）

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3)
```

参考路径（平台仓）：

- 工程根：`Heterogeneous-Multicore/platform/Espressif/esp32s3/CMakeLists.txt`
- 组件：`…/esp32s3/components/components/mini_tree/`（注意双层 `components/`）

依赖链常见为：`main` `REQUIRES app` → `app` `REQUIRES mini_tree`。

---

## 3. 组件内 CMake 要点

组件根 `CMakeLists.txt` 使用：

```cmake
idf_component_register(
    SRCS
        ${OSAL_SRCS}
        ${HAL_SRCS}
        # … vfs/bus/core/system/GEN_SRCS …
    INCLUDE_DIRS
        "."
        "${GENERATED_BOARD_DIR}"
        # … 与通用仓类似的 include 列表 …
        ${HAL_INCLUDE_DIRS}
    REQUIRES
        esp_driver_rmt
        esp_driver_gpio
        esp_driver_spi
        esp_driver_uart
        esp_driver_gptimer
        etlcpp   # 若工程用 component manager 拉 ETL
)
```

其后对 **`${COMPONENT_LIB}`** 挂：

- `add_dependencies`：`kconfig_gen` / `board_dts_gen` / scrubber 等  
- `target_compile_definitions`：`CONFIG_OSAL_*` 等  
- `target_linker_script(... error_symbols.ld)`  

OSAL / SYSTEM 仍读组件目录 `.config` 中的 `CONFIG_OSAL_*=y` / `CONFIG_SYSTEM_*=y`（与通用仓同一套路）。

---

## 4. Kconfig 双轨

| 轨道 | 作用 |
| :--- | :--- |
| 组件根 `Kconfig` | 出现在 `idf.py menuconfig` → Component config → mini_tree |
| 组件 `.config` + `tools/genconfig.py` | 生成 `build/generated/kconfig/mini_tree/config.h`，供 C 源 `#include "config.h"` |

两边选项应对齐；改完后重配/重编。不要只改 `sdkconfig` 却忘了组件 `.config`（若 CMake 仍以 `.config` 选源文件）。

---

## 5. DTS / 生成物 / HAL

| 项 | ESP 参考做法 |
| :--- | :--- |
| `BOARD_DTS` | 常写死板级文件，例如 `board/dts/esp32-s3-devkitc-1.dts`（相对组件目录） |
| 生成目录 | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree`（与通用仓同构） |
| dtc-lite | 仍扫 vfs/bus/drivers；ESP 路径通常**不**循环注入 `VENDOR_INC_DIRS` |
| HAL | `hal_gpio_esp32.c`、`hal_*_esp32s3.c` 等进 `SRCS`；中间件 shelf 仍只带 weak 占位 |

公共头规则不变：**不** `#include` 厂商驱动头进 vfs/bus 公共 API。

---

## 6. 依赖与 ETL

- ESP 驱动：写在 `idf_component_register(... REQUIRES …)`。  
- ETL：参考工程用 `idf_component.yml` 拉 `marcel-cd/etlcpp`（或等价），**不要**再把 `managed_components` 塞进 `EXTRA_COMPONENT_DIRS`。通用 CMake 仓则用 `cmake/etl.cmake`（本地 `lib/etl` 或 Fetch），见 [ecosystem.md](ecosystem.md)。  
- FreeRTOS：ESP-IDF 自带；OSAL 选 `CONFIG_OSAL_FREERTOS` 时对接 IDF 内核，**勿再嵌一份** `lib/freeRTOS` 进组件（与 [design_decisions.md](design_decisions.md) 一致）。

---

## 7. 从本 shelf 同步到 ESP 工程

本仓库是 **中间件 shelf**（通用 CMake）。ESP 工程里的 `components/.../mini_tree` 是带板级 HAL/DTS 的**工作副本**。

建议流程：

1. 在 shelf 改通用层（vfs/bus/hal 头、tools、docs）。  
2. 同步/拷贝到 ESP 组件树（或 submodule / 脚本），保留 ESP 侧 `CMakeLists.txt`（`idf_component_register`）、板级 `hal_*_esp*.c`、板级 dts。  
3. 在 ESP 工程 `idf.py build` 验证。  

不要把 ESP 的 `idf_component_register` 整文件直接覆盖本仓根 `CMakeLists.txt`。

---

## 8. 验收清单

- [ ] 根工程 `EXTRA_COMPONENT_DIRS` 含 mini_tree，且未重复登记 managed_components  
- [ ] `idf.py build` 能生成 `config.h` 与 `board_nodes.h` 等  
- [ ] app `REQUIRES mini_tree`，业务只走 `device_*`  
- [ ] OSAL 与 IDF FreeRTOS 一致，无双内核  
- [ ] menuconfig 与组件 `.config` / `config.h` 不互相矛盾  

---

## 相关文档

- [getting_started.md](getting_started.md) · [porting_guide.md](porting_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)  
- [references.md](references.md)（ESP-IDF VFS 心智对照）
