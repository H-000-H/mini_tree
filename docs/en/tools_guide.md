# mini_tree Build Tools

| 项 / Item | 内容 / Content |
| :--- | :--- |
| **读者 / Audience** | 集成构建或改工具链的人 / People integrating builds or changing the toolchain |
| **相关 / Related** | [driver_guide.md](../driver_guide.md) · [getting_started.md](../getting_started.md) |

---
## Contents

---
## 1. dtc-lite / dtc-lite

```bash
python3 tools/dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...] [-I <include_dir> ...] [-D NAME[=VALUE] ...]
```

### Dependency

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

---
## 2. gen_compile_db.py / gen_compile_db.py

```bash
python3 tools/gen_compile_db.py            # 在 mini_tree 根目录生成 / generate at the mini_tree root
python3 tools/gen_compile_db.py --clean    # 删除已生成的 compile_commands.json / remove the generated compile_commands.json
```

---
## 3. genconfig.py / genconfig.py

```bash
python3 tools/genconfig.py Kconfig <output_dir> --config .config
```

---
## 4. menuconfig.py / menuconfig.py

```bash
python tools/menuconfig.py     # 终端全屏界面 (curses TUI, 同内核 make menuconfig)
python tools/guiconfig.py      # 独立图形窗口 (Tkinter GUI, 同内核 make xconfig)
```

### No Dependency



#### Optional: Modern UI



---
## 5. scrubber CRC stub / Scrubber CRC Stub

```c
#define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000
```

---
## Relationship with CMake



---
## Related Documents

- [getting_started.md](../getting_started.md) · [faq.md](../faq.md)
- [file_index.md](../file_index.md)
