# Device Tree Notes

## dtsi 分目录）/ File Layout (Linux Style; dts / dtsi in Separate Directories)

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

## dtc-lite Compilation Pipeline

```
board/dts/*.dts
    │  ① C 预处理器（#include / #define / #ifdef，系统 cpp）
    ▼
    ② Lark 解析 → AST
    ▼
    ③ 生成 board_nodes.h / board_devtable.* / board_probe.c / …
```

```bash
python3 tools/dtc-lite.py board/dts/board.dts <build>/generated <driver_dirs...>
```
