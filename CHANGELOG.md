# Changelog / 变更记录

> 记录中间件 shelf 用户可见的变化。更细的设计动机见 [docs/design_decisions.md](docs/design_decisions.md)。  
> User-visible changes to the middleware shelf. Deeper design rationale: [docs/design_decisions.md](docs/design_decisions.md).

---

## [Unreleased] / 未发布

### 产品驱动与布局 / Product Drivers & Layout

- 产品驱动（37 个）迁入 `drivers/<chip>/{include,src}`（GLOB 编入 `mini_tree`）；**不再**使用独立 `components/driver_*`（除 ws2812）  
  The 37 product drivers moved to `drivers/<chip>/{include,src}` (GLOB-compiled into `mini_tree`); standalone `components/driver_*` is no longer used (except ws2812).
- 删除旧示例 `drivers/flash`（`winbond,w25q64`）；SPI NOR 统一走 `drivers/w25qxx`（`winbond,w25qxx`）  
  Removed the old sample `drivers/flash` (`winbond,w25q64`); SPI NOR unified under `drivers/w25qxx` (`winbond,w25qxx`).
- **板级 DTS/DTSI 外置**：经 `board_port.cmake` 注入；中间件仅保留占位 `board/dts/board.dts` 与通用 `dt-bindings/`  
  Board DTS/DTSI externalized: injected via `board_port.cmake`; the middleware keeps only the placeholder `board/dts/board.dts` and generic `dt-bindings/`.
- **一份 mini 配多 MCU**：中间件不硬编码 `board_*` / `IDF_TARGET`；每板自带 `board_port` + `board_*` + `hal_*`  
  One mini tree, many MCUs: no hardcoded `board_*` / `IDF_TARGET`; each board ships its own `board_port` + `board_*` + `hal_*`.
- ESP 板工程：`components/driver_ws2812` 为唯一允许厂商 RMT/`led_strip` 的例外；`app` 仅 `REQUIRES mini_tree` + `driver_ws2812`  
  ESP board projects: `components/driver_ws2812` is the only exception allowed to use vendor RMT/`led_strip`; `app` only `REQUIRES mini_tree` + `driver_ws2812`.
- CMake：`file(GLOB drivers/*/src/*.c)` + `drivers/*/include|src` 进 `INCLUDE_DIRS` / dtc-lite 扫描（`CMakeLists.txt`、`cmake/esp_idf.cmake`、`board/CMakeLists.txt`）  
  CMake: `file(GLOB drivers/*/src/*.c)` plus `drivers/*/include|src` into `INCLUDE_DIRS` / dtc-lite scan (`CMakeLists.txt`, `cmake/esp_idf.cmake`, `board/CMakeLists.txt`).
- `compile_flags.txt` 补齐全部产品驱动 `-Idrivers/*/include`（及含头的 `src/`）  
  `compile_flags.txt` now covers `-Idrivers/*/include` for every product driver (and `src/` where it holds headers).

### 架构与代码 / Architecture & Code

- HAL 全面 weak 空实现；默认 `board/dts/board.dts` 为通用占位  
  HAL is fully weak empty implementations; the default `board/dts/board.dts` is a generic placeholder.
- Bus/VFS 覆盖 gpio/spi/uart/i2c/i2s/can/usb/adc/dac/tim/rtc/iwdg/wwdg 等  
  Bus/VFS covers gpio/spi/uart/i2c/i2s/can/usb/adc/dac/tim/rtc/iwdg/wwdg, etc.
- USB 经 TinyUSB + 板级 `usb_tusb_port` 约定  
  USB goes through TinyUSB plus the board-level `usb_tusb_port` convention.
- clangd：`compile_flags.txt` + `ide/stubs`；禁止子目录覆盖  
  clangd: `compile_flags.txt` + `ide/stubs`; per-directory overrides are forbidden.
- ETL 明确为 **上层 C++ 基础**：源码在 `lib/etl`，根 CMake 默认进 `mini_tree`  
  ETL is the **C++ foundation for upper layers**: sources live in `lib/etl` and are linked into `mini_tree` by default from the root CMake.
- 开源积木：`lib/` 仅 vendor FreeRTOS / RT-Thread / ETL；TinyUSB / lwIP / cJSON 等配置期 FetchContent；其余积木链接期按需 FetchContent（`cmake/dep_fetch.cmake`，本地优先）  
  Open-source components: `lib/` vendors only FreeRTOS / RT-Thread / ETL; TinyUSB / lwIP / cJSON etc. are FetchContent'd at configure time, the rest at link time on demand (`cmake/dep_fetch.cmake`, local-first).

### 代码风格与命名 / Code Style & Naming

