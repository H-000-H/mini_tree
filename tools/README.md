# mini_tree 构建工具

## dtc-lite

MCU 编译期 DeviceTree 编译器：`DTS → board_devtable.c / board_probe.c / dt_config_gen.h`。

```bash
python dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...] \
    [-I <include_dir>] [-D <define>] ...
```

参数：
- `<board.dts>`：输入的板级 DTS 文件路径（由各平台工程提供）
- `<output_dir>`：生成文件的输出目录
- `[driver_source_dirs...]`：扫描 `DRIVER_REGISTER` 宏的驱动源码目录（可多个）
- `-I <dir>`：额外头文件搜索路径（可多个），让 dtsi 里 `#include <厂商头.h>` 能找到，命中后用 `cpp -E -P -dM` 提取该头全部 `#define`
- `-D <NAME[=VAL]>`：预定义宏（可多个），传给 cpp，如 `-DSTM32F407xx -DUSE_FULL_LL_DRIVER`

CMake 通过 `VENDOR_INC_DIRS` 与 `VENDOR_DEFINES` 变量透传给 `-I` 与 `-D`。

### 包结构

| 模块 | 职责 |
|------|------|
| `dtc_lite/grammar.py` | Lark 文法（Earley 算法） |
| `dtc_lite/parser.py` | Transformer 把 parse tree 转 AST |
| `dtc_lite/dts_ast.py` | `DtsNode` / `DtsProperty` 数据结构 |
| `dtc_lite/compiler.py` | `#include` 预处理、overlay 合并、驱动扫描、cpp 提取厂商头宏 |
| `dtc_lite/platform.py` | 设备树平台节点判定（`is_platform_node`），识别 `simple-bus` / `gpio-controller` / `interrupt-controller` / `mini-tree,platform` / `#*-cells` / `device_type=cpu` 等基础设施节点，跳过 VFS 驱动绑定 |
| `dtc_lite/generator.py` | C 代码生成 |
| `dtc_lite/main.py` | argparse 命令行入口，调用 `DTSCompiler` + `CGenerator` |
| `dtc-lite.py` | 顶层 CLI 包装（兼容直接执行） |

依赖 `lark` 包（`pip install lark-parser`）。设备树编写规范见 [../board/docs/devicetree.md](../board/docs/devicetree.md)。

## genconfig

Kconfig → C 头文件生成器：把 `.config` 转为 `generated/kconfig/mini_tree/config.h`。

```bash
python genconfig.py <kconfig_path> <output_dir> [--config <config_file>]
```

- `<kconfig_path>`：Kconfig 文件路径
- `<output_dir>`：`config.h` 输出目录
- `--config <path>`：`.config` 文件路径（默认 `<output_dir>/.config`）

依赖 `kconfiglib` 包（`pip install kconfiglib`）。

## menuconfig

文本菜单配置器，交互式修改 `.config`。

```bash
python menuconfig.py
```

依赖 `kconfiglib` 与 `menuconfig` 包。

## post_build_crc

链接后 CRC 校验值生成器：读入二进制文件，计算 CRC32，输出 C 头文件宏定义，供 scrubber 做 Flash 位腐烂检测。

```bash
python post_build_crc.py --input <binary> [--output <header>] \
    [--define <MACRO_NAME>] [--algo crc32]
```

参数：
- `--input <binary>`：输入二进制文件（如 `firmware.bin`）
- `--output <header>`：输出 C 头文件（省略时输出到 stdout）
- `--define <NAME>`：C 宏名（默认 `SYSTEM_SCRUBBER_CRC_BASELINE`）
- `--algo <name>`：哈希算法（默认 `crc32`，目前仅支持 crc32）

也兼容旧版位置参数：`python post_build_crc.py firmware.bin system_scrubber_crc_gen.h`

输出示例：

```c
#ifndef SYSTEM_SCRUBBER_CRC_BASELINE
#define SYSTEM_SCRUBBER_CRC_BASELINE 0xA1B2C3D4
#endif
```

若输出文件已存在且含同名宏，会就地替换其值（原子写入，防止生成残缺头文件）。
