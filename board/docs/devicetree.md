# mini_tree 设备树说明

编译期由 `tools/dtc-lite.py`（**Lark 文法** + Transformer）解析板级 `board/dts/*.dts` 与 `board/dtsi/*.dtsi`，生成 `board_nodes.h`、`board_devtable.c`、`board_probe.c`、`dt_config_gen.h` 等。

> 注意：本仓库（中间件）不自带 `board/dts/` 与 `board/dtsi/`，由各平台工程自行提供。本仓库仅保留 `board/dt-bindings/` 通用宏常量。

## 文件布局（Linux 写法；dts / dtsi 分目录）

```
<platform>/board/dts/board.dts        板级入口 (/dts-v1/, includes, / { }, &label)
<platform>/board/dtsi/<soc>.dtsi      SoC 根 / { compatible, cpus, soc: soc { ... } }
<platform>/board/dtsi/<soc>-<ip>.dtsi  IP 模板: &soc { spi@0 / uart@1 + 子设备 }
mini_tree/board/dt-bindings/           #include <dt-bindings/...> 平台无关常量 (spi/uart/tim)
mini_tree/tools/dtc-lite.py            CLI 入口（CMake 调用）
mini_tree/tools/dtc_lite/             Lark + Transformer 编译器实现
                                      依赖 lark-parser (pip install lark-parser)
```

| Linux 内核 | mini_tree 中间件 |
|------------|----------------|
| `<soc>.dtsi` | `<platform>/board/dtsi/<soc>.dtsi` |
| `<board>.dts` | `<platform>/board/dts/board.dts` |
| `&soc { ... };` | 同左 |
| `#include <dt-bindings/...>` | 同左（dtc-lite 从 `mini_tree/board/dt-bindings/` 解析通用宏常量；厂商头通过 `-I` 透传 cpp 解析） |

## dtc-lite 编译流水线

```
board/dts/*.dts
    │  ① C 预处理器（#include / #define / #ifdef，系统 cpp）
    │     - 厂商头 #include <xxx_ll_*.h> 由 -I <VENDOR_INC_DIRS> 透传
    │     - 厂商设备选择宏由 -D <VENDOR_DEFINES> 透传 (如 -DSTM32F407xx)
    ▼
合并后的 DTS 文本
    │  ② Lark Earley 解析器（dtc_lite/grammar.py）
    ▼
parse tree
    │  ③ Transformer（dtc_lite/parser.py）转 AST
    ▼
DtsNode AST
    │  ④ 语义 Pass（dtc_lite/compiler.py）
    │     - platform.py 判定基础设施节点（simple-bus / gpio-controller / #*-cells / device_type=cpu）
    │     - label_map → &label 延迟合并 / 虚空创生
    │     - aliases / chosen / interrupt 解析
    │     - DRIVER_REGISTER 扫描 + compatible 校验
    ▼
device_list + driver_map
    │  ⑤ C 代码生成（dtc_lite/generator.py）
    ▼
board_devtable.c / board_probe.c / dt_config_gen.h ...
```

**无序全解耦：** 多个 `/ { }` 任意顺序合并；`&label { }` 延迟合并；未知 label 可自动创生（仍建议在 IP dtsi 写完整模板）。

## Lark 文法规格（grammar.py）

实现见 `tools/dtc_lite/grammar.py`。终端与关键字：

| 终端 | 字面/模式 | 说明 |
|-------|-----------|------|
| `DTSV1` | `/dts-v1/` | 文件头 |
| `DELETE_NODE` | `/delete-node/` | 删除节点 |
| `DELETE_PROP` | `/delete-property/` | 删除属性 |
| `STRING` | `"..."` | 支持 `\"` `\\` `\n` `\t` |
| `INT` | `-?(0x... \| \d+)` | 十六进制须 `0x` 前缀 |
| `IDENT` | `[A-Za-z_][A-Za-z0-9_\-.,/]*` | 含 compatible 中的逗号 |
| `POUND` | `#` | `#address-cells` 等 |
| `SLASH` | `/` | 根节点 `/ {`；注释 `//` `/* */` 在预处理剥离 |
| 标点 | `{ } ; = < > & : , @` | |

注释（`//` 行注释、`/* */` 块注释）在送入 Lark 前由 `_strip_comments` 剥离，避免动态 lexer 在 `/` 上歧义。

## 语法规格（parser）

实现见 `tools/dtc_lite/parser.py`。等价 EBNF：

```ebnf
document   ::= { top_item }
top_item   ::= "/dts-v1/" ";"?
             | "/" "{" node_body "}" ";"?
             | "&" IDENT "{" node_body "}" ";"?
             | "/delete-node/" delete_target ";"?
             | "/delete-property/" IDENT ";"?
             | ";"

node_body  ::= { body_item }

body_item  ::= "#" IDENT [ "=" prop_value ] ";"?
             | IDENT "{" node_body "}" ";"?
             | IDENT ":" IDENT [ "@" addr ] "{" node_body "}" ";"?
             | IDENT "=" prop_value ";"?
             | IDENT ";"                          (* boolean property *)
             | IDENT "@" addr "{" node_body "}" ";"?
             | IDENT "@" addr int_seq ";"?
             | "/delete-node/" delete_target ";"?
             | "/delete-property/" IDENT ";"?

prop_value ::= { STRING | INT | "<" cell_seq ">" | "&" IDENT }

cell_seq   ::= { INT | "&" IDENT | IDENT }

delete_target ::= "&" IDENT | IDENT [ "@" addr ] | "/" path
```