- 新增 `.clang-format`：Allman 大括号、单语句 if/for/while 去大括号、4 空格缩进、100 列、指针靠左  
  New `.clang-format`: Allman braces, no braces around single-statement if/for/while, 4-space indent, 100 columns, pointer-on-left.
- 新增分层 `.clang-tidy`（readability-identifier-naming）：根 = 内核区（app 以下非 cpp，全小写无前缀）；`app/` 与 `system_cpp/` = Google 区（类型 PascalCase + s_/g_/k_ 前缀）；宏全大写（container_of / likely / IS_ERR / COMPAT_* / `__XXX_H__` 头文件卫士例外）  
  New layered `.clang-tidy` (readability-identifier-naming): root = kernel zone (non-cpp below app/, all-lowercase, no prefix); `app/` and `system_cpp/` = Google zone (PascalCase types + s_/g_/k_ prefixes); macros all-uppercase (exceptions: container_of / likely / IS_ERR / COMPAT_* / `__XXX_H__` header guards).
- 新增 `.clang-format-ignore`：格式化排除 `lib/`  
  New `.clang-format-ignore`: formatting excludes `lib/`.
- 命名统一（clang-tidy 全量扫描清零）：`kTag`→`k_tag`、`struct Event/Subscriber`→`event/subscriber`、`namespace MiniTree`→`mini_tree`、`System_Pre_OS_Init`→`system_pre_os_init`、`xTask/xScheduler/ListNode`→`x_task/x_scheduler/list_node`、`Fifo_Data_type`→`fifo_data_type`、C++ 侧 `getInstance/registerCmd/kMaxCmdNameLen`→小写 等  
  Naming unified (full clang-tidy scan clean): `kTag`→`k_tag`, `struct Event/Subscriber`→`event/subscriber`, `namespace MiniTree`→`mini_tree`, `System_Pre_OS_Init`→`system_pre_os_init`, `xTask/xScheduler/ListNode`→`x_task/x_scheduler/list_node`, `Fifo_Data_type`→`fifo_data_type`, C++-side `getInstance/registerCmd/kMaxCmdNameLen`→lowercase, etc.
- `tools/gen_compile_db.py`：补头文件条目与 include 目录（clang-tidy / clangd 可对头文件单独检查）  
  `tools/gen_compile_db.py`: added header entries and include directories (clang-tidy / clangd can now check headers on their own).

### 构建修复（通用 CMake 路径，最小构建实测通过） / Build Fixes (generic CMake paths, minimal build verified)

- `core/include/status.h`：补 `#include <stddef.h>`（`NULL` 未声明）  
  `core/include/status.h`: added `#include <stddef.h>` (`NULL` was undeclared).
- `vfs/gpio/vfs-gpio.c`：`DTC_GEN_COUNT_HETEROGENEOUS_GPIOS` 补 `#ifndef` 保护  
  `vfs/gpio/vfs-gpio.c`: `DTC_GEN_COUNT_HETEROGENEOUS_GPIOS` guarded with `#ifndef`.
- `time_slice/task/xtask.c`：`CHOSEN_SCHEDULER_TIM` 补 `#ifndef` 保护  
  `time_slice/task/xtask.c`: `CHOSEN_SCHEDULER_TIM` guarded with `#ifndef`.
- `cmake/tinyusb.cmake`：本地未提供 `src/CMakeLists.txt` 时 TinyUSB 核心源置空（离线不报错，详见 [docs/ecosystem.md](docs/ecosystem.md)）  
  `cmake/tinyusb.cmake`: TinyUSB core sources are emptied when local `src/CMakeLists.txt` is missing (no offline failure; see [docs/ecosystem.md](docs/ecosystem.md)).

### 文档 / Documentation

- 刷新 [esp_idf_cmake.md](docs/esp_idf_cmake.md) / [driver_guide.md](docs/driver_guide.md) / [file_index.md](docs/file_index.md)：产品驱动目录、ws2812 例外、与 ESP 板同步说明  
  Refreshed [esp_idf_cmake.md](docs/esp_idf_cmake.md) / [driver_guide.md](docs/driver_guide.md) / [file_index.md](docs/file_index.md): product driver layout, ws2812 exception, ESP board sync notes.
- 新增 [docs/ecosystem.md](docs/ecosystem.md)：积木型生态、Fetch 策略与致谢  
  Added [docs/ecosystem.md](docs/ecosystem.md): component ecosystem, fetch policy, acknowledgments.
- 专题文档对齐 hybrid 依赖：去掉过时 `ide/third_party/etl`；交叉链接 ecosystem；刷新 `overview.html`  
  Topic docs aligned with hybrid dependencies: removed stale `ide/third_party/etl`; cross-linked ecosystem; refreshed `overview.html`.
