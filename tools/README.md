# mini_tree 构建工具

> `dtc-lite`、`genconfig`、scrubber CRC stub 的用法与模块说明。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 集成构建或改工具链的人 |
| **相关** | [driver_guide.md](../docs/driver_guide.md) · [getting_started.md](../docs/getting_started.md) |

---

## 目录

1. [dtc-lite](#1-dtc-lite)
2. [genconfig.py](#2-genconfigpy)
3. [scrubber CRC stub](#3-scrubber-crc-stub)
4. [与 CMake 的关系](#4-与-cmake-的关系)

---

## 1. dtc-lite

MCU **编译期** DeviceTree 编译器：把板级 DTS 变成 C 头/源，并扫描 `DRIVER_REGISTER`。

```bash
python3 tools/dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...]
```

### 依赖

```bash
pip install lark
```

### 包结构（`tools/dtc_lite/`）

| 模块 | 职责 |
| :--- | :--- |
| `grammar.py` | Lark 文法 |
| `parser.py` | parse tree → AST |
| `dts_ast.py` | `DtsNode` / `DtsProperty` |
| `compiler.py` | `#include`、overlay 合并、驱动扫描 |
| `generator.py` | 生成 `board_*` / `dt_config_gen.h` |
| `main.py` | CLI 入口 |

### 输出（常见）

`board_nodes.h`、`board_devtable.h/.c`、`board_probe.c`、`board_handles.h`、`dt_config_gen.h`。

编写契约见 [driver_guide.md](../docs/driver_guide.md)。

---

## 2. genconfig.py

```bash
python3 tools/genconfig.py Kconfig <output_dir> --config .config
```

把 Kconfig 符号写成 `#define CONFIG_*` 的 `config.h`。需本机可用的 `kconfiglib`（或项目使用的兼容包）。

---

## 3. scrubber CRC stub

`tools/system_scrubber_crc_stub.h` 在 CMake 中拷贝为生成目录里的 `system_scrubber_crc_gen.h`，提供：

```c
#define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000U
```

链接后可用板级脚本覆盖真实 CRC 基线。

---

## 4. 与 CMake 的关系

根 `CMakeLists.txt` 在构建 `mini_tree` 时自动：

1. 调用 genconfig  
2. 调用 dtc-lite（并传入 vfs/bus/drivers 扫描路径）  
3. 拷贝 scrubber stub  

手动跑工具主要用于调试生成物或 IDE 预生成。

---

## 相关文档

- [getting_started.md](../docs/getting_started.md) · [faq.md](../docs/faq.md)  
- [file_index.md](../docs/file_index.md)
