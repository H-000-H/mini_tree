# 设备树移植手册（集中版）

> 把 mini_tree 中间件接到新 SoC / 新板级时，**设备树（DTS/DTSI）相关的全部步骤都收在这篇**，不再分散到 architecture / porting_guide / getting_started 各处。HAL 后端、OS 后端、CMake 总流程见文末"相关文档"。
>
> 本文所有示例均对照当前仓库真实机制（`tools/dtc-lite.py`、`board/dtsi/*`、`drivers/bmp280` 等）与当前 API 编写。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 接新平台 / 新板级、写产品驱动的工程师 |
| **前置** | 了解 Linux 设备树基本概念（`compatible` / `reg` / `status` / `&label` 覆盖） |
| **相关** | [driver_guide.md](driver_guide.md)（驱动编写）· [architecture.md](architecture.md)（分层与启动）· [getting_started.md](getting_started.md)（构建）|

---

## 1. 为什么用设备树，以及它在这里怎么工作

mini_tree 走 **Linux 式设备树**，但**不依赖内核 dtc**：自研 `tools/dtc-lite.py`（Lark 文法 + Transformer）在**编译期**把 `*.dts` / `*.dtsi` 解析成普通 C 静态表（`board_nodes.h`、`dt_config_gen.h`、`board_devtable.c`、`board_probe.c`）。

关键区别（与 Zephyr 等）：

- 生成物是**普通 C 静态表**，可直接断点、看符号、grep，调试友好。
- 驱动匹配是**编译期**收录（`DRIVER_REGISTER` 宏 + dtc-lite 扫描），运行时**无 `strcmp` 匹配**、无动态注册表。
- 池大小、主机数量等由**节点数自动生成**（`DTC_GEN_COUNT_*` 宏），不手填魔法数字。

---

## 2. dtc-lite 编译流水线

```
board/dts/board.dts                ← 板级入口 (/dts-v1/; includes; / { } ; &label 覆盖)
        │
        │  ① C 预处理器（#include / #define / #ifdef，系统 cpp；dt-bindings 常量在此展开）
        ▼
        │  ② Lark 解析 → AST（节点树、属性、&label 引用解析）
        ▼
        │  ③ Transformer 生成：
        ▼
   <build>/generated/board/mini_tree/
        ├── board_nodes.h       设备 ID 枚举 (device_id_t) + 节点结构声明
        ├── dt_config_gen.h     DTC_GEN_COUNT_* / DTC_GEN_*_HZ / 主机 MAX 等常量
        ├── board_devtable.c    设备表（name / compatible / label / 属性）
        └── board_probe.c       按 DRIVER_REGISTER 生成的 probe/remove 函数表
```

CLI（通常 CMake 自动调用，无需手跑）：

```bash
python3 tools/dtc-lite.py board/dts/board.dts <build>/generated <driver_dirs...>
```

`driver_dirs` 是各驱动 `.c` 所在目录，dtc-lite 扫描其中的 `DRIVER_REGISTER(...)` 宏来生成 probe 表。

---

## 3. 三种文件放置位置

| 位置 | 路径 | 说明 |
| :--- | :--- | :--- |
| 中间件占位（本仓） | `board/dts/board.dts`、`board/dtsi/example-soc.dtsi`、`board/dtsi/drivers/*.dtsi`、`board/dtsi/vfs/*.dtsi` | 通用模板，无真实外设；`board.dts` 无节点也能走通流水线 |
| 平台工程（正式） | 由 `BOARD_DTS` / `BOARD_DTSI_DIR` / `MINI_TREE_BOARD_PORT` 指向 | **真实 SoC / 板级文件放这里**，覆盖中间件占位 |
| dt-bindings 常量 | `board/dt-bindings/<bus>/*.h` | `#include <dt-bindings/...>` 的宏定义（如 `DTS_GPIO_DEFAULT_INTR`） |

> **核心约定**：中间件本仓只保留占位示例；正式板级文件放平台工程，通过 CMake 变量覆盖。不要往本仓 `board/dtsi/` 塞厂商专用 dtsi（用 `VENDOR_INC_DIRS` 引入厂商宏）。

---

## 4. 节点模板写法（DTSI）

### 4.1 SoC 骨架模板

`board/dtsi/example-soc.dtsi` 给出标准骨架：`cpus` / `soc`（带 `compatible = "simple-bus"`、`ranges`）/ `gpio` / `uart`。平台把它复制为 `<soc>.dtsi`，把数值换成厂商宏，再用 `BOARD_DTSI_DIR` 指向。