解析完成后 `DTSCompiler._merge_overlays()` 将 `&label` 引用合并到目标节点；若 label 未定义则按 Linux 语义虚空创生（`soc` 挂根下，其余挂 `/soc` 下）。

## platform.py — 平台节点判定

`tools/dtc_lite/platform.py` 提供 `is_platform_node(dev)`：识别基础设施节点，跳过 VFS 驱动绑定。判定规则：

- 节点含 `gpio-controller` / `interrupt-controller` / `mini-tree,platform` 属性
- 节点含 `#*-cells` 属性（如 `#address-cells`、`#interrupt-cells`）
- 节点 `device_type = "cpu"`
- 节点 `compatible = "simple-bus"`

被识别为平台节点的设备不会进入 `s_probe_table[]`，也不要求 `DRIVER_REGISTER`。

## board *.dts 推荐布局

1. `/dts-v1/`
2. `#include` SoC dtsi + IP dtsi（**可集中在文件头**）
3. `/ { model, compatible, aliases }`
4. `&label { ... }` — 板级引脚 / `status = "okay"`

示例：

```dts
/dts-v1/;

#include "<soc>.dtsi"
#include "<soc>-spi.dtsi"
#include <dt-bindings/spi/spi-parameter.h>

/ {
    model = "My Board";
    compatible = "vendor,my-board", "vendor,<soc>";

    aliases {
        spi0 = &spi1;
    };
};

&spi1 {
    status = "okay";
    mosi-pin = <7>;
    miso-pin = <6>;
    sclk-pin = <5>;
    max-trans-buffer = <64>;
};

&spi_dev0 {
    status = "okay";
    cs-pin = <4>;
};
```

## compatible 与属性契约

### SPI 总线控制器（Master / Slave）

| 属性 | 类型 | 说明 |
|------|------|------|
| `host-id` | int | HAL SPI host 编号 |
| `mosi-pin` / `miso-pin` / `sclk-pin` | int | 板级覆写（或 `mosi-port`/`mosi-pin`/`mosi-clk`/`mosi-af` 多字段硬件直投） |
| `dma-tx-cfg` / `dma-rx-cfg` | int array | DMA 流配置（硬件直投：handle/stream/channel/priority/mem_size/enable） |
| `max-trans-buffer` | int | 单次传输上限 |
| `status` | string | `"okay"` / `"disabled"` |

### SPI 子设备（如 W25Q64 / 自定义 SPI slave）

| 属性 | 类型 | 说明 |
|------|------|------|
| `cs-pin` | int | 片选 GPIO |
| `spi-mode` | int | CPOL/CPHA |
| `spi-max-frequency` | int | Hz |
| `queue-size` | int | 传输队列深度 |
| `status` | string | 启用开关 |

**角色约束（probe 阶段校验）：**

- SPI slave 子设备必须挂在 SPI slave 总线下
- SPI master 子设备（如 `winbond,w25q64`）必须挂在 SPI master 总线下

挂错会在 probe 直接 `VFS_ERR_INVAL`，不会留 `ctx->host == NULL` 的半初始化设备。

### UART 设备

| 属性 | 类型 | 说明 |
|------|------|------|
| `port` / `pin` / `clk` / `af` | int | 板级引脚与 AF（硬件直投） |
| `baud` | int | 波特率 |
| `status` | string | 启用开关 |

具体属性字段集见各平台 dtsi 模板与 `dt-bindings/uart/uart-parameter.h`。

### GPIO 设备

| 属性 | 类型 | 说明 |
|------|------|------|
| `pin` / `port` / `clk-bus` | int | GPIO 编号与端口时钟（硬件直投） |
| `mode` / `pull` / `speed` / `output-type` / `af` | int | 厂商宏值直投（如 `LL_GPIO_MODE_OUTPUT`） |
| `status` | string | 启用开关 |

## CMake 集成

`mini_tree/CMakeLists.txt` 在构建前调用：

```text
python tools/dtc-lite.py <BOARD_DTS> <build>/generated/board/mini_tree \
    [-I <VENDOR_INC_DIRS>] [-D <VENDOR_DEFINES>] \
    <driver_source_dirs...>
```

`driver_dirs` 须覆盖所有 `DRIVER_REGISTER` 所在目录，否则 `status = "okay"` 且无驱动的节点会导致构建失败。

### CMake 关键变量

| 变量 | 作用 |
|------|------|
| `BOARD_DTS` | 板级 DTS 入口，默认 `board/dts/board.dts`，各平台覆盖 |
| `BOARD_DTSI_DIR` | 板级 dtsi 目录，默认 `board/dtsi`，各平台覆盖 |
| `BOARD_DT_BINDINGS_DIR` | dt-bindings 目录，默认 `board/dt-bindings`（中间件自带） |
| `VENDOR_INC_DIRS` | 厂商 HAL 头搜索路径，透传给 `dtc-lite.py -I` |
| `VENDOR_DEFINES` | 厂商预定义宏，透传给 `dtc-lite.py -D`（如 `STM32F407xx,USE_FULL_LL_DRIVER`） |

## 依赖

- Python 3.8+
- `lark-parser`（`pip install lark-parser`）
- 系统 `cpp`（C 预处理器，用于厂商头 `#define` / `enum` 提取）
- （可选）`kconfiglib`（仅 `menuconfig.py` 需要）
