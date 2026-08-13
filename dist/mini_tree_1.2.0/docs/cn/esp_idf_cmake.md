# ESP-IDF 移植指南（CMake 集成）

> 相对本仓根目录「通用 CMake」（`add_subdirectory` + `add_library(mini_tree STATIC)`），ESP32 走 **IDF 组件** 路径。参考实现：`platform/Espressif/esp32s3/`（根 `CMakeLists.txt` `EXTRA_COMPONENT_DIRS` 注册偏好 + `components/board_${IDF_TARGET}` + `components/hal_esp32s3` + 可选 `components/driver_ws2812`）。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 把本 shelf 接到 ESP-IDF 工程的人 |
| **前置** | [getting_started.md](getting_started.md)（通用 CMake） |
| **相关** | [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [design_decisions.md](design_decisions.md) |

---

## 目录

1. [概览：通用 CMake vs ESP-IDF](#1-概览通用-cmake-vs-esp-idf)
2. [工程结构](#2-工程结构)
3. [自动推导契约（替代 board_port.cmake）](#3-自动推导契约替代-boardportcmake)
4. [组件内 CMake 要点](#4-组件内-cmake-要点)
5. [Kconfig 双轨与平台声明](#5-kconfig-双轨与平台声明)
6. [DTS 与生成物](#6-dts-与生成物)
7. [HAL：ESP 全屏蔽 + 板级 strong 实现](#7-halesp-全屏蔽--板级-strong-实现)
8. [产品驱动路径（GLOB）](#8-产品驱动路径glob)
9. [依赖与 ETL](#9-依赖与-etl)
10. [与 shelf 仓库同步](#10-与-shelf-仓库同步)
11. [验收清单](#11-验收清单)

---

## 1. 概览：通用 CMake vs ESP-IDF

| 项 | 本仓通用（ST / 裸工程等） | ESP-IDF |
| :--- | :--- | :--- |
| 接入 | `add_subdirectory(mini_tree)` | 根 `CMakeLists.txt` `EXTRA_COMPONENT_DIRS` 注册偏好：优先 `managed_components/mini_tree`（vendored 副本，与 cjson/led_strip 同目录），否则回退 shelf 绝对路径（开发主链路）；`ESP_PLATFORM` 时走 `cmake/esp_idf.cmake` 并 `return()` |
| 目标 | `add_library(mini_tree STATIC)` | `idf_component_register(...)` |
| 引用生成物 | `target_* (mini_tree …)` | `target_* (${COMPONENT_LIB} …)` |
| 路径 | `CMAKE_CURRENT_LIST_DIR` | **必须**用 `CMAKE_CURRENT_LIST_DIR` / `MINI_TREE_DIR`（IDF 两阶段；勿依赖 requirements 阶段的 `SOURCE_DIR`）；`file(GLOB …)` **不要**加 `CONFIGURE_DEPENDS`（requirements 为 script 模式） |
| HAL 实现 | 板级 `target_sources(可执行目标 …)` 编入最终 elf，strong 天然压 weak | 板级独立组件 `hal_<soc>`（`WHOLE_ARCHIVE`）；shelf 内 stub 在 ESP 下编译为空（见 [§7](#7-halesp-全屏蔽--板级-strong-实现)） |
| Kconfig | 仅 `.config` + `genconfig.py` | 组件 `Kconfig` 进 `idf.py menuconfig`；ESP 下 `config.h` 常为空壳，真值走 `sdkconfig.h` |
| 链接脚本 | 板级 `-T` / `error_symbols.ld` | `target_linker_script(${COMPONENT_LIB} INTERFACE error_symbols.ld)` |

---

## 2. 工程结构

参考：`platform/Espressif/esp32s3/`（[device-platform](https://github.com/H-000-H/device-platform) 仓库内）

```cmake
# 根 CMakeLists.txt —— 仅两行, 其余全部由 components/ 自动组装
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_board)
```

```cmake
# 根 CMakeLists.txt —— mini_tree 注册偏好 (EXTRA_COMPONENT_DIRS 官方组件发现入口):
#   1) managed_components/mini_tree 存在 → 用 vendored 副本 (与 cjson/led_strip 同目录,
#      可放 git clone / 发布快照; 注意 idf.py fullclean 会删除 managed_components/)
#   2) 否则 → 回退 shelf 绝对路径 (开发主链路, 编辑即时生效)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/managed_components/mini_tree/CMakeLists.txt")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/managed_components/mini_tree")
else()
    list(APPEND EXTRA_COMPONENT_DIRS "/path/to/mini_tree")
endif()
```

| 组件 / 文件 | 路径 | 说明 |
| :--- | :--- | :--- |
| `mini_tree` | 根 `CMakeLists.txt` `EXTRA_COMPONENT_DIRS`（优先 managed 副本，回退 shelf） | **纯架构**中间件 + `drivers/*`；无 SoC 硬编码 |
| `board_<soc>` | `components/board_<soc>/` | 板级 `dts/` + `dtsi/` **真组件**（空 `idf_component_register`）；目录名必须匹配 `board_${IDF_TARGET}`，mini_tree 按此约定自动发现（见 [§3](#3-自动推导契约)） |
| `hal_<soc>` | `components/hal_<soc>/` | HAL strong 实现组件（`WHOLE_ARCHIVE`，见 [§7](#7-halesp-全屏蔽--板级-strong-实现)） |
| `driver_ws2812` | `components/driver_ws2812/` | **可选**；唯一允许厂商 RMT/`led_strip` 的产品驱动 |
| `app` | `components/app/` | `REQUIRES mini_tree`（+ 可选 `driver_ws2812`） |

依赖链：`app` → `mini_tree`（+ 可选 `driver_ws2812`）。

**一份 mini 配多 MCU**：同一份 `mini_tree` 可被多个板工程引用（`EXTRA_COMPONENT_DIRS` 偏好注册）；每个工程自带 `board_<soc>` + `hal_<soc>`。板级注入**不需要任何注入文件**——见 [§3](#3-自动推导契约)。

**不要**再设 `EXTRA_COMPONENT_DIRS` 指向旧的 `components/driver/`（已废弃）。

---

## 3. 自动推导契约（替代 board_port.cmake）

板级注入不再依赖任何"注入文件"（`board_port.cmake` / `MINI_TREE_BOARD_PORT` 已移除）。`cmake/esp_idf.cmake` 在配置期**全部自动推导**：

| 推导项 | 来源 | 说明 |
| :--- | :--- | :--- |
| 芯片 dtc `-I/-D` | `idf_build_get_property(IDF_TARGET)` | 自动拼 `components/esp_hal_gpio/<chip>/include`、`components/soc/<chip>/include`（`IS_DIRECTORY` 防御，IDF 版本变了跳过）；`-DCONFIG_IDF_TARGET_<CHIP>=1` / `-DIDF_TARGET_<CHIP>=1` 让 dtsi 的 `#ifdef` 分支生效 |
| dt-bindings 基础 `-I` | 固定 `-I mini_tree/board` | 由 esp_idf.cmake 提供；勿把 dtsi 目录塞进来 |
| 板级 DTS | 约定组件 `components/board_${IDF_TARGET}` | 组件目录内 `dts/board.dts` + `dtsi/`；未发现 → 占位板（可编过，无板级节点）；**发现但布局不符 → 配置期报错**（fail-loud，防静默占位） |
| 树外产品驱动 | 约定 `components/*/src` | dtc-lite 自动扫 `DRIVER_REGISTER` 进 probe 表；无注册宏的 `src/`（如 `hal_*`）多解析一次，无害 |
| 功能开关 | IDF `CONFIG_*`（sdkconfig） | `CONFIG_SYSTEM` / `CONFIG_EVENT_BUS` / `CONFIG_SYSTEM_CMD` / `CONFIG_USB` 均来自 `Kconfig.mini_tree`，menuconfig 可见，`sdkconfig.defaults` 可钉死（ESP 路径不再读 `.config`） |

换 MCU = 新建 `board_<新芯片>` 组件 + `hal_<新芯片>` 组件，mini_tree 本体不动。

**逃生门**：`MINI_TREE_DTC_EXTRA_ARGS` 仍可注入非标准芯片头路径（正常工程无需设置）。

### 3.1 板级组件布局（实操模板）

```text
components/board_esp32s3/
├── CMakeLists.txt        # 空 idf_component_register() —— 纯数据组件, 让 IDF 发现它
├── dts/board.dts         # 主 dts (dtc-lite 入口, 约定路径)
└── dtsi/                 # 板级/SoC dtsi 片段 (约定目录, 变更即重跑 dts)
```

```cmake
# components/board_esp32s3/CMakeLists.txt
idf_component_register()
```

**编写检查单**（从零建新 ESP 板级工程时逐项过）：

1. 目录名必须为 `board_${IDF_TARGET}`（如 `board_esp32s3`、`board_esp32c6`），否则 mini_tree 发现不到 → 占位板（可编过，无板级节点）。
2. 布局必须 `dts/board.dts` + `dtsi/`；组件发现但布局不符 → **配置期 FATAL**（有意为之，防静默降级）。
3. 芯片头/宏**不用写**——IDF_TARGET 自动推导；dtsi 里 `#ifdef CONFIG_IDF_TARGET_<CHIP>` 需**全大写**与推导宏拼写一致。
4. 树外驱动放 `components/*/src`，含 `DRIVER_REGISTER` 即自动进 probe 表。
5. 别忘了 `sdkconfig.defaults` 里的平台声明 `CONFIG_PLATFORM_ESP32=y`（见 [§5](#5-kconfig-双轨与平台声明)）。
6. HAL 实现加在 `hal_<soc>` 组件（见 [§7](#7-halesp-全屏蔽--板级-strong-实现)），与板级组件解耦。

---

## 4. 组件内 CMake 要点

`ESP_PLATFORM` 时根 `CMakeLists.txt` 仅 `include(cmake/esp_idf.cmake)`（芯片/板级/树外驱动全部在 esp_idf.cmake 内自动推导）：

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

**HAL stub 的编译屏蔽在源码层**（`#if defined(ESP_PLATFORM)`），故 `HAL_SRCS` 保留全量列表（与通用 CMake 一致），`esp_idf.cmake` 不需要按芯片裁剪源文件列表。细节见 [§7](#7-halesp-全屏蔽--板级-strong-实现)。

---

## 5. Kconfig 双轨与平台声明

| 轨道 | 作用 |
| :--- | :--- |
| 组件根 `Kconfig` | 出现在 `idf.py menuconfig` → Component config → mini_tree |
| 组件 `.config` | 非 ESP / 选源文件时用；**同步到 shelf 时默认不覆盖**板级 `.config` |

ESP 构建下生成的 `config.h` 常为占位；业务 `CONFIG_*` 以 `sdkconfig.h` 为准。

`cmake/esp_idf.cmake` 与通用 CMake 一样按 `.config` 软编码：`CONFIG_USB` 未显式 `# ... is not set` 时编入 `bus/usb`、`vfs/usb`、`hal/usb`（板级需 usb_tusb_port glue 并自行 REQUIRE `esp_tinyusb`）；裁剪则全部不编入。ESP 板若暂无 glue，`.config` 应保持 `# CONFIG_USB is not set`。

### 平台声明：`CONFIG_PLATFORM_ESP32`

Platform choice 中新增 `PLATFORM_ESP32`（对应 `CONFIG_PLATFORM_ESP32`）。**ESP-IDF 工程必须显式选中**（写入 `sdkconfig.defaults`：`CONFIG_PLATFORM_ESP32=y` / `# CONFIG_PLATFORM_ARM_CM4F is not set`）：

- 编译行为由 IDF 自身管理（`ESP_PLATFORM` 编译宏 + `cmake/esp_idf.cmake`），本选项**不直接驱动编译**；
- 它标识"本工程是 ESP 平台"，与 HAL 全屏蔽机制配套（[§7](#7-halesp-全屏蔽--板级-strong-实现)）；
- 非 ESP 裸机构建不要选它，用 ARM / RISC-V 平台选项（默认 `PLATFORM_ARM_CM4F` 仅对非 ESP 有意义）。

---

## 6. DTS 与生成物

| 项 | ESP 参考做法 |
| :--- | :--- |
| `BOARD_DTS` | 自动发现：`components/board_${IDF_TARGET}/dts/board.dts`（见 [§3](#3-自动推导契约)）；未发现板级组件时中间件默认仅为占位（`mini-tree,placeholder`，可编过但**无板级节点**） |
| 生成目录 | `${CMAKE_BINARY_DIR}/generated/{kconfig,board,scrubber}/mini_tree` |
| dtc-lite | 扫 vfs/bus + **全部** `drivers/*/src` + 工程 `components/*/src`；`-I mini_tree/board` 解析 `dt-bindings/`；芯片头/宏由 `IDF_TARGET` 推导 |

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
 * 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK int hal_xxx(/*...*/) { return VFS_ERR_NOTSUPP; }
/* ... 其余 stub 函数 ... */
#endif /* ESP_PLATFORM */
```

- **ESP 构建**：stub 文件编译为空 → bus/vfs 对 `hal_*` 的引用全是**强引用** → 板级漏实现 → **链接直接报错**（`undefined reference to hal_xxx`），错误信息会列出缺失符号
- **非 ESP 构建**：行为完全不变，weak stub 照常兜底

### 为什么不能靠 weak/strong 覆盖

ESP-IDF 是组件化构建，一切代码都在静态库里、链接时**按需提取**。mini_tree 组件内的 weak stub 已满足 `spi_bus.c` 等的引用后，链接器**不会**再从板级组件库提取 strong 覆盖实现——整库 0 提取，所有 HAL 静默落到 stub，probe 返回 `-ENOSYS`（`VFS_ERR_NOTSUPP`），症状极难排查。

裸机工程（ST/CH32）没有此问题：板级强实现用 `target_sources(可执行目标 PRIVATE ...)` 直接编入最终 elf（强制全链接，strong 天然压 weak）；IDF 组件化剥夺了这个自由目标，故必须在源码层屏蔽 stub。

### 板级组件（如参考实现 `hal_esp32s3`）移植时必须做三件事

1. **实现** ESP 构建引用的全部 `hal_*`。当前引用集：外设 7 个（`gpio/spi/uart/i2c/can/tim/adc`）+ 系统垫底 5 个（`iwdg/storage/flash/usb/platform_safety`，可先返回失败占位）。注意 `hal_cpu_secondary_startup` 在 `CONFIG_CPU_CORES > 1` 时才被引用，设双核 AMP 前先实现它。
2. **`idf_component_register(WHOLE_ARCHIVE ...)`**：mini_tree 库的 `.o` 在链接行**第二轮**才被提取（循环依赖重排），产生的 `hal_*` undefined 晚于板级库的常规扫描点；WHOLE_ARCHIVE 让 IDF 把板级库追加到链接行**末尾**再扫一次。去掉它症状是 `undefined reference to hal_spi_*`。
3. **板级组件命名 `board_${IDF_TARGET}` + 约定布局**（`dts/board.dts` + `dtsi/`）——其余（芯片头/宏、树外驱动扫描）全部自动推导，见 [§3](#3-自动推导契约)。

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
| 根 `CMakeLists.txt`（非 ESP） | 同上 | 同上 | 同上 |
| `board/CMakeLists.txt`（独立 board 库） | — | — | `../drivers/*/src` |
| `compile_flags.txt` | — | 全部 `-Idrivers/*/include`（及含头的 `src`） | — |

树外例外：`components/driver_ws2812/src` 经 `components/*/src` 约定自动扫入 dtc（见 [§3](#3-自动推导契约)）；源文件仍编在 `driver_ws2812` 组件（`WHOLE_ARCHIVE`）。

旧 `drivers/flash`（`winbond,w25q64`）已删除；Flash 用 `drivers/w25qxx`（`winbond,w25qxx`）。

---

## 9. 依赖与 ETL

`lib/` 现状：随仓 vendor 仅 **FreeRTOS（v11.3.0）、RT-Thread（v5.3.0）、ETL**；其余积木全部走 FetchContent。

- ESP 驱动：写在 `idf_component_register(... REQUIRES …)`。
- 配置期 FetchContent（根 CMake 直接 `include(cmake/*.cmake)`，local-or-fetch）：TinyUSB、lwIP、cJSON。ESP 下建议改用 IDF 组件 / Component Manager（如 `esp_tinyusb`、`managed_components`）。
- 链接期 FetchContent（按需 `mini_tree_link_*`，local-or-fetch on link）：LVGL、u8g2、littlefs、FatFs、SFUD、Mbed TLS、coreMQTT、coreHTTP、nanopb、miniz、MCUBoot、FreeModbus、libmodbus、CMSIS-DSP、MultiButton、EasyFlash、EasyLogger、FlashDB。
- ETL：随仓 vendor（`lib/etl`，仅 include + cmake），无需拉取；**不要**把 `managed_components` 塞进 `EXTRA_COMPONENT_DIRS`。
- FreeRTOS：IDF 自带；`CONFIG_OSAL_FREERTOS` 对接 IDF 内核，勿再嵌 `lib/freeRTOS`。

---

## 10. 与 shelf 仓库的关系

板工程经根 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS` **注册偏好**引用 mini_tree：`managed_components/mini_tree`（vendored 副本）优先，否则回退 shelf 绝对路径（开发主链路，编辑 shelf **即时生效**，无需同步，不存在 `components/mini_tree` 副本）。

- shelf 只放通用层（中间件 / HAL stub / drivers / cmake / docs）；板级（`board_<soc>` / `hal_<soc>` / `driver_*`）放各板工程仓库。
- **vendored 模式**（放 `managed_components/mini_tree`）：与 cjson/led_strip 同目录、同"第三方组件"外观；但它是**手动副本**，不会自动同步 shelf 改动；且 `idf.py fullclean` 会删除整个 `managed_components/`，副本需重新放置。切换偏好后需 `idf.py reconfigure` 才生效。
- 换机器/挪目录树：改根 `CMakeLists.txt` 里的回退绝对路径一处即可（本工程风格用绝对路径）。

不要把 ESP 的 `idf_component_register` 整文件覆盖本仓「非 ESP」根 `CMakeLists.txt` 逻辑（两入口靠 `if(ESP_PLATFORM)` 分支共存）。

---

## 11. 验收清单

- [ ] 无过时 `EXTRA_COMPONENT_DIRS=…/components/driver`
- [ ] `mini_tree` 经根 `CMakeLists.txt` `EXTRA_COMPONENT_DIRS` 注册（优先 `managed_components/mini_tree`，回退 shelf），`components/mini_tree` 不复存在
- [ ] `components/board_${IDF_TARGET}/` 存在且含 `CMakeLists.txt` + `dts/board.dts` + `dtsi/`
- [ ] `idf.py build` 生成 `board_probe.c` / `dt_config_gen.h`，产品驱动 `DRIVER_REGISTER` 均已匹配（含树外 `components/*/src`）
- [ ] `drivers/*/src/*.c` GLOB 覆盖全部产品驱动，无逐文件列表
- [ ] `app` `REQUIRES mini_tree`（+ 需要时 `driver_ws2812`）
- [ ] `drivers/flash` 不存在；Flash 节点为 `winbond,w25qxx`
- [ ] OSAL 与 IDF FreeRTOS 一致，无双内核
- [ ] `hal/*.c` stub 均带 `#if defined(ESP_PLATFORM)` 屏蔽（ESP 构建不编 stub）
- [ ] 板级 `hal_<soc>` 组件带 `WHOLE_ARCHIVE`，实现 ESP 构建引用的全部 `hal_*`
- [ ] `sdkconfig.defaults` 含 `CONFIG_PLATFORM_ESP32=y` + 功能开关钉死（`CONFIG_SYSTEM=y` / `CONFIG_USB=y` / `# CONFIG_EVENT_BUS is not set` / `# CONFIG_SYSTEM_CMD is not set`）
- [ ] dtc 命令行含推导出的芯片 `-I/-D` 与 `-I mini_tree/board`（见 `build/build.ninja`）

---

## 相关文档

- [getting_started.md](getting_started.md) · [device_tree_porting.md](device_tree_porting.md) · [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)
- [references.md](references.md)（ESP-IDF VFS 心智对照）