```dts
// 平台工程 my_soc.dtsi（从 example-soc.dtsi 复制改名）
/include/ "dt-bindings/gpio/gpio-parameter.h"

/ {
    compatible = "vendor,my-soc";
    #address-cells = <1>;
    #size-cells = <0>;

    soc: soc {
        compatible = "simple-bus";
        ranges;
        #address-cells = <1>;
        #size-cells = <0>;

        gpio: gpio@0 {
            compatible = "mt-gpios";
            reg = <0>;
            gpio-controller;
            #gpio-cells = <2>;
            status = "disabled";   // 板级 &gpio { status = "okay"; } 打开
        };

        i2c0: i2c@1 {
            compatible = "mt-i2c-master";
            reg = <1>;
            #address-cells = <1>;
            #size-cells = <0>;
            i2c-clk = <400000>;   // 总线时钟，板级可改
            status = "disabled";
        };
    };
};
```

### 4.2 产品驱动节点模板

每个产品驱动一个 `board/dtsi/drivers/<chip>.dtsi`，用 `&i2c0 { }` 形式挂到总线，**所有数值 0 占位 + `status = "disabled"`**，板级再打开。池大小自动 = `DTC_GEN_COUNT_<COMPAT_UPPER>`（节点数，缺省 1）。

```dts
// board/dtsi/drivers/bmp280.dtsi（本仓现有模板，照抄即可）
&i2c0 {
bmp280: bmp280@0 {
    compatible = "mt-bmp280";
    reg = <0>;          // I2C 7bit 地址，典型 0x76/0x77
    status = "disabled";
};
};
```

### 4.3 `status` 约定

- `status = "okay"`：设备参与编译与 probe。
- `status = "disabled"`：dtc-lite 仍解析节点（生成 ID / 占位），但**不进 probe 表**，驱动不会被调用。
- 板级通过 `&label { status = "okay"; ...覆盖属性... };` 打开并填真实值。

> **⚠ 覆盖的前提：`label` 必须已在某个 dtsi 里声明过（`label: node@x { }`）**。若 `&label` 引用的标签不存在，dtc-lite **不会报错**，而是自动"虚空创生"一个 `/soc/<label>` 孤儿节点，把 `status` / 覆盖属性挂到它上面——真正的目标节点完全没被改动，导致 `DEV_ID_*` 仍是 `disabled`、属性（如 `clock-frequency`）也没覆盖进去。真实案例：`board.dts` 用 `&cpu0`，但 `cpu.dtsi` 里写的是 `cpu@0`（无 `cpu0:` 标签），结果 `/cpus/cpu@0` 保持 `disabled`，`status="okay"` 和 `clock-frequency=<72MHz>` 全部落到虚空节点。修复：给节点加标签 `cpu0: cpu@0 { ... }`，引用即命中。

---

## 5. 写板级 DTS（完整示例）

板级 `board.dts` 是入口，负责 include SoC dtsi + 打开/覆盖节点：

```dts
// 平台工程 boards/my_board/board.dts
/dts-v1/;

/include/ "my_soc.dtsi"          // 平台 SoC 骨架（BOARD_DTSI_DIR 指向 boards/my_board/dtsi/）
/include/ "bmp280.dtsi"          // 产品驱动模板（中间件 board/dtsi/drivers/，或平台自带）

/ {
    compatible = "vendor,my-board";
    #address-cells = <1>;
    #size-cells = <0>;
};

/* 打开 GPIO 控制器，填真实基地址 */
&gpio {
    status = "okay";
    reg = <0x40021000>;
};

/* 打开 I2C0，挂 BMP280（真实 7bit 地址 0x76） */
&i2c0 {
    status = "okay";
    i2c-clk = <400000>;

    bmp280@76 {
        compatible = "mt-bmp280";
        reg = <0x76>;
        status = "okay";
    };
};
```

> 注意：板级 dts 里只要 `&i2c0` 已在本板定义（来自 `my_soc.dtsi`），`bmp280.dtsi` 里的 `&i2c0 { ... }` 会与板级合并——**模板里的 `status="disabled"` 会被板级 `status="okay"` 覆盖**。若板级已自带 bmp280 节点，则无需再 include 模板。

---

## 6. dtc-lite 生成物详解

### 6.1 `board_nodes.h` —— 设备 ID 枚举

