# mini_tree 设备树说明

编译期由 `tools/dtc-lite.py`（**PLY 词法分析** + 递归下降语法分析）解析具体项目提供的 `BOARD_DTS`，生成 `board_nodes.h`、`board_devtable.c`、`board_probe.c`、`dt_config_gen.h` 等到 `${CMAKE_BINARY_DIR}/generated/board/mini_tree/`。

> 设备树源文件（`.dts` / `.dtsi`）由具体项目提供并通过 `BOARD_DTS` 变量传入 `mini_tree` CMake 子模块。例如 ESP32-S3 项目位于 `ESP32-S3/components/mini_tree/board/dts/`，STM32F407 项目位于 `STM32F407ZGT6/mini_tree/board/dts/`，CH32V307 项目位于 `CH32V307/mini_tree/board/dts/`。

## 文件布局（以 ESP32-S3 为例；其他节点结构相同）

```
<project>/mini_tree/board/dts/<board>.dts   板级入口 (/dts-v1/, includes, / { }, &label)
<project>/mini_tree/board/dtsi/<soc>.dtsi   SoC 根 / { compatible, cpus, soc: soc { ... } }
<project>/mini_tree/board/dtsi/<ip>.dtsi    IP: &soc { spi@0 / spi@1 + 子设备 }
<project>/mini_tree/board/dt-bindings/      #include <dt-bindings/...> 常量
mini_tree/tools/dtc-lite.py                 CLI 入口（CMake 调用）
mini_tree/tools/dtc_lite/                   PLY 编译器实现
mini_tree/tools/vendor/ply/                 vendored PLY 3.x（构建无需 pip install）
```

| Linux 内核 | mini_tree |
|------------|-----------|
| `esp32s3.dtsi` | `<project>/mini_tree/board/dtsi/esp32s3.dtsi` |
| `esp32-s3-devkitc-1.dts` | `<project>/mini_tree/board/dts/esp32-s3-devkitc-1.dts` |
| `&soc { ... };` | 同左 |
| `#include <dt-bindings/...>` | 同左（dtc-lite 从 `board/dt-bindings/` 解析） |

## dtc-lite 编译流水线

```
board/dts/*.dts
    │  ① C 预处理器（#include / #define / #ifdef，非 PLY）
    ▼
合并后的 DTS 文本
    │  ② PLY lexer（dtc_lite/lexer.py）
    ▼
Token 流
    │  ③ 递归下降 parser（dtc_lite/parser.py）
    ▼
DtsNode AST
    │  ④ 语义 Pass（dtc_lite/compiler.py）
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

## PLY 词法规格（lexer）

实现见 `tools/dtc_lite/lexer.py`。token 类型：

| Token | 字面/模式 | 说明 |
|-------|-----------|------|
| `DTSV1` | `/dts-v1/` | 文件头 |
| `DELETE_NODE` | `/delete-node/` | 删除节点 |
| `DELETE_PROP` | `/delete-property/` | 删除属性 |
| `STRING` | `"..."` | 支持 `\"` `\\` `\n` `\t` |
| `INT` | `-?(0x... \| \d+)` | 十六进制须 `0x` 前缀 |
| `IDENT` | `[A-Za-z_][A-Za-z0-9_\-.,/]*` | 含 compatible 中的逗号 |
| `POUND` | `#` | `#address-cells` 等 |
| `SLASH` | `/` | 根节点 `/ {`；注释 `//` `/* */` 优先匹配 |
| 标点 | `{ } ; = < > & : , @` | |

注释与 `/dts-v1/` 的匹配优先级高于裸 `/`，避免 `/*` 被拆成 `SLASH` + 非法 `*`。

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

## board *.dts 推荐布局

见 `board/dts/esp32-s3-devkitc-1.dts`：

1. `/dts-v1/`
2. `#include` SoC dtsi + IP dtsi（**可集中在文件头**）
3. `/ { model, compatible, aliases }`
4. `&label { ... }` — 板级引脚 / `status = "okay"`

## compatible 与属性契约

### `esp32,spi` / `esp32,spi-master`（总线控制器）

| 属性 | 类型 | 说明 |
|------|------|------|
| `host-id` | int | HAL SPI host 编号 |
| `mosi-pin` / `miso-pin` / `sclk-pin` | int | 板级覆写 |
| `dma-chan` | int | `-1` 表示自动分配 |
| `max-trans-buffer` | int | 单次传输上限 |
| `status` | string | `"okay"` / `"disabled"` |

- `esp32,spi` → Slave 总线（`spi_bus.c`）
- `esp32,spi-master` → Master 总线（`spi_master_bus`）

### `heterogeneous,fft-spi-slave` / `heterogeneous,w25q64-master`（SPI 子设备）

| 属性 | 类型 | 说明 |
|------|------|------|
| `cs-pin` | int | 片选 GPIO |
| `spi-mode` | int | CPOL/CPHA |
| `spi-max-frequency` | int | Hz |
| `queue-size` | int | 传输队列深度 |
| `status` | string | 启用开关 |

**角色约束（probe 阶段校验）：**

- `fft-spi-slave` 必须挂在 `esp32,spi`（slave）总线下
- `w25q64-master` 必须挂在 `esp32,spi-master` 总线下

挂错会在 probe 直接 `VFS_ERR_INVAL`，不会留 `ctx->host == NULL` 的半初始化设备。

### `esp32,ws2812`

| 属性 | 类型 | 说明 |
|------|------|------|
| `gpio` | int | 板级必配 |
| `num-leds` | int | 板级必配 |
| `brightness` / `color-order` / RMT 时序 | 见 dtsi | 默认来自 `dt-bindings/led/ws2812-timing.h` |

## CMake 集成

`mini_tree/CMakeLists.txt` 在构建前调用：

```text
${Python3_EXECUTABLE} tools/dtc-lite.py ${BOARD_DTS} ${GENERATED_BOARD_DIR}
```

- `BOARD_DTS` 由具体项目 CMakeLists.txt 设置，指向板级 `.dts` 入口
- `GENERATED_BOARD_DIR` 默认为 `${CMAKE_BINARY_DIR}/generated/board/mini_tree/`
- ESP32-S3 节点通过 `idf_component_register()` 集成，`BOARD_DTS` 在 `components/mini_tree/CMakeLists.txt` 中设置
- STM32F407 / CH32V307 节点通过 `add_subdirectory(mini_tree)` 集成，`BOARD_DTS` 在项目根 `CMakeLists.txt` 中设置

## 依赖

- Python 3.8+
- PLY 已 vendored 于 `tools/vendor/ply/`，无需额外 `pip install`
