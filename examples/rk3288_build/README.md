# rk3288 快速测试构建

## 说明

本目录是用 **rk3288.dtsi**（Rockchip SoC 真实设备树）通过 dtc-lite.py 生成的 DTS 解析产物，用于验证：

- **Linux DTS 源级兼容** — rk3288.dtsi 无需修改即可被 dtc-lite.py 解析
- **无缝移植能力** — 同一套 .dtsi 文件，既可用于 Linux 内核驱动，也可用于 mini_tree MCU 项目

## 目录结构

```
examples/rk3288_build/
├── README.md
├── dts/                          # DTS 源文件（自包含，不依赖项目根目录）
│   ├── board.dts                 # 板级 DTS（引用 rk3288.dtsi + &uart0 overlay）
│   ├── rk3288.dtsi               # Rockchip RK3288 SoC 设备树（来自 Linux 内核）
│   ├── skeleton.dtsi             # 最小 skeleton（给 rk3288.dtsi 用）
│   └── dt-bindings/              # Linux 风格 dt-bindings 宏头文件
│       ├── gpio/gpio.h
│       ├── interrupt-controller/irq.h
│       ├── interrupt-controller/arm-gic.h
│       ├── pinctrl/rockchip.h
│       ├── clock/rk3288-cru.h    # ~90 个时钟/复位 ID 宏
│       └── thermal/thermal.h
├── board/generated/              # dtc-lite.py 生成的 C 代码
└── obj/ lib/                     # 构建中间产物
```

## 构建配置

| 项目 | 值 |
|------|-----|
| DTS 源 | `examples/rk3288_build/dts/board.dts` |
| 工具链 | dtc-lite.py（Python，无 MCU 编译） |

## DTS 生成（重新生成设备表）

```bash
python tools/dtc-lite.py examples/rk3288_build/dts/board.dts examples/rk3288_build/board/generated
```

输出 7 个文件：`board_nodes.h`、`board_devtable.c`、`board_devtable.h`、`board_probe.c`、`board_handles.h`、`dt_config_gen.h`、`board_force_link.c`。

## 构建产物

| 路径 | 说明 |
|------|------|
| `board/generated/` | dtc-lite.py 生成的设备表、句柄、节点 ID |

生成设备：**68 个**，涵盖 RK3288 全部主要外设：

- 4× Cortex-A12 CPU
- 3× DMAC (PL330)
- 4× DW MMC (SD/MMC/SDIO/eMMC)
- 5× UART, 6× I2C, 3× SPI, 4× PWM
- Ethernet GMAC, USB OTG/EHCI/HSIC, HDMI, I2S, VOP
- GIC-400 中断控制器, CRU 时钟/复位
- TSADC, SARADC, Watchdog, Timer
- 9× GPIO (gpio0~gpio8) 含 pinctrl
- 其他 SoC 内部模块 (PMU, SGRF, GRF, SRAM 等)

## 复现

当前目录下的 `board/generated/` 已包含最新生成结果。如需重新生成 DTS 设备表：

```bash
python tools/dtc-lite.py examples/rk3288_build/dts/board.dts examples/rk3288_build/board/generated
```

> **注意**：rk3288 是 Cortex-A12 应用处理器，其 DTS 包含 GIC-400 中断控制器、PL330 DMAC、CRU 时钟框架等 Linux 级外设，不在 MCU 的裸机/FreeRTOS 场景下运行。本例仅验证 dtc-lite.py 的 **Linux DTS 源级兼容性** — 同一份 .dtsi 无需修改即可被解析生成 C 结构体。

## 移植验证

本构建证明了：

1. rk3288.dtsi 中的 `&label` overlay、`#address-cells`/`#size-cells`、宏表达式全部正确解析
2. reg 属性按 `ac/sc` 正确分组，`device_get_reg()` 可准确读取
3. 中断三件套（`interrupt-parent` + `#interrupt-cells` + `interrupt-controller`）完整支持
4. 多个 `<>` 块的中断分组有效
5. Linux 内核的 .dtsi 文件可直接纳入 mini_tree 项目使用（需提供对应 dt-bindings 头文件）

## 设备树优势：代码量对比

以本示例 68 个设备为例，对比手写注册与 DTS 生成的代码量：

| 项目 | 行数 | 说明 |
|------|------|------|
| **DTS 源（用户编写）** | **253** | board.dts + skeleton.dtsi + dt-bindings 头文件 |
| rk3288.dtsi（复用 Linux） | 1273 | 来自 Linux 内核，无需编写 |
| **生成 C 代码** | **3170** | 68 个设备的属性表、reg 表、中断表、probe 表 |

