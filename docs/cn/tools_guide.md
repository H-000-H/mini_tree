# mini_tree 构建工具

> `dtc-lite` 与 scrubber CRC stub 的用法与模块说明。
> Usage and module notes for `dtc-lite` and the scrubber CRC stub.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 集成构建或改工具链的人 / People integrating builds or changing the toolchain |
| **相关 / Related** | [driver_guide.md](../driver_guide.md) · [getting_started.md](../getting_started.md) · [esp_idf_cmake.md](../esp_idf_cmake.md) |

## 目录

1. [dtc-lite / dtc-lite](#1-dtc-lite-dtc-lite)
2. [scrubber CRC stub / Scrubber CRC Stub](#2-scrubber-crc-stub-scrubber-crc-stub)
3. [与 CMake 的关系 / Relationship with CMake](#3-与-cmake-的关系-relationship-with-cmake)

## 1. dtc-lite / dtc-lite

MCU **编译期** DeviceTree 编译器：把板级 DTS 变成 C 头/源，并扫描 `DRIVER_REGISTER`。
A **compile-time** DeviceTree compiler for MCUs: turns a board DTS into C headers/sources and scans for `DRIVER_REGISTER`.

```bash
python3 tools/dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...] [-I <include_dir> ...] [-D NAME[=VALUE] ...]
```

`-I <dir>` 追加厂商头搜索目录（供 dtc 经 cpp 展开 `#include <dt-bindings/...>` 与芯片宏）；`-D NAME[=VALUE]` 追加预处理宏定义。ESP 路径下这些由 `board_port.cmake` 经 `MINI_TREE_DTC_EXTRA_ARGS` 注入（见 [esp_idf_cmake.md](../esp_idf_cmake.md) §3）。

### 依赖

```bash
pip install lark
```

### 包结构（`tools/dtc_lite/`）/ Package Layout (`tools/dtc_lite/`)

| 模块 / Module | 职责 / Responsibility |
| :--- | :--- |
| `grammar.py` | Lark 文法 / Lark grammar |
| `parser.py` | parse tree → AST |
| `dts_ast.py` | `DtsNode` / `DtsProperty` |
| `compiler.py` | `#include`、overlay 合并、驱动扫描 / `#include`, overlay merging, driver scan |
| `generator.py` | 生成 `board_*` / `dt_config_gen.h` / Generates `board_*` / `dt_config_gen.h` |
| `platform.py` | 平台相关预处理 / 宏展开辅助 / Platform-specific preprocessing / macro-expansion helpers |
| `main.py` | CLI 入口 / CLI entry |

### 输出（常见）/ Outputs (Typical)

编写契约见 [driver_guide.md](../driver_guide.md)。
See [driver_guide.md](../driver_guide.md) for the authoring contract.

## 2. scrubber CRC stub / Scrubber CRC Stub

`tools/system_scrubber_crc_stub.h` 在 CMake 中拷贝为生成目录里的 `system_scrubber_crc_gen.h`，提供：
`tools/system_scrubber_crc_stub.h` is copied by CMake into the generated directory as `system_scrubber_crc_gen.h`, providing:

```c
#define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000
```

链接后可用板级脚本覆盖真实 CRC 基线。
After linking, a board-level script can override the real CRC baseline.

## 3. 与 CMake 的关系

ESP 路径下 `cmake/esp_idf.cmake` 在构建 `mini_tree` 组件时自动：
On the ESP path, `cmake/esp_idf.cmake` automatically, when building the `mini_tree` component:

1. 调用 dtc-lite（并传入 vfs/bus/drivers 扫描路径与芯片 `-I/-D`）/ invokes dtc-lite (passing the vfs/bus/drivers scan paths plus chip `-I/-D`)
2. 生成 `config.h` 转发头（指向 `sdkconfig.h`）/ generates the `config.h` forwarder (to `sdkconfig.h`)
3. 拷贝 scrubber stub / copies the scrubber stub

> 本分支配置完全走 ESP-IDF Kconfig（`idf.py menuconfig`），不再有独立的 `genconfig.py` / `menuconfig.py` / `tools/_vendor`（已移除）。

## 相关文档

- [esp_idf_cmake.md](../esp_idf_cmake.md) · [driver_guide.md](../driver_guide.md) · [getting_started.md](../getting_started.md)
