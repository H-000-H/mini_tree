# mini_tree 设备树说明

编译期由 `tools/dtc-lite.py`（**Lark 文法** + Transformer）解析 `board/dts/*.dts` 与 `board/dtsi/*.dtsi`，生成 `board_nodes.h`、`board_devtable.c`、`board_probe.c`、`dt_config_gen.h` 等。
At compile time, `tools/dtc-lite.py` (**Lark grammar** + Transformer) parses `board/dts/*.dts` and `board/dtsi/*.dtsi` and generates `board_nodes.h`, `board_devtable.c`, `board_probe.c`, `dt_config_gen.h`, etc.

## 文件布局（Linux 写法；dts

中间件本仓只保留**通用占位示例**；正式 SoC / 板级文件放在平台工程，用 `BOARD_DTS` / `BOARD_DTSI_DIR`（或 `MINI_TREE_BOARD_PORT`）覆盖。
This middleware repo keeps only **generic placeholder examples**; the real SoC / board files live in platform projects and override via `BOARD_DTS` / `BOARD_DTSI_DIR` (or `MINI_TREE_BOARD_PORT`).

```
board/dts/board.dts              占位入口 (/dts-v1/, includes, / { }, &label)
board/dtsi/example-soc.dtsi      通用示例：cpus / soc / gpio / uart 模板
board/dt-bindings/               #include <dt-bindings/...> 常量
tools/dtc-lite.py                CLI 入口（CMake 调用）
tools/dtc_lite/                  Lark + Transformer 编译器实现
                                 依赖 lark（pip install lark）
```

| Linux 内核 / Linux kernel | mini_tree（本仓占位 / this repo, placeholder） | 平台工程（正式 / platform project, formal） |
|------------|----------------------|------------------|
| `<soc>.dtsi` | `board/dtsi/example-soc.dtsi` | `board/dtsi/<soc>*.dtsi` |
| `<board>.dts` | `board/dts/board.dts` | `board/dts/<board>.dts` |
| `&soc { ... };` | 同左 / same | 同左 / same |
| `#include <dt-bindings/...>` | 同左（dtc-lite 从 `board/dt-bindings/` 解析）/ same (dtc-lite resolves from `board/dt-bindings/`) | 可再加厂商 `-I` / may add vendor `-I` |

## dtc-lite 编译流水线

```
board/dts/*.dts
    │  ① C 预处理器（#include / #define / #ifdef，系统 cpp）
    ▼
    ② Lark 解析 → AST
    ▼
    ③ 生成 board_nodes.h / board_devtable.* / board_probe.c / …
```

示例 / Example:

```bash
python3 tools/dtc-lite.py board/dts/board.dts <build>/generated <driver_dirs...>
```

占位 `board/dts/board.dts` 无外设节点，仍可完整走通流水线并编过；平台接入时把 `BOARD_DTS` 指到真实板级 `.dts`，并把 SoC 专用 dtsi 放进平台的 `BOARD_DTSI_DIR`。
The placeholder `board/dts/board.dts` has no peripheral nodes yet still runs the full pipeline and builds; platforms point `BOARD_DTS` at their real board `.dts` and put SoC-specific dtsi into their `BOARD_DTSI_DIR`.

详见 [docs/driver_guide.md](../driver_guide.md)、[docs/porting_guide.md](../porting_guide.md)。
See [docs/driver_guide.md](../driver_guide.md) and [docs/porting_guide.md](../porting_guide.md).
