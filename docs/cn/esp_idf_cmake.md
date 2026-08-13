# ESP-IDF 移植指南（CMake 集成）

> 本分支是 **纯 ESP-IDF 组件**：以 `components/mini_tree` 接入 IDF，`ESP_PLATFORM` 时走 `cmake/esp_idf.cmake`。参考实现：`platform/Espressif/esp32s3/`（`components/mini_tree` + `components/board_port.cmake` + `components/hal_esp32s3` + 可选 `components/driver_ws2812`）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 把本组件接到 ESP-IDF 工程的人 |
| **前置** | [getting_started.md](getting_started.md)（ESP-IDF 接入） |
| **相关** | [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [design_decisions.md](design_decisions.md) |

---

## 目录

1. [概览：通用 CMake vs ESP-IDF](#1-概览通用-cmake-vs-esp-idf)
2. [工程结构](#2-工程结构)
3. [board_port.cmake 注入契约](#3-boardportcmake-注入契约)
4. [组件内 CMake 要点](#4-组件内-cmake-要点)
5. [Kconfig 双轨与平台声明](#5-kconfig-双轨与平台声明)
6. [DTS 与生成物](#6-dts-与生成物)
7. [HAL：ESP 全屏蔽 + 板级 strong 实现](#7-halesp-全屏蔽--板级-strong-实现)
8. [产品驱动路径（GLOB）](#8-产品驱动路径glob)
9. [依赖与 ETL](#9-依赖与-etl)
10. [与 shelf 仓库同步](#10-与-shelf-仓库同步)
11. [验收清单](#11-验收清单)

---

## 1. 概览：ESP-IDF 组件接入

| 项 | ESP-IDF |
| :--- | :--- |
| 接入 | `components/mini_tree` 自动扫描；`ESP_PLATFORM` 时走 `cmake/esp_idf.cmake` |
| 目标 | `idf_component_register(...)` |
| 引用生成物 | `target_* (${COMPONENT_LIB} …)` |
| 路径 | **必须**用 `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR`（IDF 两阶段；勿依赖 requirements 阶段的 `SOURCE_DIR`）；`file(GLOB …)` **不要**加 `CONFIGURE_DEPENDS`（requirements 为 script 模式） |
| HAL 实现 | 板级独立组件 `hal_<soc>`（`WHOLE_ARCHIVE`）；组件内 stub 在 ESP 下编译为空（见 [§7](#7-halesp-全屏蔽--板级-strong-实现)） |
| Kconfig | `Kconfig.projbuild` 进 `idf.py menuconfig`；`config.h` 仅转发 `sdkconfig.h`，真值走 `sdkconfig.h` |
| 链接脚本 | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

---

## 2. 工程结构

参考：`platform/Espressif/esp32s3/`（[Heterogeneous-Multicore](https://github.com/H-000-H/Heterogeneous-Multicore) 仓库内）

```cmake
# 根 CMakeLists.txt —— 仅两行, 其余全部由 components/ 自动组装
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_board)
```

| 组件 / 文件 | 路径 | 说明 |
| :--- | :--- | :--- |
| `mini_tree` | `components/mini_tree/` | **纯架构**中间件 + `drivers/*`；无 SoC 硬编码 |
| `board_port.cmake` | `components/board_port.cmake` | 板级注入：`BOARD_DTS` / `BOARD_DTSI_DIR` / 芯片 `-I/-D` / 树外扫描（见 [§3](#3-boardportcmake-注入契约)） |
| `board_<soc>` | `components/board_<soc>/` | 板级 `dts/` + `dtsi/`（**非** IDF 组件，仅数据） |
| `hal_<soc>` | `components/hal_<soc>/` | HAL strong 实现组件（`WHOLE_ARCHIVE`，见 [§7](#7-halesp-全屏蔽--板级-strong-实现)） |
| `driver_ws2812` | `components/driver_ws2812/` | **可选**；唯一允许厂商 RMT/`led_strip` 的产品驱动 |
| `app` | `components/app/` | `REQUIRES mini_tree`（+ 可选 `driver_ws2812`） |

依赖链：`app` → `mini_tree`（+ 可选 `driver_ws2812`）。

**一份 mini 配多 MCU**：同一份 `mini_tree`（symlink / 子模块 / 拷贝）可挂在多个板工程下；每个工程自带 `board_port.cmake` + `board_*` + `hal_*`。也可设 `MINI_TREE_BOARD_PORT` 指向任意注入文件。

**不要**再设 `EXTRA_COMPONENT_DIRS` 指向旧的 `components/driver/`（已废弃）。

---

## 3. board_port.cmake 注入契约

该文件回答"这个工程是谁"：板级 DTS 在哪、芯片宏是什么、树外驱动在哪。**所有变量都由 mini_tree 侧消费**（消费方已标注）：

| 变量 | 消费方 | 作用 |
| :--- | :--- | :--- |
| `BOARD_DTS` | esp_idf.cmake → dtc-lite | 板级设备树主文件；不设则用占位板（可编过，无板级节点） |
| `BOARD_DTSI_DIR` | esp_idf.cmake | dtsi 片段目录（`file(GLOB .../*.dtsi)` 作 DEPENDS，变更即重跑 dts） |
| `MINI_TREE_DTC_EXTRA_ARGS` | mini_tree/CMakeLists.txt(ESP 分支) → DTC_LITE_ARGS | 芯片专属 `-I`（IDF 头，`IS_DIRECTORY` 防御）+ 目标宏（`-DCONFIG_IDF_TARGET_<chip>=1`，让 dtsi 的 `#ifdef` 生效）。基础 `-I mini_tree/board`（dt-bindings 搜索）由 mini_tree 默认提供，这里**只追加**芯片项 |
| `MINI_TREE_DTC_EXTRA_SCAN_DIRS` | esp_idf.cmake → `_DTC_SCAN_DIRS` | 树外产品驱动目录（dtc-lite 扫 `DRIVER_REGISTER` 生成 probe 表） |
| `MINI_TREE_DTC_EXTRA_DEPENDS` | esp_idf.cmake → `_DTC_DEPENDS` | 这些文件变更时 dts 生成脚本自动重跑 |

换 MCU = 改这份文件（或设 `MINI_TREE_BOARD_PORT` 指向别的注入文件），mini_tree 本身不动。

### 3.1 如何编写（实操模板）

以 `platform/Espressif/esp32s3/components/board_port.cmake` 为蓝本（带 `<chip>` 标注处即**换芯片要改的位置**）：

```cmake
# SPDX-License-Identifier: Apache-2.0
# 板级注入 — 回答"这个工程是谁"。由 mini_tree/CMakeLists.txt (ESP_PLATFORM) include。
# 换 MCU = 改本文件; 也可以不改文件, 设 MINI_TREE_BOARD_PORT 指向别的注入文件。

# ── ① 路径基准 (固定写法, 换芯片不用改) ────────────────────────────────
get_filename_component(_BOARD_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)   # components/
get_filename_component(_BOARD_DIR  "${_BOARD_ROOT}/board_<soc>" ABSOLUTE)  # ← 你的板级 dts 目录

# ── ② 板级设备树 ───────────────────────────────────────────────────────
set(BOARD_DTS      "${_BOARD_DIR}/dts/board.dts")   # 主 dts (dtc-lite 入口)
set(BOARD_DTSI_DIR "${_BOARD_DIR}/dtsi")            # dtsi 片段目录
file(GLOB MINI_TREE_BOARD_DTSI "${BOARD_DTSI_DIR}/*.dtsi")   # 变更即重跑 dts

# ── ③ 芯片专属 dtc 参数 ────────────────────────────────────────────────
# 基础 "-I mini_tree/board" (dt-bindings 搜索) 由 mini_tree 默认提供, 这里只追加芯片项。
if(DEFINED ENV{IDF_PATH})
    foreach(_d
        "$ENV{IDF_PATH}/components/esp_hal_gpio/<chip>/include"   # ← 换成你的芯片
        "$ENV{IDF_PATH}/components/soc/<chip>/include"            # ← 换成你的芯片
    )
        if(IS_DIRECTORY "${_d}")        # IDF 目录结构随版本变, 存在才加 (跳过不影响构建)
            list(APPEND MINI_TREE_DTC_EXTRA_ARGS "-I${_d}")
        endif()
    endforeach()
    # 目标宏: 让 dtsi 里的 #ifdef CONFIG_IDF_TARGET_<CHIP> 分支生效
    list(APPEND MINI_TREE_DTC_EXTRA_ARGS
        "-DCONFIG_IDF_TARGET_<CHIP>=1"   # ← 换成你的芯片 (全大写, 与 dtsi 里 #ifdef 一致)
        "-DIDF_TARGET_<CHIP>=1")
endif()

# ── ④ 树外产品驱动 (没有就不写这两行) ───────────────────────────────────
file(GLOB _OUT_DRV_SRCS "${_BOARD_ROOT}/driver_xxx/src/*.c")     # ← 你的树外驱动
set(MINI_TREE_DTC_EXTRA_SCAN_DIRS "${_BOARD_ROOT}/driver_xxx/src")   # dtc 扫 DRIVER_REGISTER
set(MINI_TREE_DTC_EXTRA_DEPENDS  ${MINI_TREE_BOARD_DTSI} ${_OUT_DRV_SRCS} "${BOARD_DTS}")
```

**编写检查单**（从零建新 ESP 板级工程时逐项过）：

1. ① 段固定；② 段的 `board_<soc>` 目录名改成你的（`board_esp32c6` 等）。
2. ③ 段芯片头路径：每行一个 `components/<driver>/<chip>/include`；`IS_DIRECTORY` 防御是**故意的**——不同 IDF 版本目录结构不同，缺了跳过即可，不要改成硬错误。
3. ③ 段目标宏：`CONFIG_IDF_TARGET_<CHIP>` 必须**全大写**且与 dtsi 里 `#ifdef` 拼写一致（如 `CONFIG_IDF_TARGET_ESP32S3`）；`IDF_TARGET_<CHIP>` 是另一种写法，dtsi 用哪个就跟哪个。
4. ④ 段：没有树外驱动就**不要**写 `MINI_TREE_DTC_EXTRA_SCAN_DIRS`（esp_idf.cmake 会用默认扫描集）。
5. 别忘了 `sdkconfig.defaults` 里的平台声明 `CONFIG_PLATFORM_ESP32=y`（见 [§5](#5-kconfig-双轨与平台声明)）。
6. HAL 实现加在 `hal_<soc>` 组件（见 [§7](#7-halesp-全屏蔽--板级-strong-实现)），**本文件不用动**——board_port.cmake 与 HAL 组件是解耦的。

---

## 4. 组件内 CMake 要点

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

> `SRCS` 含 GLOB 到的 `drivers/*/src/*.c` 与 vfs/bus；`INCLUDE_DIRS` 含产品驱动 include/src 与生成目录。其后对 **`${COMPONENT_LIB}`** 挂 gen 依赖、`CONFIG_OSAL_*`、`error_symbols.ld`。

**HAL stub 的编译屏蔽在源码层**（`#if defined(ESP_PLATFORM)`），故 `HAL_SRCS` 保留全量列表，`esp_idf.cmake` 不需要按芯片裁剪源文件列表。细节见 [§7](#7-halesp-全屏蔽--板级-strong-实现)。

---

## 5. Kconfig 双轨与平台声明

| 轨道 | 作用 |
| :--- | :--- |
| 组件根 `Kconfig` | 出现在 `idf.py menuconfig` → Component config → mini_tree |
| `sdkconfig` | `idf.py menuconfig` 生成；业务 `CONFIG_*` 以 `sdkconfig.h` 为准 |

ESP 构建下生成的 `config.h` 仅转发 `sdkconfig.h`；业务 `CONFIG_*` 以 `sdkconfig.h` 为准。

`cmake/esp_idf.cmake` 按 `sdkconfig.h` 软编码：`CONFIG_USB` 未显式 `# ... is not set` 时编入 `bus/usb`、`vfs/usb`、`hal/usb`（板级需 usb_tusb_port glue 并自行 REQUIRE `esp_tinyusb`）；裁剪则全部不编入。ESP 板若暂无 glue，`sdkconfig` 应保持 `# CONFIG_USB is not set`。

### 平台声明：`CONFIG_PLATFORM_ESP32`

Platform 仅保留 `PLATFORM_ESP32`（对应 `CONFIG_PLATFORM_ESP32`，默认开启）。本分支平台固定 ESP32，无需在 `sdkconfig.defaults` 显式切换（旧模板里的 `CONFIG_PLATFORM_ARM_CM4F` 已移除）：

- 编译行为由 IDF 自身管理（`ESP_PLATFORM` 编译宏 + `cmake/esp_idf.cmake`），本选项**不直接驱动编译**；
- 它标识"本工程是 ESP 平台"，与 HAL 全屏蔽机制配套（[§7](#7-halesp-全屏蔽--板级-strong-实现)）；
- 仅 `OSAL_NULL`（裸机后备）场景的调度器 tick 与平台无关；无 ARM / RISC-V 平台选项。

---

## 6. DTS 与生成物

| 项 | ESP 参考做法 |
| :--- | :--- |
| `BOARD_DTS` | 由 `board_port.cmake` 注入（例：`board_esp32s3/dts/board.dts`）；中间件默认仅为占位（`mini-tree,placeholder`，可编过但**无板级节点**） |
| 生成目录 | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree` |
| dtc-lite | 扫 vfs/bus + **全部** `drivers/*/src` + `MINI_TREE_DTC_EXTRA_SCAN_DIRS`；`-I mini_tree/board` 解析 `dt-bindings/`；芯片头/宏走 `MINI_TREE_DTC_EXTRA_ARGS` |

**勿**把板级 `dtsi/` 放进 dtc `-I`：会被当成厂商头走 cpp 抽宏而不内联，结果只剩根节点（`devices: 1`）。dtsi 靠 `BOARD_DTS` 所在 `board_dir` 自动解析。

**勿**在 `cmake/esp_idf.cmake` 写死某一 `IDF_TARGET` 的 `soc/<chip>/include`。

公共头规则不变：**产品驱动禁止** `#include` 厂商 SDK（`driver_ws2812` 除外）。

---

## 7. HAL：ESP 全屏蔽 + 板级 strong 实现

### 机制（shelf 侧已就位，移植时无需改动）

每个 `hal/<name>/hal_<name>.c` weak stub 文件顶部统一为：

```c
#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件提供 strong 实现,
 * 缺失直接链接报错, 杜绝静默 -ENOSYS。 */
#else
COMPAT_WEAK int hal_xxx(/*...*/) { return VFS_ERR_NOTSUPP; }
/* ... 其余 stub 函数 ... */
#endif /* ESP_PLATFORM */
```

- **ESP 构建**：stub 文件编译为空 → bus/vfs 对 `hal_*` 的引用全是**强引用** → 板级漏实现 → **链接直接报错**（`undefined reference to hal_xxx`），错误信息会列出缺失符号

### 为什么不能靠 weak/strong 覆盖

ESP-IDF 是组件化构建，一切代码都在静态库里、链接时**按需提取**。mini_tree 组件内的 weak stub 已满足 `spi_bus.c` 等的引用后，链接器**不会**再从板级组件库提取 strong 覆盖实现——整库 0 提取，所有 HAL 静默落到 stub，probe 返回 `-ENOSYS`（`VFS_ERR_NOTSUPP`），症状极难排查。

IDF 组件化剥夺了"板级 `target_sources` 直接编入最终 elf（强制全链接，strong 天然压 weak）"的自由目标，故必须在源码层屏蔽 stub。

### 板级组件（如参考实现 `hal_esp32s3`）移植时必须做三件事

1. **实现** ESP 构建引用的全部 `hal_*`。当前引用集：外设 7 个（`gpio/spi/uart/i2c/can/tim/adc`）+ 系统垫底 5 个（`iwdg/storage/flash/usb/platform_safety`，可先返回失败占位）。注意 `hal_cpu_secondary_startup` 在 `CONFIG_CPU_CORES > 1` 时才被引用，设双核 AMP 前先实现它。
2. **`idf_component_register(WHOLE_ARCHIVE ...)`**：mini_tree 库的 `.o` 在链接行**第二轮**才被提取（循环依赖重排），产生的 `hal_*` undefined 晚于板级库的常规扫描点；WHOLE_ARCHIVE 让 IDF 把板级库追加到链接行**末尾**再扫一次。去掉它症状是 `undefined reference to hal_spi_*`。
3. **`board_port.cmake` 注入**工程特有信息（板级 DTS 路径、IDF 芯片头/目标宏、树外驱动目录）——见 [§3](#3-boardportcmake-注入契约)。

> USB HAL 特殊：`hal/usb/hal_usb.h` 对未实现时的调用方做 `#pragma GCC poison`，实现方须在 include 前 `#define HAL_USB_IMPL`。

---

## 8. 产品驱动路径（GLOB）

统一布局（共 37 个产品驱动）：

```text
mini_tree/drivers/<chip>/
├── include/     # 对外头（ioctl / regs / bridge）
└── src/         # *.c；可含私有 .h
```

| 构建入口 | 源文件 | Include | dtc-lite 扫描 |
| :--- | :--- | :--- | :--- |
| `cmake/esp_idf.cmake` | `drivers/*/src/*.c` | `drivers/*/include` + `drivers/*/src` | `_PRODUCT_DRV_SRC_DIRS` |
| `compile_flags.txt` | — | 全部 `-Idrivers/*/include`（及含头的 `src`） | — |

树外例外：`components/driver_ws2812/src` 仅通过 `MINI_TREE_DTC_EXTRA_SCAN_DIRS` / `EXTRA_DEPENDS` 扫入 dtc；源文件仍编在 `driver_ws2812` 组件（`WHOLE_ARCHIVE`）。

旧 `drivers/flash`（`winbond,w25q64`）已删除；Flash 用 `drivers/w25qxx`（`winbond,w25qxx`）。

---

## 9. 依赖与 ETL

`lib/` 现状：随仓 vendor 仅 **ETL**；其余积木（TinyUSB、cJSON、LVGL 等）走 **ESP-IDF 组件体系**（`idf_component.yml` / registry），不再使用 FetchContent / `mini_tree_link_*`。

- ESP 驱动：写在 `idf_component_register(... REQUIRES …)`。
- 第三方组件：经 `idf_component.yml` 声明（如 `esp_tinyusb`、`lvgl`、`esp_lvgl_port`），由 IDF Component Manager 托管。
- ETL：随仓 vendor（`lib/etl`，仅 include），无需拉取；**不要**把 `managed_components` 塞进 `EXTRA_COMPONENT_DIRS`。
- FreeRTOS：IDF 自带；`CONFIG_OSAL_FREERTOS` 对接 IDF 内核，无 vendored 副本。

---

## 10. 与主仓库同步

本分支（ESP 组件）与主仓库 mini_tree 的通用层应对齐。

建议：

1. 在 **ESP 板工程**完成产品驱动 / DTS / CMake 验收（`idf.py build`）。
2. **内容同步到主仓库**（`rsync` 添加/覆盖，**不要** `--delete` 掉主仓库独有内容）。
3. 反向：主仓库改通用层后再拷回板工程时，保留板级 `hal_esp32s3`、根工程 `CMakeLists.txt`、`driver_ws2812`、`board_port.cmake`。

本分支是纯 ESP 组件，根 `CMakeLists.txt` 走 `cmake/esp_idf.cmake`，无双路径入口。

---

## 11. 验收清单

- [ ] 无过时 `EXTRA_COMPONENT_DIRS=…/components/driver`
- [ ] `idf.py build` 生成 `board_probe.c` / `dt_config_gen.h`，产品驱动 `DRIVER_REGISTER` 均已匹配
- [ ] `drivers/*/src/*.c` GLOB 覆盖全部 37 个产品驱动，无逐文件列表
- [ ] `app` `REQUIRES mini_tree`（+ 需要时 `driver_ws2812`）
- [ ] `drivers/flash` 不存在；Flash 节点为 `winbond,w25qxx`
- [ ] OSAL 与 IDF FreeRTOS 一致，无双内核
- [ ] `hal/*.c` stub 均带 `#if defined(ESP_PLATFORM)` 屏蔽（ESP 构建不编 stub）
- [ ] 板级 `hal_<soc>` 组件带 `WHOLE_ARCHIVE`，实现 ESP 构建引用的全部 `hal_*`
- [ ] `CONFIG_PLATFORM_ESP32` 默认开启（平台固定 ESP，无需显式声明）

---

## 相关文档

- [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md)（ESP-IDF VFS 心智对照）
