# rk3288 快速测试构建

## 说明

本目录是用 **rk3288.dtsi**（Rockchip SoC 真实设备树）构建的产物，用于验证：

- **Linux DTS 源级兼容** — rk3288.dtsi 无需修改即可被 dtc-lite.py 解析
- **无缝移植能力** — 同一套 .dtsi 文件，既可用于 Linux 内核驱动，也可用于 mini_tree MCU 项目
- **跨平台构建** — 以 arm_cm4f 为目标 MCU，验证 MCU 工具链完整可用

## 构建配置

| 项目 | 值 |
|------|-----|
| 平台 | `PLATFORM=arm_cm4f` |
| OSAL | `OSAL_BACKEND=FREERTOS` |
| 设备树 | `board/board.dts`（引用 rk3288.dtsi） |
| 工具链 | arm-none-eabi-gcc 13.3.1 |

## 构建产物

| 路径 | 说明 |
|------|------|
| `board/generated/` | DTC 自动生成的设备表、句柄、节点 ID |
| `generated/kconfig/config.h` | 构建配置宏 |
| `lib/*.a` | 各模块静态库（7 个） |
| `obj/` | 全部目标文件 |

生成设备：**7 个**（uart0~gpio0，来自 rk3288.dtsi 的 6 个外设 + 根节点）

## 复现

```bash
# 将 rk3288.dtsi 放到项目根目录，board/board.dts 引用它
# 然后执行：
mingw32-make PLATFORM=arm_cm4f OSAL_BACKEND=FREERTOS
```

## 移植验证

本构建证明了：

1. rk3288.dtsi 中的 `&label` overlay、`#address-cells`/`#size-cells`、宏表达式全部正确解析
2. reg 属性按 `ac/sc` 正确分组，`device_get_reg()` 可准确读取
3. Linux 内核的 .dtsi 文件可直接纳入 mini_tree 项目使用

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