每个 `status="okay"` 的节点（含 `chosen` 等）得到一个 `DEV_ID_<label>` 枚举值，业务/驱动用它索引设备，**不靠字符串查找**：

```c
typedef enum {
    DEV_ID_ = 0,
    DEV_ID_gpio = 1,
    DEV_ID_i2c0 = 2,
    DEV_ID_bmp280 = 3,
    DEV_ID_COUNT = 4
} device_id_t;
```

### 6.2 `dt_config_gen.h` —— 计数与主机常量

节点数 → `DTC_GEN_COUNT_<COMPAT_UPPER>`；还有时钟、tick、主机数量等：

```c
#define DTC_GEN_COUNT_MT_BMP280  1     // bmp280 节点数 → 驱动静态池大小
#define DTC_GEN_COUNT_MT_I2C_MASTER    1
#define DTC_GEN_CPU_CLOCK_HZ        168000000
#define DTC_GEN_TICK_RATE_HZ        1000
#define DTC_GEN_I2C_HOST_MAX        3
```

驱动用它定义静态池（见 §8），**节点增删自动调整池大小，无需手改**。

### 6.3 `board_devtable.c` / `board_probe.c`

- `board_devtable.c`：设备表，含 name / compatible / label / 解析后的属性（int / str / bool / reg / irq）。
- `board_probe.c`：dtc-lite 扫描所有 `DRIVER_REGISTER` 宏生成的 `board_driver_probe_<name>()` 函数表；`board_driver_probe_all()` 遍历设备、按 compatible 匹配驱动、调 probe。

---

## 7. 驱动如何对接 DTS

### 7.1 注册驱动

`DRIVER_REGISTER(name, compat, probe_fn, remove_fn)` —— `compat` 必须与 dtsi 节点的 `compatible` **完全一致**（逗号分隔的字符串）：

```c
// drivers/bmp280/src/bmp280_drv.c
#include "driver.h"
#include "device.h"
#include "dt_config_gen.h"

static int bmp280_probe(struct device* pdev)
{
    /* 见 §8 完整示例 */
}

static int bmp280_remove(struct device* pdev)
{
    /* dev_lifecycle 标准 remove 序列，见 driver.h 注释 */
}

DRIVER_REGISTER(bmp280, "mt-bmp280", bmp280_probe, bmp280_remove);
```

> 运行时**无 strcmp 匹配**：dtc-lite 在编译期把 `DRIVER_REGISTER` 收进 `board_probe.c`，`board_driver_probe_all()` 直接按表调用。改了 compatible 必须重跑 dtc-lite（CMake 自动）。

### 7.2 在 probe 里取属性

`device_get_prop_int / _str / _bool / _int_array`、`device_get_reg / _irq` 全部从编译期表读取：

```c
int reg_addr = device_get_prop_int(pdev, "reg", -1);          // I2C 地址
int clk      = device_get_prop_int(pdev, "i2c-clk", 400000);   // 总线时钟
const char* name = device_get_name(pdev);                     // 节点 label/name
```

### 7.3 取依赖设备（如 I2C 总线）

设备节点若通过 `&i2c0` 挂载，驱动用 `device_get_phandle_dev` 或 `board_dev_get(DEV_ID_i2c0)` 拿到总线设备：

```c
struct device* bus = board_dev_get(DEV_ID_i2c0);   // 编译期枚举，零查找
if (IS_ERR_OR_NULL(bus))
    return PTR_ERR(bus);
```

---

## 8. 完整移植示例（可编译代码）

下面给出一个**自包含的传感器驱动 + 板级 dts + CMake 注入**最小闭环，照抄即可跑通流水线。

### 8.1 驱动：`drivers/bmp280/src/bmp280_drv.c`（节选，真实写法风格）

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "bmp280_drv.h"
#include "bmp280_regs.h"
#include "compiler_compat.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "mini_backend.h"
#include "status.h"
#include "system_log.h"
#include "vfs-i2c.h"
#include <stddef.h>
#include <stdint.h>

/* 池大小由 DTS 节点数自动生成：板级每多一个 bmp280 节点，池自动增大 */
#define BMP280_POOL_COUNT  DTC_GEN_COUNT_MT_BMP280

struct bmp280_device {
    struct device* bus;       /* I2C 总线设备 */
    int            addr;      /* 7bit 地址 */
    /* ... 校准系数、状态 ... */
};

/* 静态池：编译期固定大小，无运行时堆分配 */
static struct bmp280_device s_bmp280_pool[BMP280_POOL_COUNT] MINI_ALIGNED(4);
static uint8_t s_bmp280_used[BMP280_POOL_COUNT] MINI_ALIGNED(4);

