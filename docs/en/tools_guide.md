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
python tools/menuconfig.py                          # terminal full-screen UI (curses TUI, like `make menuconfig`)
python tools/_vendor/guiconfig.py Kconfig.non_esp   # standalone GUI window (Tkinter, like `make xconfig`)
```

### No Dependency

Both UIs come from the vendored upstream **kconfiglib** under `tools/_vendor/` (ISC license, see [../../tools/_vendor/README.md](../../tools/_vendor/README.md)) — no system-level kconfig package and no ESP-IDF `esp_idf_kconfig` required. The GUI additionally needs only the stdlib `tkinter` (bundled with the official Windows installer; `python3-tk` on Linux).

`tools/menuconfig.py` is the **TUI launcher**: it prepends `tools/_vendor` to `sys.path`, pins the top-level `Kconfig.non_esp` and `.config`, then calls upstream `menuconfig`. This branch ships **no GUI launcher** — run the upstream script directly. `tools/_vendor/guiconfig.py` is executable as-is (`tools/_vendor` lands on `sys.path`) and the top-level `Kconfig.non_esp` uses `rsource`, so the CWD does not matter; `.config` is written to the CWD by default (`KCONFIG_CONFIG` overrides it):

```bash
python tools/_vendor/guiconfig.py Kconfig.non_esp   # run from the repo root; reads/writes ./.config
```

The `kconfiglib.py` / `menuconfig.py` / `guiconfig.py` under `tools/_vendor/` stay in sync with upstream and are unmodified (see the manifest in `tools/_vendor/README.md`).



---
## 5. scrubber CRC stub / Scrubber CRC Stub

```c
#define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000
#define SYSTEM_SCRUBBER_IMAGE_LEN    0U
```

---
## Relationship with CMake



---
## Related Documents

- [getting_started.md](../getting_started.md) · [faq.md](../faq.md)
- [file_index.md](../file_index.md)
