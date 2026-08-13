# mini_tree Build Tools

| Item / Item | Content / Content |
| :--- | :--- |
| **Audience / 读者** | People integrating builds or changing the toolchain / 集成构建或改工具链的人 |
| **Related / 相关** | [driver_guide.md](../driver_guide.md) · [getting_started.md](../getting_started.md) · [esp_idf_cmake.md](../esp_idf_cmake.md) |

---
## Contents

---
## 1. dtc-lite / dtc-lite

A **compile-time** DeviceTree compiler for MCUs: turns a board DTS into C headers/sources and scans for `DRIVER_REGISTER`.

```bash
python3 tools/dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...] [-I <include_dir> ...] [-D NAME[=VALUE] ...]
```

`-I <dir>` appends vendor-header search dirs (so dtc can cpp-expand `#include <dt-bindings/...>` and chip macros); `-D NAME[=VALUE]` appends preprocessor macros. On the ESP path these are injected by `board_port.cmake` via `MINI_TREE_DTC_EXTRA_ARGS` (see [esp_idf_cmake.md](../esp_idf_cmake.md) §3).

### Dependency

```bash
pip install lark
```

### Package Layout (`tools/dtc_lite/`)

| Module / 模块 | Responsibility / 职责 |
| :--- | :--- |
| `grammar.py` | Lark grammar / Lark 文法 |
| `parser.py` | parse tree → AST |
| `dts_ast.py` | `DtsNode` / `DtsProperty` |
| `compiler.py` | `#include`, overlay merging, driver scan / `#include`、overlay 合并、驱动扫描 |
| `generator.py` | generates `board_*` / `dt_config_gen.h` / 生成 `board_*` / `dt_config_gen.h` |
| `platform.py` | platform-specific preprocessing / macro-expansion helpers / 平台相关预处理 / 宏展开辅助 |
| `main.py` | CLI entry / CLI 入口 |

### Outputs (Typical)

`board_nodes.h`、`board_devtable.h/.c`、`board_probe.c`、`board_handles.h`、`dt_config_gen.h`.

---
## 2. scrubber CRC stub / Scrubber CRC Stub

```c
#define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000
```

`tools/system_scrubber_crc_stub.h` is copied by CMake into the generated directory as `system_scrubber_crc_gen.h`; a board-level script can override the real CRC baseline after linking.

---
## Relationship with CMake

On the ESP path, `cmake/esp_idf.cmake` automatically, when building the `mini_tree` component:

1. invokes dtc-lite (passing the vfs/bus/drivers scan paths plus chip `-I/-D`)
2. generates the `config.h` forwarder (to `sdkconfig.h`)
3. copies the scrubber stub

> This branch is configured entirely through ESP-IDF Kconfig (`idf.py menuconfig`); the standalone `genconfig.py` / `menuconfig.py` / `tools/_vendor` are removed.

---
## Related Documents

- [esp_idf_cmake.md](../esp_idf_cmake.md) · [driver_guide.md](../driver_guide.md) · [getting_started.md](../getting_started.md)
- [faq.md](../faq.md) · [file_index.md](../file_index.md)
