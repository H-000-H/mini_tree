# mini_tree 构建工具

## dtc-lite

MCU 编译期 DeviceTree 编译器：`DTS → board_devtable.c / board_probe.c / dt_config_gen.h`。

```bash
python dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...]
```

### 包结构

| 模块 | 职责 |
|------|------|
| `dtc_lite/grammar.py` | Lark 文法 (Earley 算法) |
| `dtc_lite/parser.py` | Transformer 把 parse tree 转 AST |
| `dtc_lite/dts_ast.py` | DtsNode / DtsProperty 数据结构 |
| `dtc_lite/compiler.py` | `#include` 预处理、overlay 合并、驱动扫描 |
| `dtc_lite/generator.py` | C 代码生成 |

依赖 `lark` 包（`pip install lark-parser`）。设备树编写规范见 `board/docs/devicetree.md`。