不使用设备树时，3170 行 C 代码需要手写——为每个设备声明 `device_prop_t[]`、`device_reg_t[]`、`device_irq_t[]`，维护设备 ID 枚举，编写 probe 顺序表。增减外设或修改地址都需要同步更新多处。

使用设备树后，只需编写 `board.dts` 引用 SoC 的 `.dtsi`，用 `&label { }` 使能外设即可。SoC 级描述（`rk3288.dtsi`）直接复用 Linux 内核源码，与内核驱动保持同步。

## 已知差异（与 Linux DTS 对比）

| 功能 | Linux | mini_tree |
|------|-------|-----------|
### 中断
| 功能 | Linux | mini_tree |
|------|-------|-----------|
| `interrupt-parent` 解析 | 自动沿父链查找，phandle 跳转 | ✅ 已实现 |
| `#interrupt-cells` 分组 | 按 cells 数切分 specifier | ✅ 已实现 |
| `interrupt-controller` 标记 | 用于构建中断控制器树 | ✅ 已实现 |
| 驱动层 IRQ 获取 API | `platform_get_irq(pdev, idx)` | ✅ `device_get_irq(dev, idx, &irq)` |
| IRQ 号映射 | DTS spec → Linux irq 号 | ✅ 按 `#interrupt-cells` 规则提取 irq |
| `interrupts-extended` | 支持多中断控制器引用 | ❌ 未实现 |
| `interrupt-names` | 按名字索引 IRQ | ❌ 未实现 |

### 时钟 / DMA / reset
| 功能 | Linux | mini_tree |
|------|-------|-----------|
| `clocks` + `clock-names` | 框架解析，CCF 时钟框架管理 | ❌ 无通用解析，驱动自行 `device_get_prop_int()` |
| `resets` + `reset-names` | 框架级自动 deassert | ❌ 未实现 |
| `dmas` + `dma-names` | 框架级 DMA 引擎绑定 | ❌ 未实现 |
| `assigned-clocks` / `assigned-clock-rates` | 框架自动配置时钟频率 | ❌ 未实现 |

### GPIO / pinctrl
| 功能 | Linux | mini_tree |
|------|-------|-----------|
| `gpios` / `cs-gpios` (带 `#gpio-cells`) | GPIO 描述符框架解析 | ❌ 存为 raw int，无 `#gpio-cells` 分组 |
| `pinctrl-0` / `pinctrl-names` | pinctrl 子系统自动设置 | ❌ 未实现 |
| `gpio-controller` / `#gpio-cells` | 用于 GPIO 控制器树 | ❌ 未识别 |

### 地址翻译
| 功能 | Linux | mini_tree |
|------|-------|-----------|
| `ranges` (子→父地址映射) | 自动进行地址增量翻译 | ❌ 未实现，存为 raw int |
| `dma-ranges` | DMA 地址翻译 | ❌ 未实现 |
| `#address-cells` / `#size-cells` → reg 分组 | 完整支持 | ✅ 已实现 |

### 节点操作
| 功能 | Linux | mini_tree |
|------|-------|-----------|
| `/include/` | 预处理指令 | ✅ 已实现 |
| `&label` overlay | 标准 DTC 合并到目标节点 | ✅ 已实现 |
| `/delete-node/` | 删除节点 | ✅ 已实现 |
| `/delete-property/` | 删除属性 | ✅ 已实现 |
| `/omit-if-no-ref/` | 未引用则自动省略 | ❌ 未实现 |
| `/plugin/` | 插件模式（动态覆写） | ❌ 未实现 |

### 其他
| 功能 | Linux | mini_tree |
|------|-------|-----------|
| `compatible` 匹配 | 内核驱动匹配 | ✅ 已实现 |
| `status` 解析 | okay/disabled/fail | ✅ 已实现 |
| `label` → 节点引用 | 编译时解析 | ✅ 已实现 |
| 属性存为原始值 | `device_get_prop_int()` 等 | ✅ 已实现 |
| 宏展开（`#define`） | 通过 CPP 预处理 | ✅ 已实现 |
| `/bits/` 指令 | 按 bit 指定值 | ❌ 未实现 |
| `/memreserve/` | 保留内存区域 | ❌ 未实现 |
| 函数式宏（如 `GIC_CPU_MASK_SIMPLE(nr)`） | CPP 预处理 | ⚠️ 部分支持（简单替换，参数展开由 tokenizer 跳过） |
