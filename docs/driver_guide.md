## 7. 设备树与驱动

### 7.1 设备树文件 (board.dts)

硬件拓扑在 `board/board.dts` 中声明。使用 vendor 中性的 compatible 字符串：

```dts
/dts-v1/;

/ {
    compatible = "my-project";

    cpus {
        cpu@0 {
            compatible = "vendor,cpu";
            clock-frequency = <16000000>;
        };
    };

    soc {
        uart0: uart@0 {
            compatible = "vendor,uart";
            status = "disabled";
        };

        i2c0: i2c@0 {
            compatible = "vendor,i2c-bus";
            status = "disabled";
        };

        gpio0: gpio@0 {
            compatible = "vendor,gpio";
            status = "disabled";
        };
    };
};
```

移植到新芯片时只需改 compatible 前缀、添加引脚属性、将 status 改为 `"okay"`。

### 7.2 DTS 预处理

dtc-lite 支持类 CPP 预处理，可以在 DTS 中使用 `#include` 引入头文件和 `#define` 宏：

```dts
/dts-v1/;

#include "dt-bindings/gpio.h"      // 支持 #include
#include <dt-bindings/pinmux.h>    // 支持 <> 形式

#define UART_BAUD  115200          // 支持 #define

/ {
    uart0: uart@0 {
        compatible = "vendor,uart";
        baud = <UART_BAUD>;        // 宏替换
    };

    gpio0: gpio@0 {
        compatible = "vendor,gpio";
        irq-gpios = <IRQ_EDGE_RISING>;  // 来自 dt-bindings 头文件的宏
    };
};
```

这样可以在头文件中统一管理引脚号和常量，多个板级 DTS 共享同一套 bindings。

### 7.3 DTS 属性参考

| DTS 写法 | C 语言对应 | 读取 API |
|----------|-----------|---------|
| `status = "okay";` | 字符串 | `device_get_prop_str(dev, "status", &val)` |
| `max-speed = <1000000>;` | 整数 | `device_get_prop_int(dev, "max-speed", &val)` |
| `enable-dma;` | 布尔 (true) | `device_get_prop_bool(dev, "enable-dma", &val)` |

#### 框架内置属性

| 属性 | 作用 | 取值示例 |
|------|------|---------|
| `compatible` | 驱动匹配字 | `"vendor,uart"` |
| `status` | 编译期启禁 | `"okay"` / `"disabled"` |
| `depends-on` | 依赖的设备 (phandle 引用) | `<&i2c0>` |
| `criticality` | 安全关键等级 | `"fatal"` / `"warning"` / `"ignore"` |
| `reg` | 寄存器地址或设备地址 | `<0x40021000>` |
| `interrupts` | 中断号 | `<25>` |

#### chosen 和 aliases

```dts
/ {
    chosen {
        console   = <&uart0>;     /* 生成 CHOSEN_CONSOLE 宏 */
        tick-rate = <1000>;
    };

    aliases {
        touch = <&touch_dev>;     /* 生成 ALIAS_TOUCH 宏 */
    };
};
```

C 代码中通过宏直接引用：

```c
#include "board_handles.h"
device_t* console = board_dev_get(CHOSEN_CONSOLE);
device_t* touch   = board_dev_get(ALIAS_TOUCH);
```

#### 依赖关系

用 `depends-on` 声明设备间的 probe 顺序：

```dts
i2c0: i2c@0 {
    compatible = "vendor,i2c-bus";
};

audio_codec: codec@0 {
    compatible = "vendor,audio-codec";
    depends-on = <&i2c0>;   /* I2C 初始化后 probe */
};
```

`dtc-lite.py` 用 Kahn 算法按依赖排序。

#### 与 Linux DTS 的差异

mini_tree 的 DTS 是微型子集，**不是完整 Linux DTC**：