static struct bmp280_device* bmp280_claim(void)
{
    for (int i = 0; i < BMP280_POOL_COUNT; i++)
    {
        if (!s_bmp280_used[i])
        {
            s_bmp280_used[i] = 1u;
            return &s_bmp280_pool[i];
        }
    }
    return NULL;
}

static int bmp280_probe(struct device* pdev)
{
    struct bmp280_device* dev = bmp280_claim();
    if (dev == NULL)
        return MINI_ERR_NOMEM;

    /* 1. 取板级属性（reg = I2C 地址） */
    dev->addr = device_get_prop_int(pdev, "reg", -1);
    if (dev->addr < 0)
        return MINI_ERR_INVAL;

    /* 2. 取总线依赖（编译期枚举，零查找） */
    dev->bus = board_dev_get(DEV_ID_i2c0);
    if (IS_ERR_OR_NULL(dev->bus))
        return PTR_ERR(dev->bus);

    /* 3. 绑定生命周期（probe 阶段才可注册，见 driver.h） */
    /* device_lc_bind(pdev); */

    MT_LOG_INFO("bmp280", "probed @0x%02x on i2c0", dev->addr);
    return MINI_OK;
}

static int bmp280_remove(struct device* pdev)
{
    /* dev_lc_remove_start / device_ops_unregister / dev_lc_remove_drain /
       dev_lc_remove_finish 标准序列见 driver.h 注释 */
    return MINI_OK;
}

