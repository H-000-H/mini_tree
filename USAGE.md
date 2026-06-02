# mini_tree 使用手册

> **适用于：** mini_tree 通用嵌入式中间件框架
> **快速入口：** [README.md](README.md) — 架构总览与构建说明

---

## 目录

1. [术语说明](#1-术语说明)
2. [快速开始](#2-快速开始)
3. [配置系统](#3-配置系统)
4. [用户工程集成](#4-用户工程集成)
5. [点火时序](#5-点火时序)
6. [硬件移植](#6-硬件移植)
7. [设备树与驱动](#7-设备树与驱动)
8. [服务编写规范](#8-服务编写规范)
9. [应用层解耦规范](#9-应用层解耦规范)
10. [调试与监控](#10-调试与监控)
11. [红线区 — 硬实时 Fast Path](#11-红线区--硬实时-fast-path)
12. [常见问题](#12-常见问题)
13. [Keil MDK 集成说明](#13-keil-mdk-集成说明)

---

## 1. 术语说明

| 术语 | 含义 |
|------|------|
| **OSAL** | 操作系统抽象层，统一封装 FreeRTOS / RT-Thread / 裸机接口 |
| **EventBus** | 发布-订阅事件总线，模块间解耦通信 |
| **BufferPool** | 基于位图的无锁内存池，零拷贝消息传递 |
| **DTS** | 设备树源文件 (.dts)，描述硬件拓扑与依赖关系 |
| **VFS** | 拟物化文件系统，设备树的运行时抽象视图 |
| **Phase 1** | RTOS 启动前的早期初始化（看门狗、EventBus 预置） |
| **Phase 2** | RTOS 启动后的驱动探针与任务创建 |
| **hal_if** | 硬件抽象层接口，定义外设操作插座 |
| **soc_port_** | 具体芯片的 HAL 实现（如 soc_port_mychip） |
| **Scrubber** | 闪存巡检任务，检测 Flash Bit-Rot |
| **TWDT** | 任务看门狗，监控任务是否按时喂狗 |

---

## 2. 快速开始

### 2.1 一键构建 (推荐)

```bash
python tools/p2s.py -p arm_cm3 -t gcc -o freertos
python tools/p2s.py -p arm_cm4f -t keil5 -o rtthread
python tools/p2s.py -p arm_cm7 -t keil5 -o null
python tools/p2s.py -l             # 列出可用组合
python tools/p2s.py --menuconfig   # 配置后构建
python tools/p2s.py --clean        # 清理
```

### 2.2 Makefile 构建

```bash
make PLATFORM=arm_cm4f TOOLCHAIN=gcc OSAL_BACKEND=FREERTOS
make PLATFORM=arm_cm4f TOOLCHAIN=clang
make PLATFORM=riscv TOOLCHAIN=gcc OSAL_BACKEND=NULL
make PLATFORM=posix TOOLCHAIN=gcc OSAL_BACKEND=NULL  # 本地测试
```

### 2.3 CMake 构建

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm4f.cmake
cmake --build build
```

### 2.4 作为静态库引入用户工程

```cmake
add_subdirectory(path/to/mini_tree)
target_link_libraries(my_app PRIVATE mini_tree)
```

用户需提供：
- `hal_*` 符号（通过 `soc_port_` 实现）
- FreeRTOS 后端：`FreeRTOSConfig.h`
- RT-Thread 后端：RT-Thread 内核配置

---

## 3. 配置系统

### 3.1 menuconfig 图形化配置

```bash
python tools/menuconfig.py
```

### 3.2 核心配置项

| 菜单 | 选项 | 说明 |
|------|------|------|
| **Platform** → Target MCU | `PLATFORM_ARM_CM3` | ARM Cortex-M3 |
| | `PLATFORM_ARM_CM4F` | ARM Cortex-M4F (FPU) |
| | `PLATFORM_ARM_CM7` | ARM Cortex-M7 |
| | `PLATFORM_RISCV` | RISC-V 32-bit |
| | `PLATFORM_POSIX` | POSIX (本地编译验证) |
| **RTOS Backend** | `OSAL_FREERTOS` | FreeRTOS 后端 |
| | `OSAL_RTTHREAD` | RT-Thread 后端 |
| | `OSAL_NULL` | 裸机 (前后台系统) |
| **System Backend** | `SYSTEM_CPP` | C++ system (默认) |
| | `SYSTEM_C` | 纯 C system |
| **Board Features** | `SAFETY_SHUTDOWN` | IEC 61508 安全停机 |
| **Build Options** | `BUILD_DISASM` | 构建期自动生成反汇编 |

### 3.3 配置值来源优先级

1. `.config` 文件 (menuconfig 生成)
2. CMake 变量 `-DFREERTOS_PORT=GCC_ARM_CM3` (跳过 Kconfig)
3. 默认值 (无配置时)

### 3.4 手动配置 (无 menuconfig 环境)

```bash
cp config.example.h build/generated/kconfig/config.h
# 编辑 config.h，取消注释所需选项
```

---

## 4. 用户工程集成

### 4.1 完整工程结构

```
my_project/
├── CMakeLists.txt
├── my_board.dts                # 板级设备树
├── main.c
├── soc_port_mychip/            # HAL 实现 (需用户编写)
│   ├── CMakeLists.txt
│   ├── hal_gpio.c
│   ├── hal_uart.c
│   ├── hal_spi.c
│   ├── hal_i2c.c
│   ├── hal_wdt.c
│   ├── hal_flash.c
│   ├── hal_cpu.c
│   └── hal_pwm.c
└── mini_tree/                  # 子模块或子目录
    ├── CMakeLists.txt
    └── ...
```

### 4.2 用户 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app)

# 引入 mini_tree
add_subdirectory(components/mini_tree)

# 注册 soc_port (HAL 实现)
add_subdirectory(components/soc_port_mychip)

# 链接
target_link_libraries(my_app.elf PRIVATE mini_tree soc_port_mychip)
```

### 4.3 硬件初始化

用户需实现 `hal_*` 接口。每个 `hal_if/include/` 中的接口在 `soc_port_` 中实现：

```c
// soc_port_mychip/hal_gpio.c
#include "hal_gpio.h"
#include "chip_sdk.h"  // 芯片 SDK，仅在 .c 内部包含

void hal_gpio_init(hal_gpio_t* obj, uint32_t pin)
{
    // 调用芯片 SDK 的 GPIO 初始化
    gpio_config_t cfg = {
        .pin = pin,
        .mode = GPIO_MODE_OUTPUT,
    };
    chip_gpio_config(&cfg);
}

void hal_gpio_set_level(hal_gpio_t* obj, uint32_t pin, uint32_t level)
{
    chip_gpio_write(pin, level);
}
```

---

## 5. 点火时序

### 5.1 标准两段式点火 (推荐)

```c
#include "system_init.h"     // C 版本
// 或
#include "system_init.hpp"   // C++ 版本

int main(void)
{
    /* ─── Step 1: 用户硬件初始化 ─── */
    platform_init();                // 时钟、GPIO、外设电源
    board_hal_init();               // soc_port HAL 驱动安装

    /* ─── Step 2: Phase 1 (RTOS 启动前) ─── */
    mini_tree_pre_os_init();
    // 或 C++:  MiniTree::System_Pre_OS_Init();
    //
    // 做的事:
    //   - Bootloop 防护检查
    //   - RTC 硬件看门狗初始化
    //   - 设备树数据结构 init
    //   - EventBus init

    /* ─── Step 3: 用户注册自有驱动 ─── */
    my_driver_register_all();

    /* ─── Step 4: Phase 2 (RTOS 启动后) ─── */
    mini_tree_start_tasks();
    // 或 C++:  MiniTree::System_Start_Tasks();
    //
    // 做的事:
    //   - EventBus 启动 (创建分发任务)
    //   - 设备树 Driver Probe (拓扑排序)
    //   - TWDT 初始化
    //   - Flash Scrubber 启动
    //   - Bootloop 计数器清零
    //   - EventBus seal 封表

    /* ─── Step 5: 用户创建自有业务任务 ─── */
    osal_task_create("app", 4096, 5, my_app_task, NULL);

    /* ─── Step 6: 启动调度器 ─── */
#if CONFIG_OSAL_NULL
    while (1) { mini_tree_system_loop(); }  // 裸机轮询
#else
    vTaskStartScheduler();                  // RTOS 接管
#endif
}
```

---

## 6. 硬件移植

### 6.1 移植工作流

```
用户 SoC                          抽象接口 (hal_if/)
    │                                   │
    ├── GPIO  ──────────────────  hal_gpio.h
    ├── SPI   ──────────────────  hal_spi_bus.h
    ├── I2C   ──────────────────  hal_i2c.h
    ├── PWM   ──────────────────  hal_pwm.h
    ├── UART  ──────────────────  hal_uart.h
    ├── ADC   ──────────────────  hal_adc.h
    ├── WDT   ──────────────────  hal_wdt.h
    ├── CPU   ──────────────────  hal_cpu.h
    └── Flash ──────────────────  hal_flash.h
```

### 6.2 实现 HAL 插座 (Subsystem Ops 模式)

每个 `hal_if/include/` 中的接口定义一组操作表（纯虚结构体），驱动通过 ops 指针调用，不直接引用芯片 SDK 符号：

```c
// 1. 芯片 SDK 仅在 .c 内部包含
#include "hal_gpio.h"
#include "chip_sdk.h"

// 2. 实现无状态契约函数
static int mychip_gpio_set_level(device_t* dev, int level) {
    int pin = (int)(intptr_t)dev->priv_data;
    return chip_gpio_write(pin, level);
}

// 3. 实例化 ops 表
static const hal_gpio_ops_t s_mychip_gpio_ops = {
    .set_level = mychip_gpio_set_level,
    .init      = mychip_gpio_init,
};
```

核心层、驱动层、应用层只通过 `hal_gpio_ops_t` 操作硬件，与具体芯片解耦。

### 6.3 移植模板

参考 `examples/porting_template/` 中的文件骨架：

| 文件 | 说明 |
|------|------|
| `hal_cpu.c` | CPU 紧急停止、Cache 操作 |
| `hal_flash.c` | Flash 读/写/擦除 |
| `hal_wdt.c` | 硬件看门狗喂狗与超时设置 |
| `hal_storage.c` | NVS 存储接口 |
| `hal_platform_safety.c` | 平台级安全停机 |
| `hal_force_stop.c` | 强制外设停止 |

---

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

### 7.2 DTS 属性参考

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
| 编译输出 | dtb 二进制 | 直接生成 C 代码 |
| phandle 分配 | 自动 | 用 label 替代 |
| 地址翻译 | 完整 | 不展开 |
| overlay | 支持 | 不支持 |

**无需学 Linux DTS 的寻址模型**。mini_tree 的 DTS 就是"节点 + 键值对"，属性在驱动中用 `device_get_prop_*` 直接读。

### 7.3 dtc-lite 编译流程

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

### 7.4 驱动注册与 Probe

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

### 7.5 Probe 排序

```
I2C 初始化 → I2C 设备 Probe
SPI 初始化 → SPI 设备 Probe
GPIO 初始化 → 按键驱动 Probe
显示控制器初始化 → 显示驱动 Probe
```

### 7.6 驱动 Probe 的 goto 清理模式

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

### 7.7 驱动生命周期

```
UNINIT → READY (dtc-lite 初始化完成)
READY  → PROBED (probe 成功)
PROBED → RUNNING (open 成功)
RUNNING → SUSPENDED (suspend)
RUNNING → PROBED (close)
PROBED → REMOVED (remove)
```

### 7.8 设备锁

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

### 7.9 ISR 上下文约束

| 函数 | ISR 安全 | 说明 |
|------|----------|------|
| `bp_alloc_isr` | 是 | BufferPool ISR 分配 |
| `osal_queue_send` | 是 (自动检测) | 仅入队 |
| `event_bus_post` | 是 | 仅入队，不做遍历 |
| `osal_mutex_lock` | 否 | 不可在 ISR 中等锁 |
| `osal_task_delay` | 否 | ISR 无任务上下文 |
| `osal_calloc` | 否 | 可能触发调度 |

---

## 8. 服务编写规范

### 8.1 Meyers Singleton 模式

```cpp
// audio_service.hpp
class AudioService {
public:
    static AudioService& getInstance();
    bool init();
    bool start();
    void stop();

private:
    AudioService() = default;
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;
};

// audio_service.cpp
AudioService& AudioService::getInstance()
{
    static AudioService service;    // C++11 保证线程安全
    return service;
}
```

### 8.2 生命周期规范

| 方法 | 何时调用 | 做什么 |
|------|---------|--------|
| `init()` | Phase 1 | 分配资源，注册 EventBus 订阅 |
| `start()` | Phase 2 | 启用硬件，开始工作 |
| `stop()` | 系统停机 | 关闭硬件，释放资源 |
| `suspend()` | 低功耗 | 暂停运行，保持配置 |
| `resume()` | 唤醒 | 从暂停点恢复 |

### 8.3 EventBus 通信

```cpp
// 订阅事件
EventBus::getInstance().subscribe(
    EVENT_SYS_READY, EVENT_SYS_READY,
    [](const Event& event, void* user_data) {
        // 系统就绪回调
    }
);

// 发布事件
EventBus::getInstance().post(EVENT_MY_FEATURE, (uintptr_t)some_data);

// 事件 ID 分配:
//   0x0000 - 0x0FFF  框架保留
//   0x1000 - 0xFFFF  用户自定义 (EVENT_USER_BASE + n)
```

> **重要约束**:
> - `subscribe()` 只能在 Phase 2 点火完成前调用，封表后返回 false
> - `post()` 在 Phase 1 完成前静默丢弃事件，防止全局构造函数偷跑

---

## 9. 应用层解耦规范

### 9.1 面向 VFS 设备树编程

业务层通过 VFS 节点操作硬件，不直接调用芯片 SDK：

```cpp
void led_status_task(void* param) {
    int fd = vfs_open("/dev/gpio_led", O_WRONLY);
    while (1) {
        uint8_t level = 1;
        vfs_write(fd, &level, 1);
        osal_task_delay(500);
    }
}
```

更换芯片时，业务代码无需修改——底层 HAL 实现切换由设备树和 soc_port 层完成。

### 9.2 EventBus 事件驱动

```cpp
// UI 层：调整音量时发送事件，不直接调用 AudioService
void knob_callback(lv_event_t* e) {
    uint32_t volume = get_knob_value();
    EventBus::getInstance().post(EVENT_AUDIO_VOLUME_CHANGED, volume);
}

// 音频层：在独立上下文中订阅处理
void audio_service_callback(const Event& event, void* user_data) {
    uint32_t new_volume = event.payload;
    set_hardware_gain(new_volume);
}
```

EventBus 的发布者和订阅者无需感知对方存在。

---

## 10. 调试与监控

### 10.1 反汇编审查

开启 `CONFIG_BUILD_DISASM=y`：

```bash
cmake -B build -DCONFIG_BUILD_DISASM=y
cmake --build build
ls build/disasm/
# algorithm.lst  board.lst  core.lst  hal_if.lst  osal.lst  system.lst
```

审查要点：
- BufferPool 无锁分配的原子指令 (`lock cmpxchg`)
- `__builtin_ctz` 的前导零扫描 (`bsf` / `clz`)
- 编译器是否内联了关键路径上的小函数

### 10.2 安全监控

| 监控项 | 机制 | 告警阈值 |
|--------|------|---------|
| 栈溢出 | 魔术字扫描 | 剩余 < 512 字节 |
| Task 卡死 | TWDT 超时复位 | 3 秒未喂狗 |
| CPU 总线死锁 | RTC 硬件看门狗 | 8 秒超时 |
| Flash Bit-Rot | CRC32 逐页巡检 | 任意校验失配 |
| Bootloop | NVS 计数器 | ≥ 5 次连续崩溃 |

### 10.3 HardFault 现场调试

ARM Cortex-M 硬故障关键寄存器：

| 寄存器 | 含义 |
|--------|------|
| `PC` | 异常发生时的指令地址 |
| `LR` | 返回地址或 EXC_RETURN |
| `CFSR` | 细分故障类型 (Usage/Bus/MemFault) |
| `HFSR` | 硬故障状态 (Forced = escalation) |
| `MMFAR` | MemFault 目标地址 |

定位步骤：

1. 从调试器提取 `PC` 值
2. `arm-none-eabi-objdump -S build/board/src/board_device.lst`
3. 在 `.lst` 中搜索 `PC` 地址定位源码行

### 10.4 OpenOCD 调试 (通用)

用户工程自行提供芯片对应的 OpenOCD 配置：

```bash
# 以目标芯片为例，替换为实际芯片配置
openocd -f interface/stlink.cfg -f target/your_chip_target.cfg
```

```bash
arm-none-eabi-gdb build/demo.elf \
    -ex "target remote :3333" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "continue"
```

### 10.5 OSAL_NULL 单元测试

`OSAL_NULL` 后端允许在主机上编译运行测试，无需开发板：

```bash
cmake -B build_test -DPLATFORM_POSIX=ON -DOSAL_BACKEND=NULL
cmake --build build_test
```

```c
// 测试 buffer pool
void test_buffer_pool(void)
{
    bp_config_t cfg = { .name = "test", .buf_size = 64, .buf_count = 4 };
    bp_t* pool = bp_create(&cfg);
    assert(pool != NULL);

    void* b1 = bp_alloc(pool);
    void* b2 = bp_alloc(pool);
    assert(b1 != NULL && b2 != NULL);
    assert(b1 != b2);

    bp_free(pool, b1);
    bp_free(pool, b2);

    void* b3 = bp_alloc(pool);
    assert(b3 != NULL);  /* 释放后可重用 */

    bp_destroy(pool);
}

// 测试 EventBus
void test_eventbus(void)
{
    EventBus::getInstance().init();
    g_system_os_initialized = true;

    bool received = false;
    EventBus::getInstance().subscribe(
        EVENT_SYS_READY, EVENT_SYS_READY,
        [](const Event& e, void* ud) { *(bool*)ud = true; }, &received);

    EventBus::getInstance().post(EVENT_SYS_READY);
    mini_tree_system_loop();

    assert(received);
}
```

---

## 11. 红线区 — 硬实时 Fast Path

### 11.1 红线/蓝线架构原则

代码量 95% 的**蓝线区**强制使用 VFS/OSAL，牺牲性能换取可移植性。代码量 5% 的**红线区**（电机 FOC、音频 DSP、高频协议）允许直接操作寄存器，为纳秒级实时性牺牲可移植性。

### 11.2 Fast Path 文件

| 文件 | 用途 | 通用性 |
|------|------|--------|
| `hal_gpio_fast.h` | GPIO 寄存器直写 | 平台自行实现 |
| `hal_cpu_fast.h` | NVIC + 全局中断 + ISR 检测 | Cortex-M + RISC-V |
| `hal_cpu_delay.h` | 微秒级硬实时延时 (DWT/rdcycle) | ARM + RISC-V |
| `hal_pwm_fast.h` | 运行时占空比直写 (仅声明 API) | 平台自行实现 |

### 11.3 GPIO Fast Path

```c
#include "hal_gpio_fast.h"

/* 各平台统一 API, 底层实现因芯片而异 */
hal_gpio_fast_set(GPIO_PORT_BASE, 1U << PIN_LED);
hal_gpio_fast_clr(GPIO_PORT_BASE, 1U << PIN_LED);
hal_gpio_fast_toggle(GPIO_PORT_BASE, 1U << PIN_LED);
uint32_t val = hal_gpio_fast_read(GPIO_PORT_BASE);
```

> 适用于高于 10kHz 的 GPIO 翻转频率。低频操作请走标准 VFS 路径。
> SoC 移植时定义 `HAL_GPIO_FAST_OVERRIDE` 替换为平台自己的实现。

### 11.4 CPU / NVIC Fast Path

```c
#include "hal_cpu_fast.h"

hal_irq_enable(29);             // 使能中断
hal_irq_disable(29);            // 禁能
hal_irq_set_priority(29, 5);    // 设置优先级

/* 全局中断开关 */
uint32_t mask = hal_irq_disable_all();
// ... 原子操作 ...
hal_irq_restore(mask);

/* ISR 上下文检测 */
if (hal_is_in_isr()) {
    osal_queue_send(queue, &evt, 0);
}
```

### 11.5 PWM Fast Path

`hal_pwm_fast.h` 仅声明 API 签名，不提供通用实现。用户根据芯片定时器寄存器布局自行实现：

```c
#include "hal_pwm_fast.h"

static inline void hal_pwm_fast_set_duty(uint32_t tim_base, int channel, uint32_t duty)
{
    *(volatile uint32_t*)(tim_base + 0x34 + ((uint32_t)channel << 2)) = duty;
}
```

### 11.6 硬实时微秒延时

```c
#define HAL_CPU_FREQ_HZ  240000000UL
#include "hal_cpu_delay.h"

void init(void) {
    hal_delay_init();                     // 启动周期计数器
}

void pulse_us(void) {
    hal_gpio_fast_set(PORT, PIN);
    hal_delay_us(10);                     // 精确 10μs
    hal_gpio_fast_clr(PORT, PIN);
}
```

| 平台 | 底层机制 | 精度 |
|------|---------|------|
| Cortex-M3/4/7 | DWT_CYCCNT | 1 cycle |
| Cortex-M0/M0+ | SysTick 回退 | 受 OS 影响 |
| RISC-V RV32 | rdcycle | 1 cycle |

### 11.7 RAM_EXEC — 零抖动代码驻留

将高频函数搬运到 ITCM/DTCM/SRAM，避免 Flash 等待状态导致的抖动：

```c
#include "compiler_compat.h"

RAM_EXEC void hall_sensor_isr(void)
{
    /* 在 TCM 中执行, 零等待状态 */
    hal_gpio_fast_set(GPIO_PORT_BASE, PIN_MASK);
}
```

需要根据芯片修改 linker script 添加 `.ram_code` 段。

---

## 12. 常见问题

### Q: 如何选择合适的 OSAL 后端？

| 场景 | 选择 |
|------|------|
| 产品级多任务 | FreeRTOS 或 RT-Thread |
| 纯前后台裸机 | OSAL_NULL |
| 资源极度受限 (< 8KB RAM) | OSAL_NULL + system_c |
| 需要 FinSH 调试终端 | RT-Thread |
| 社区生态广泛 | FreeRTOS |

### Q: 如何选择 system 后端？

| 场景 | 推荐选择 |
|------|---------|
| 默认现代 C++ | SYSTEM_CPP |
| 医疗/工控合规要求纯 C | SYSTEM_C |
| 团队 C 技能为主 | SYSTEM_C |
| 功能安全认证交付 | SYSTEM_C |

### Q: 如何添加新的 HAL 接口？

1. 在 `hal_if/include/` 中声明操作表结构体
2. 在 `soc_port_` 中实现
3. 驱动通过 `device_t*` 获取 ops 并调用

### Q: 为什么需要在启动前 touch Singleton？

```cpp
(void)EventBus::getInstance();  // 预触摸
```

C++11 局部静态变量的首次初始化使用 `__cxa_guard_acquire` 互斥锁。如果在 ISR 中首次调用 `getInstance()`，互斥锁可能导致死锁。Phase 1 在 RTOS 启动前执行，自然完成了实例化。

### Q: 构建报错 `Target requires the language dialect "C23"`？

ARM GCC 13.3.1 使用 `-std=c2x` 而非 `-std=c23`。使用提供的 toolchain 文件自动处理：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
```

### Q: 多核配置注意事项？

任务通过 `osal_task_create_handle` 的 `core_id` 参数绑核；EventBus 跨核事件投递由 `osal_queue_send` 保证原子性；BufferPool 内存建议对齐到 cache line 避免伪共享。

---

## 13. Keil MDK 集成说明

> **不推荐日常使用 Keil**。构建和验证优先围绕 GCC/Clang 进行。

### 13.1 工具链选择

Keil MDK 5.38 及以上使用 Arm Compiler 6 (ARMCLANG, AC6)，基于 LLVM/Clang 后端。**ARMCC v5.06 已被项目淘汰**，原因详见 [README.md](README.md) 中的说明。

### 13.2 生成 Keil 工程

```bash
python tools/gen_uvprojx.py --platform arm_cm4f --osal FREERTOS \
    --core MYCHIP_MODEL --clock 168000000 \
    --flash-base 0x08000000 --flash-size 0x100000
```

参数说明：

| 参数 | 说明 |
|------|------|
| `--core` | Keil 设备名（如 STM32F407VG，取决于你的芯片） |
| `--clock` | CPU 频率 (Hz) |
| `--flash-base` | Flash 基地址 |
| `--flash-size` | Flash 大小 (bytes) |

### 13.3 影子工程模式

1. 在 VS Code/Cursor 中完成代码编写和 CMake 构建
2. `python tools/gen_uvprojx.py` 生成 `.uvprojx`
3. 在 Keil 中打开，确认编译器为 **AC6 (ARMCLANG)**
4. 烧录与调试
5. 调试定位到问题后，切回 VS Code/Cursor 修改