| 特性 | Linux DTC | mini_tree dtc-lite |
|------|-----------|-------------------|
| `#include` / `#define` 预处理 | 独立 cpp 阶段 | 内置类 CPP 预处理 |
| `#address-cells` / `#size-cells` | 地址翻译展开 | 语法支持，值可通过属性读取 |
| `/include/` 文件包含 | 支持 | 不支持（用 `#include` 替代） |
| `&label` overlay | 支持 | 仅解析不处理 |
| phandle 自动分配 | 自动 | 用 label 替代 |
| 编译输出 | dtb 二进制 | 直接生成 C 代码 |

**无需学 Linux DTS 的寻址模型**。mini_tree 的 DTS 就是"节点 + 键值对"，属性在驱动中用 `device_get_prop_*` 直接读。

### 7.4 dtc-lite 编译流程

编译期由 `tools/dtc-lite.py` 处理 DTS：

1. **解析** — 读取 DTS 节点树
2. **拓扑排序** — Kahn 算法处理 `depends-on` 依赖
3. **代码生成** — 输出到 `build/generated/board_*.c/.h`
   - `board_devtable.c` — 设备表 (`.rodata` 结构数组)
   - `board_probe.c` — Probe 排序函数
   - `board_force_link.c` — 构造函数强制链接驱动
   - `board_nodes.h` — 节点 ID 枚举
   - `board_handles.h` — 设备句柄
   - `dt_config_gen.h` — DTS 配置宏

### 7.5 驱动注册与 Probe

```c
// 驱动注册 (构造函数自动调用)
DRIVER(my_uart) {
    .compatible = "vendor,uart",
    .probe = my_uart_probe,
    .remove = my_uart_remove,
};

// Probe 函数
int my_uart_probe(device_t* dev)
{
    int baud;
    device_get_prop_int(dev, "baud", &baud);
    // 初始化硬件
    return 0;
}
```

### 7.6 Probe 排序

```
I2C 初始化 → I2C 设备 Probe
SPI 初始化 → SPI 设备 Probe
GPIO 初始化 → 按键驱动 Probe
显示控制器初始化 → 显示驱动 Probe
```

### 7.7 驱动 Probe 的 goto 清理模式

```c
int my_driver_probe(device_t* dev)
{
    void*       rx_buf = NULL;
    osal_mutex_t* lock = NULL;
    int         ret;

    /* 第 1 步: 分配互斥锁 */
    if (osal_mutex_create(&lock) != 0) {
        ret = -1;
        goto cleanup;
    }

    /* 第 2 步: 分配 DMA 缓冲区 */
    rx_buf = osal_calloc(1, 512);
    if (!rx_buf) {
        ret = -1;
        goto cleanup_lock;
    }

    /* 第 3 步: 硬件初始化 */
    if (!hal_spi_bus_init(NULL, 1000000)) {
        ret = -1;
        goto cleanup_rx;
    }

    /* 成功 */
    dev_set_priv(dev, rx_buf);
    ret = 0;
    goto cleanup;

    /* ── 逆序释放 ── */
cleanup_rx:
    osal_free(rx_buf);
cleanup_lock:
    osal_mutex_destroy(lock);
cleanup:
    return ret;
}
```

### 7.8 驱动生命周期

```
UNINIT → READY (dtc-lite 初始化完成)
READY  → PROBED (probe 成功)
PROBED → RUNNING (open 成功)
RUNNING → SUSPENDED (suspend)
RUNNING → PROBED (close)
PROBED → REMOVED (remove)
```

### 7.9 设备锁

mini_tree 为每个设备预分配递归互斥锁：

```c
int my_drv_write(device_t* dev, const void* buf, size_t len, uint32_t timeout_ms)
{
    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    /* ... 写硬件寄存器 ... */
    device_unlock(dev);
    return len;
}
```

### 7.10 ISR 上下文约束

| 函数 | ISR 安全 | 说明 |
|------|----------|------|
| `bp_alloc_isr` | 是 | BufferPool ISR 分配 |
| `osal_queue_send` | 是 (自动检测) | 仅入队 |
| `event_bus_post` | 是 | 仅入队，不做遍历 |
| `osal_mutex_lock` | 否 | 不可在 ISR 中等锁 |
| `osal_task_delay` | 否 | ISR 无任务上下文 |
| `osal_calloc` | 否 | 可能触发调度 |