DRIVER_REGISTER(bmp280, "mt-bmp280", bmp280_probe, bmp280_remove);
```

### 8.2 板级 DTS：`boards/my_board/board.dts`

见 §5（同上，复制即用）。

### 8.3 CMake 注入（平台工程）

平台工程通过以下变量把板级树注入 mini_tree（通常在 `add_subdirectory(mini_tree)` 之前 set）：

```cmake
set(MINI_TREE_BOARD_PORT   ${CMAKE_CURRENT_SOURCE_DIR}/boards/my_board)
set(BOARD_DTS              ${MINI_TREE_BOARD_PORT}/board.dts)
set(BOARD_DTSI_DIR         ${MINI_TREE_BOARD_PORT}/dtsi)   # 含 my_soc.dtsi
# 若需厂商宏：
# set(VENDOR_INC_DIRS      ${MINI_TREE_BOARD_PORT}/vendor_inc)
```

| 变量 | 作用 |
| :--- | :--- |
| `BOARD_DTS` | 完整板级 `.dts` 入口路径 |
| `BOARD_DTSI_DIR` | 板级 `dtsi/` 目录（SoC 骨架等） |
| `MINI_TREE_BOARD_PORT` | 板级端口根目录 |
| `VENDOR_INC_DIRS` | 厂商头（dt-bindings 之外的宏）额外 `-I` |

---

## 9. 应用层移植（把业务跑起来）

> **移植不止是加 VFS/驱动设备节点**——最终目标是让**应用代码**在新平台跑起来。好在**通常只需改 DTS + HAL 即可**：DTS 声明外设与属性，HAL 把底层外设接上，应用层代码（业务任务、设备访问、框架服务）**跨平台零改动**。

### 9.1 应用层移植的组成部分

| 层 | 新平台要做什么 | 是否跨平台 |
| :--- | :--- | :--- |
| 板级入口 `main` | 时钟/堆/控制台 + 两段式点火 | **平台专用**（改） |
| 应用任务模块 `app/<模块>/` | 业务逻辑：任务回调 + 任务注册 | **零改动**（DTS/HAL 就绪后） |
| 设备访问 | `device_find_by_label` / `device_open` / `device_ioctl` | 零改动（靠 DTS label） |
| 框架服务 | 统一接口任务 / EventBus / config_store / 调度器 | 零改动（OS 后端统一） |

### 9.2 API 关键点

- 点火：`mini_tree_pre_os_init()` → `mini_tree_start_tasks()`（**内部已调 `board_driver_probe_all` 完成外设 probe**）→ `xscheduler_start()`。
- 任务创建（**上层统一走 C++ 类封装**，见 §9.4）：
  - 业务侧只做两件事：类内实现协程回调 `void thread(x_task* self)`；实现注册接口 `bool thread_register()`。
  - `thread_register()` 内部按调度后端二选一 —— **类内实现细节，上层不直接调用**：
    - 协调式：`xscheduler_task_create(&tcb, name, cb, period_ms)`（TCB 作为类内静态成员）
    - 抢占式（`XTASK_PREEMPT`）：`x_scheduler_task_create(name, period_ms, priority, cb, param)`
  - OS 后端（FreeRTOS/RT-Thread/mini-os）：`mini_task_create_handle(...)`（线程入口模型；裸机下返回 `MINI_ERR_NOTSUPP`）
- 主循环：裸机 `while(1) x_scheduler_poll()`（或 `mini_tree_system_loop()`）；OS 后端启动各自调度器。

**裸机调度器 tick 源（`xscheduler_start()`）两级选择：**

| 平台 | DTS 是否配 `chosen { scheduler-tim = &timN; }` | 结果 |
| :--- | :--- | :--- |
| Cortex-M | 未配 | **默认 SysTick**（`hal/systick`，架构标准件，零配置开箱即用） |
| Cortex-M | 已配 | **chosen TIM 显式覆盖**（`CHOSEN_SCHEDULER_TIM` 编译期判定，可用通用 TIM 换出 SysTick） |
| RISC-V | **必配** | chosen TIM（mtime / SoC 定时器走普通 `tim` 节点），SysTick 路径编译剔除 |

> **频率走 DTS**：SysTick 的 tick 频率取自 `/chosen` 的 `tick-rate`（`DTC_GEN_TICK_RATE_HZ`），CPU 主频取自 `/cpus/cpu@0` 的 `clock-frequency`（`DTC_GEN_CPU_CLOCK_HZ`）；`hal_systick` 仅写死寄存器基址（`HAL_SYSTICK_BASE`，默认 `0xE000E010`，可覆盖），不写死任何频率。RISC-V 板必须显式在 DTS 配 `scheduler-tim`，否则 `hal_systick_init` 返回 `MINI_ERR_NOTSUPP`，调度器不启动。

#### 9.2.1 SysTick 的 DTS 配置（无 VFS 层，靠 dtc-lite 宏注入）

`dtc-lite` 从非设备节点读属性生成宏到 `dt_config_gen.h`（不走 VFS，理由见 §9.2.3）。在你的 `<soc>.dtsi`（`BOARD_DTSI_DIR` 指向）里：

```dts
/ {
	chosen {
		tick-rate = <1000>;              /* tick 频率 Hz：调度器每 1ms 中断一次 (默认) */
		heap-size = <32768>;             /* 既有：堆大小 */
		/* 仅 RISC-V 或想用通用 TIM 覆盖 SysTick 时加下面这行 */
		/* scheduler-tim = &tim0; */
	};

	cpus {
		#address-cells = <1>;
		#size-cells = <0>;
		cpu@0 {
			device_type = "cpu";
			compatible = "<厂商>,cpu";
			reg = <0>;
			clock-frequency = <72000000>;  /* CPU 主频 (HCLK)，务必填真实值！ */
		};
	};
};
```

| DTS 位置 | 属性 | 生成宏 | SysTick 用途 |
| :--- | :--- | :--- | :--- |
| `/cpus/cpu@0` | `clock-frequency` | `DTC_GEN_CPU_CLOCK_HZ` | 计算 SysTick LOAD 重装载值 |
| `/chosen` | `tick-rate` | `DTC_GEN_TICK_RATE_HZ` | SysTick 中断频率 / `tick_delay` |
| `/chosen` | `scheduler-tim = &timN` | `CHOSEN_SCHEDULER_TIM` | RISC-V 或显式覆盖走通用 TIM |
| `/chosen` | `heap-size` | `DTC_GEN_HEAP_SIZE` | 与 tick 无关（既有配置） |

> **两个约束**
> 1. **`clock-frequency` 必须填真值**：仓库 `board/dtsi/example-soc.dtsi` 模板是占位 `<0>`。若显式写了 `cpus/cpu@0` 但 `clock-frequency` 为 0 或缺失，`dtc-lite` 会生成 `#error`，**编译期直接报错**强制修正（任何非 ESP 的 CPU 都须有主频，SysTick 用它算 LOAD）。未写 `cpus` 的占位验证板/ESP 才兜底 16MHz。
> 2. **`tick-rate` 语义**：调度器按 `tick_delay = 1000 / DTC_GEN_TICK_RATE_HZ` 推进 tick（每中断几 ms）。默认 1000 → 每中断 +1ms；配 500 → 每中断 +2ms（仍保持 1ms 逻辑 tick，正确）；配 >1000 触发亚毫秒兜底（按 1ms）。建议保持 1000。

