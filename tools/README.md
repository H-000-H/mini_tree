# mini_tree 构建工具 / mini_tree Build Tools

> `dtc-lite`、`gen_compile_db`、`genconfig`、`menuconfig`、scrubber CRC stub 的用法与模块说明。
> Usage and module notes for `dtc-lite`, `gen_compile_db`, `genconfig`, `menuconfig`, and the scrubber CRC stub.

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 集成构建或改工具链的人 / People integrating builds or changing the toolchain |
| **相关 / Related** | [driver_guide.md](../docs/driver_guide.md) · [getting_started.md](../docs/getting_started.md) |

---

## 目录 / Contents

1. [dtc-lite / dtc-lite](#1-dtc-lite-dtc-lite)
2. [gen_compile_db.py / gen_compile_db.py](#2-gen_compiledbpy-gen_compiledbpy)
3. [genconfig.py / genconfig.py](#3-genconfigpy-genconfigpy)
4. [menuconfig.py / menuconfig.py](#4-menuconfigpy-menuconfigpy)
5. [scrubber CRC stub / Scrubber CRC Stub](#5-scrubber-crc-stub-scrubber-crc-stub)
6. [与 CMake 的关系 / Relationship with CMake](#6-与-cmake-的关系-relationship-with-cmake)

---

## 1. dtc-lite / dtc-lite

MCU **编译期** DeviceTree 编译器：把板级 DTS 变成 C 头/源，并扫描 `DRIVER_REGISTER`。
A **compile-time** DeviceTree compiler for MCUs: turns a board DTS into C headers/sources and scans for `DRIVER_REGISTER`.

```bash
python3 tools/dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...]
```

### 依赖 / Dependency

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

`board_nodes.h`、`board_devtable.h/.c`、`board_probe.c`、`board_handles.h`、`dt_config_gen.h`。

编写契约见 [driver_guide.md](../docs/driver_guide.md)。
See [driver_guide.md](../docs/driver_guide.md) for the authoring contract.

---

## 2. gen_compile_db.py / gen_compile_db.py

从根 `compile_flags.txt` 生成 `compile_commands.json`（**含头文件条目**），供 clangd / clang-tidy 索引。
Generates `compile_commands.json` from the root `compile_flags.txt` (**including header entries**) for clangd / clang-tidy indexing.

```bash
python3 tools/gen_compile_db.py            # 在 mini_tree 根目录生成 / generate at the mini_tree root
python3 tools/gen_compile_db.py --clean    # 删除已生成的 compile_commands.json / remove the generated compile_commands.json
```

场景：mini_tree 作为子模块嵌入父项目时，父项目的 `compile_commands.json` 会覆盖本仓的 `compile_flags.txt`，导致 `hal/bus/vfs` 头文件找不到；本脚本在仓库根就近生成，clangd 优先使用它，无需父项目 configure 即可获得完整索引。
Use case: when mini_tree is embedded as a submodule, the parent's `compile_commands.json` shadows this repo's `compile_flags.txt`, so `hal/bus/vfs` headers go missing; this script generates a local copy at the repo root that clangd prefers, giving full indexing without a parent configure.

---

## 3. genconfig.py / genconfig.py

```bash
python3 tools/genconfig.py Kconfig <output_dir> --config .config
```

把 Kconfig 符号写成 `#define CONFIG_*` 的 `config.h`。需本机可用的 `kconfiglib`（或项目使用的兼容包）。
Writes Kconfig symbols into a `config.h` of `#define CONFIG_*` lines. Needs a usable `kconfiglib` (or a compatible package the project uses).

---

## 4. menuconfig.py / menuconfig.py

```bash
python3 tools/menuconfig.py
```

Kconfig **图形化**配置工具（依赖 `kconfiglib`）：交互式浏览/修改 `.config` 并保存，供 genconfig 消费。
A **menu-style** Kconfig configurator (requires `kconfiglib`): interactively browse/edit `.config` and save it for genconfig to consume.

---

## 5. scrubber CRC stub / Scrubber CRC Stub

`tools/system_scrubber_crc_stub.h` 在 CMake 中拷贝为生成目录里的 `system_scrubber_crc_gen.h`，提供：
`tools/system_scrubber_crc_stub.h` is copied by CMake into the generated directory as `system_scrubber_crc_gen.h`, providing:

```c
#define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000U
```

链接后可用板级脚本覆盖真实 CRC 基线。
After linking, a board-level script can override the real CRC baseline.

---

## 6. 与 CMake 的关系 / Relationship with CMake

根 `CMakeLists.txt` 在构建 `mini_tree` 时自动：
The root `CMakeLists.txt` automatically, when building `mini_tree`:

1. 调用 genconfig / invokes genconfig
2. 调用 dtc-lite（并传入 vfs/bus/drivers 扫描路径）/ invokes dtc-lite (passing the vfs/bus/drivers scan paths)
3. 拷贝 scrubber stub / copies the scrubber stub

手动跑工具主要用于调试生成物或 IDE 预生成（如 `gen_compile_db.py`）。
Running tools manually is mainly for debugging generated artifacts or pre-generating for IDEs (e.g. `gen_compile_db.py`).

---

## 相关文档 / Related Documents

- [getting_started.md](../docs/getting_started.md) · [faq.md](../docs/faq.md)
- [file_index.md](../docs/file_index.md)