- 根 [README.md](README.md) 中文改版（简介 / 特性 / 快速开始）  
  Root [README.md](README.md) rewritten in Chinese (intro / features / quick start).
- 专题文档统一放到 `docs/`；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING`  
  Topic docs consolidated into `docs/`; the root keeps only `README` / `CHANGELOG` / `CONTRIBUTING`.
- 新增 [docs/README.md](docs/README.md) 作为文档目录页  
  Added [docs/README.md](docs/README.md) as the documentation index page.
- 原根目录 `NOTICE.md` → [docs/design_decisions.md](docs/design_decisions.md)；新增 Apache 惯例 [NOTICE](NOTICE)  
  Root `NOTICE.md` moved to [docs/design_decisions.md](docs/design_decisions.md); Apache-conventional [NOTICE](NOTICE) added.
- 补充 [docs/references.md](docs/references.md) 与作者偏好取舍  
  Added [docs/references.md](docs/references.md) and author preference trade-offs.
- 补缺口专题：USB / 外设 / AMP / can_hook / 运行时服务；ESP-IDF CMake 见 [esp_idf_cmake](docs/esp_idf_cmake.md)  
  Filled doc gaps: USB / peripherals / AMP / can_hook / runtime services; ESP-IDF CMake see [esp_idf_cmake](docs/esp_idf_cmake.md).
- 不推荐 Keil 作主 IDE；推荐 Cursor / VS Code / CLion / Qoder  
  Keil is not recommended as the primary IDE; Cursor / VS Code / CLion / Qoder are recommended instead.
- [NOTICE](NOTICE) 全面重写：补全各组件版本号、版权人、SPDX 标识、合规提示（LGPL/双许可/ChaN）  
  [NOTICE](NOTICE) fully rewritten: complete component versions, copyright holders, SPDX IDs, compliance notes (LGPL / dual-license / ChaN).
- [LICENSE](LICENSE) APPENDIX 填充实际版权行  
  [LICENSE](LICENSE) APPENDIX filled with the actual copyright lines.
- [README.md](README.md) 许可证节增加商用合规要点（libmodbus LGPL / Mbed TLS 双许可 / FatFs）  
  [README.md](README.md) license section gained commercial compliance points (libmodbus LGPL / Mbed TLS dual-license / FatFs).
- [CONTRIBUTING.md](CONTRIBUTING.md) 新增 SPDX 头规范与 NOTICE 同步要求  
  [CONTRIBUTING.md](CONTRIBUTING.md) gained the SPDX header spec and NOTICE sync requirements.
- [docs/README.md](docs/README.md) 导航表新增「合规 / 许可证」入口  
  [docs/README.md](docs/README.md) nav table gained a "Compliance / License" entry.
- [docs/peripherals.md](docs/peripherals.md) §5 新增 RS485 Modbus RTU 驱动条目  
  [docs/peripherals.md](docs/peripherals.md) §5 added an RS485 Modbus RTU driver entry.
- [docs/driver_guide.md](docs/driver_guide.md) §7 修正 `device_lc_bind` 说明（产品驱动由框架统一绑定）  
  [docs/driver_guide.md](docs/driver_guide.md) §7 corrected the `device_lc_bind` notes (product drivers are bound uniformly by the framework).

### 仓库卫生 / Repo Hygiene

- 补 [LICENSE](LICENSE)（Apache-2.0）  
  Added [LICENSE](LICENSE) (Apache-2.0).
- 补 [.gitignore](.gitignore)（对齐 Heterogeneous-Multicore / ST 板工程共用规则）  
  Added [.gitignore](.gitignore) (aligned with the shared rules of Heterogeneous-Multicore / ST board projects).

### 配置 / Configuration

- OSAL：FreeRTOS V11.3.0 / RT-Thread v5.3.0 / NULL+xtask  
  OSAL: FreeRTOS V11.3.0 / RT-Thread v5.3.0 / NULL+xtask.
- SYSTEM_C / SYSTEM_CPP 二选一  
  SYSTEM_C / SYSTEM_CPP — choose one.

---

## [Historical] / 历史

多轮重构（设备树、硬件直投、OSAL、安全回路、文档迁徙等）详见 [docs/design_decisions.md](docs/design_decisions.md)。  
Multiple refactoring rounds (device tree, direct hardware mapping, OSAL, safety loops, doc migration, etc.) — see [docs/design_decisions.md](docs/design_decisions.md).  
平台验证历史以各 SoC 工程仓库为准。  
Platform verification history lives in the per-SoC project repositories.

---

## 相关文档 / Related Documents

- [docs/roadmap.md](docs/roadmap.md) · [docs/todolist.md](docs/todolist.md) · [docs/api_compatibility.md](docs/api_compatibility.md)