#### 9.2.2 ESP 特殊：不需要配置 cpus / 主频

**ESP 全系不需要填 `cpus` / `clock-frequency`，SysTick 整层不参与**，三重复合保证：

1. **`hal_systick.c` 不在 ESP 编译列表**：`cmake/esp_idf.cmake` 的 `HAL_SRCS` 独立，不编 `hal_systick.c`。
2. **ESP 强制 `CONFIG_OS_FREERTOS`**（对接 IDF 内核），`xtask_coop/preempt` 不编译，无人调用 `hal_systick_init`；tick 归 **IDF 的 SYSTIMER + FreeRTOS**（`CONFIG_FREERTOS_HZ`）。
3. **ESP 默认板 DTS 无 `cpus`** → dtc-lite 走兜底，不生成 `#error`，编译照常通过。

> ESP 板工程若**显式写了 `cpus/cpu@0` 且 `clock-frequency = <0>`**，会触发 `#error`——但 ESP 正常不应写 cpus（走 IDF 时钟树），保留该约束可拦截复制占位模板漏改的错误。

#### 9.2.3 为什么 SysTick 不走 VFS 层

VFS 层是给 **DTS 设备节点**服务的（probe、open/close、`get_hal_dev`、ioctl）。SysTick **不是设备节点**，故不走 VFS，理由：

| 维度 | 通用 TIM（走 VFS） | SysTick（不走 VFS） |
| :--- | :--- | :--- |
| DTS 角色 | 设备节点（`tim@...` + compatible + probe） | **非设备节点**：无 compatible、不在总线、无寄存器表 |
| 配置来源 | 节点属性，经 `device_get_prop_*` 拉取 | DTS **宏**注入（`DTC_GEN_TICK_RATE_HZ` / `DTC_GEN_CPU_CLOCK_HZ`），调度器直接消费 |
| 操作面 | open/close/ioctl 一整套 | 只有 `init / deinit / irq_handler` 三个 |
| 中断路由 | NVIC + `interrupt_hw_enable` + VIRQ dispatch | **内核异常（异常 15）**，`SysTick_Handler`(weak) → `hal_systick_irq_handler`(强) 直连 |

结论：SysTick 是架构标准件、操作极简、中断是内核异常，硬塞进 VFS 设备模型要造假节点 + `vfs-systick` 驱动 + dtc-lite 支持，纯增复杂度。**调度器直连 `hal_systick_*`** 反而最贴"配置走 DTS 宏、操作走 HAL 直投"的分层。`hal_systick` 与通用 TIM 是两条独立路径，各司其职。

### 9.3 板级入口 —— C 版（`main.c`）

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "system_init.h"
#include "xtask.h"          /* 裸机调度器 (CONFIG_OS_BARE) */
#include "led.h"            /* 应用任务模块 */

/* 平台：时钟 / 堆 / 控制台初始化（HAL 后端，平台专用） */
static void platform_boot(void);

int main(void)
{
    platform_boot();                  /* 平台专用：时钟、SysTick、控制台 */

    mini_tree_pre_os_init();          /* 框架静态初始化 */
    mini_tree_start_tasks();          /* probe 外设 + 启动框架任务 */
    xscheduler_start();               /* 默认 SysTick；DTS 配 chosen 则显式覆盖走通用 TIM */

    App_Led_register();               /* 注册应用任务 */

#if defined(CONFIG_OS_BARE)
    for (;;)
        x_scheduler_poll();           /* 裸机时间片轮询（含抢占式） */
#elif defined(CONFIG_OS_FREERTOS)
    vTaskStartScheduler();
#elif defined(CONFIG_OS_RTTHREAD)
    rt_system_scheduler_start();
#endif
    return 0;
}
```

### 9.4 应用任务模块 —— C++ 版（**推荐**；`app/led/led.hpp` + `led.cpp`）

C++ 侧把任务封装成单例类：**协程回调 `thread()`** + 注册接口 `thread_register()`；静态 TCB 与配置常量都收在类内，不暴露全局。

`led.hpp`（类声明 + 静态 TCB）：

```cpp
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "xtask.h"
#include <cstdint>

struct device;   // 前向声明

namespace App_Led
{
    class Led
    {
    public:
        static Led& get_instance();

        // 禁止拷贝和移动
        Led(const Led&) = delete;
        Led& operator=(const Led&) = delete;
        Led(Led&&) = delete;
        Led& operator=(Led&&) = delete;

