# mini_tree 构建工具

## dtc-lite

MCU 编译期 DeviceTree 编译器：`DTS → board_devtable.c / board_probe.c / board_nodes.h / board_handles.h / dt_config_gen.h`。

由 `mini_tree/CMakeLists.txt` 在构建前自动调用，输出到 `${CMAKE_BINARY_DIR}/generated/board/mini_tree/`：

```cmake
# mini_tree/CMakeLists.txt 中的调用
add_custom_command(
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
    COMMAND "${Python3_EXECUTABLE}" "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
    DEPENDS "${DTC_LITE}" ... "${BOARD_DTS}"
    COMMENT "Running dtc-lite on ${BOARD_DTS}"
)
```

`BOARD_DTS` 由具体项目 CMakeLists.txt 提供，指向板级 `.dts` 入口。

### 包结构

| 模块 | 职责 |
|------|------|
| `dtc_lite/lexer.py` | PLY 词法分析 |
| `dtc_lite/parser.py` | 递归下降语法分析（消费 lexer token 流） |
| `dtc_lite/compiler.py` | `#include` 预处理、overlay 合并、驱动扫描、Kahn 拓扑排序 |
| `dtc_lite/generator.py` | C 代码生成 |
| `dtc_lite/main.py` / `ast.py` | CLI 入口与 AST 节点定义 |
| `vendor/ply/` | vendored [PLY](https://github.com/dabeaz/ply) |

### 其他工具

| 脚本 | 职责 |
|------|------|
| `genconfig.py` | Kconfig → `config.h`（由 `.config` 生成预处理宏） |
| `kconfig_gui.py` | Kconfig 图形化配置（原 `menuconfig.py`，因与 kconfiglib 模块名冲突改名） |
| `post_build_crc.py` | 编译后 CRC 基线生成（供 `system_scrubber` 使用） |
| `firmware_size_report.py` | 固件容量统计（Flash/RAM 占用报表，由具体项目 POST_BUILD 调用） |

设备树编写规范见 `board/docs/devicetree.md`。