        void thread(x_task* self);   // 协程回调：翻转 LED
        bool thread_register(void);  // 注册任务到调度器

    private:
        Led();

        ::device* m_dev = nullptr;

        static constexpr unsigned int kDelay = 500;
        static constexpr unsigned int kPriority = 5;
        static constexpr const char* kName = "Led_Task";
        static constexpr const char* kTag = "Led";

#ifndef CONFIG_XTASK_PREEMPT
        static x_task s_tcb;   // 协调式：静态 TCB
#endif
    };
} // namespace App_Led
```

`led.cpp`（构造里按 label 找设备并打开；协程体用 `PT_BEGIN`/`PT_DELAY` 让出；注册按调度后端二选一）：

```cpp
#include "led.hpp"
#include "device.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"

namespace App_Led
{
#ifndef CONFIG_XTASK_PREEMPT
    x_task Led::s_tcb;   // 协调式 TCB 定义
#endif

    Led& Led::get_instance()
    {
        static Led s_instance;
        return s_instance;
    }

    Led::Led()
    {
        ::device* pdev = device_find_by_label("led");
        if (IS_ERR_OR_NULL(pdev))
        {
            MT_LOG_WARN(kTag, "led device not found");
            return;
        }
        if (device_open(pdev, nullptr) != MINI_OK)
        {
            MT_LOG_ERROR(kTag, "device_open failed");
            return;
        }
        m_dev = pdev;
    }

    /* 协程回调：PT_BEGIN / PT_DELAY 让出（宏内部是无值 return，故返回类型必须是 void） */
    void Led::thread(x_task* self)
    {
        struct vfs_gpio_arg arg = {0};

        PT_BEGIN(self);
        while (true)
        {
            if (m_dev != nullptr)
            {
                int ret = device_ioctl(m_dev, GPIO_CMD_TOGGLE, &arg, sizeof(arg), 100);
                if (ret != MINI_OK)
                    MT_LOG_ERROR(kTag, "ioctl failed: %d", ret);
            }
            PT_DELAY(self, kDelay);
        }
        PT_END(self);
    }

    /* 注册：抢占式 (name, period, priority, cb, param) / 协调式 (task, name, cb, period) */
    bool Led::thread_register(void)
    {
#ifdef CONFIG_XTASK_PREEMPT
        x_task_handle_t handle = x_scheduler_task_create(
            kName, kDelay, kPriority,
            [](x_task* self) { get_instance().thread(self); }, nullptr);
#else
        x_task_handle_t handle = xscheduler_task_create(
            &s_tcb, kName,
            [](x_task* self) { get_instance().thread(self); },
            kDelay);
#endif
        return handle != 0;
    }
} // namespace App_Led
```

> 要点：
> - **回调必须返回 `void`**：`PT_BEGIN` / `PT_DELAY` 内部是无值 `return`，返回 `etl::optional` 之类的类型会编译失败。
> - 抢占式走 `x_scheduler_task_create(name, period_ms, priority, cb, param)`；协调式走 `xscheduler_task_create(&tcb, name, cb, period_ms)`，需要类内静态 TCB。
> - 注册成功后由 `main` 启动调度器（`xscheduler_start()`），入口处逐个调用 `xxx::get_instance().thread_register()`。

---

### 9.5 应用任务模块 —— C 版（兼容用法，上层不推荐；`app/led/led.c` + `led.h`）

`led.h`（声明 + 静态 TCB）：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "xtask.h"          /* x_task / xscheduler_task_create */

#define APP_LED_NAME      "Led_Task"
#define APP_LED_PERIOD_MS 500u

void App_Led_register(void);          /* 任务注册入口（main 调用） */
extern x_task g_led_task;             /* 静态 TCB（协调式需调用方分配） */
```

`led.c`（业务回调 + 注册，协调式 `XTASK_COOP`）：

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "led.h"
#include "device.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"

static const char* const s_kTag = "Led_Task";
x_task g_led_task;                    /* 静态 TCB */

static struct device* s_led_dev = NULL;

/* 任务回调：每次到期翻转 LED（DTS 未配 led 节点时安全空转） */
static void led_task_cb(x_task* self)
{
    struct vfs_gpio_arg arg = {0};
    int ret;
    MINI_IGNORE_RESULT(self);

    if (s_led_dev == NULL)
    {
        struct device* pdev = device_find_by_label("led");
        if (IS_ERR_OR_NULL(pdev))
            return;
        if (device_open(pdev, NULL) != MINI_OK)
        {
            MT_LOG_ERROR(s_kTag, "device_open(led) failed");
            return;
        }
        s_led_dev = pdev;
    }

    ret = device_ioctl(s_led_dev, GPIO_CMD_TOGGLE, &arg, sizeof(arg), 100);
    if (ret != MINI_OK)
        MT_LOG_ERROR(s_kTag, "device_ioctl(TOGGLE) failed: %d", ret);
}

/* 任务注册：协调式新签名（task, name, cb, period_ms） */
void App_Led_register(void)
{
    if (xscheduler_task_create(&g_led_task, APP_LED_NAME, led_task_cb, APP_LED_PERIOD_MS) == 0)
        MT_LOG_ERROR(s_kTag, "register failed");
}
```

---

## 10. 验证流程

1. **genconfig**：`.config` → `config.h`，确认 `CONFIG_*` 与选择一致。
2. **dtc-lite**：编译期自动跑；检查生成 `<build>/generated/board/mini_tree/board_nodes.h` 里出现 `DEV_ID_bmp280`，`dt_config_gen.h` 里 `DTC_GEN_COUNT_MT_BMP280 >= 1`。
3. **编译**：编 `mini_tree` 静态库，确认 `board_driver_probe_bmp280` 被收录、无未定义符号（`board_dev_get` 来自生成的 `board_devtable.c`）。
4. **运行**：`board_driver_probe_all()` 在启动早期遍历，日志应打出 `bmp280 probed @0x76 on i2c0`。
5. **链接**：接平台链接脚本，确认含 `ERR_SECTION_BASE`（见 memory_footprint.md §1）。

---

## 11. 常见坑与排查

| 现象 | 原因 | 解决 |
| :--- | :--- | :--- |
| `DEV_ID_xxx` 未生成 | 节点 `status="disabled"` 或未 include 模板 | 板级 `&label { status = "okay"; }` 打开 |
| `&label { status="okay" }` 覆盖后节点仍是 `DISABLED` / 频率没写进去 | **标签 `label` 未在任一 dtsi 声明**，dtc-lite 不报错而是**虚空创生**一个孤儿节点挂到 `/soc`，覆盖没落到目标节点 | 给目标节点补上标签，如 `cpu0: cpu@0 { ... }`，确保 `&cpu0` 精确命中；`git diff` 对比 `board_devtable.c` 里 `.status` / 属性确认生效 |
| probe 未被调用 | `DRIVER_REGISTER` 的 compat 与 dtsi `compatible` 不一致（空格/大小写） | 严格一致；重跑 dtc-lite |
| 池溢出 / `MINI_ERR_NOMEM` | 板级节点数 > 预期 | `DTC_GEN_COUNT_*` 自动跟随节点数，检查是否漏开节点 |
| 驱动找不到总线 | `board_dev_get(DEV_ID_i2c0)` 返回错误 | 确认 `i2c0` 节点 `status="okay"` 且在 `board_nodes.h` 有枚举 |
| 改了 dts 不生效 | 增量构建未重跑 dtc-lite | 清理 `<build>/generated` 或触发 CMake reconfigure（`.config` / dts 为 `CONFIGURE_DEPENDS`） |
| 厂商宏找不到 | dt-bindings 路径未包含 | 放 `board/dt-bindings/` 或用 `VENDOR_INC_DIRS` |

> **调试提示**：生成物是普通 C 表，直接用 `arm-none-eabi-nm` / 文本 grep `<build>/generated/board/mini_tree/board_probe.c` 看 probe 表是否含你的驱动；`board_nodes.h` 可直接打开看 ID 枚举。

---

## 12. 相关文档

- [driver_guide.md](driver_guide.md) — 驱动编写规范（probe/remove、dev_lifecycle、fops）
- [architecture.md](architecture.md) — 分层与启动时序
- [getting_started.md](getting_started.md) — 构建与集成
- [memory_footprint.md](memory_footprint.md) — 内存/flash 基准（含 `ERR_SECTION_BASE` 说明）

> 本文合并了原 `board_devicetree.md`（设备树机制概要）与 `porting_guide.md`（移植总步骤），相关内容已并入上文各节。

---

> 本文取代并集中了原先散落在 architecture / getting_started / patterns / service_spec / design_decisions 中的设备树/板级移植片段；示例均按本仓当前 `board/dtsi`、`tools/dtc-lite.py`、`drivers/bmp280` 机制编写。
