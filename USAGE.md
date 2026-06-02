# mini_tree 使用手册

> **适用于：** mini_tree 通用嵌入式中间件框架
> **参考硬件：** ESP32-S3 (sound_dsp_project, 0.x 未解耦原型 — **仅驱动写法可参考, 整体架构存在已知问题, 参见 §8.4 说明**)
> **快速入口：** [README.md](README.md) — 架构总览与构建说明

---

## 目录

1. [术语说明](#1-术语说明)
2. [项目结构速览](#2-项目结构速览)
3. [0.x 版本与当前版本的关键差异](#3-0x-版本与当前版本的关键差异)
4. [快速开始](#4-快速开始)
5. [配置系统](#5-配置系统)
6. [用户工程集成](#6-用户工程集成)
7. [点火时序](#7-点火时序)
8. [服务编写规范](#8-服务编写规范)
9. [应用层解耦规范](#9-应用层解耦规范)
10. [硬件移植](#10-硬件移植)
11. [设备树与驱动](#11-设备树与驱动)
12. [调试与监控](#12-调试与监控)
13. [常见问题](#13-常见问题)
14. [多核配置](#14-多核配置)
15. [OSAL_NULL 单元测试](#15-osal_null-单元测试)
16. [Keil MDK 集成说明](#16-keil-mdk-集成说明)
17. [红线区 — 硬实时 Fast Path](#17-红线区--硬实时-fast-path)

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
| **soc_port_** | 具体芯片的 HAL 实现（如 soc_port_esp32） |
| **Scrubber** | 闪存巡检任务，检测 Flash Bit-Rot |
| **TWDT** | 任务看门狗，监控任务是否按时喂狗 |

---

## 2. 项目结构速览

### 当前版本 (decoupled)

目录结构详见 [README.md 目录结构](README.md#目录结构)。

### 0.x 用户工程结构 (sound_dsp_project 参考)

```
sound_dsp_project/           # ESP-IDF 用户工程
├── CMakeLists.txt           # 顶层 CMake, 引入 EXTRA_COMPONENT_DIRS
├── main/
│   ├── main.cpp             # app_main 入口
│   └── main.hpp             # EXTERN_C 宏定义
└── components/
    ├── mini_tree 各组件      # 直接拷贝为 ESP-IDF component
    ├── app/                  # 应用层 (LVGL UI 入口)
    ├── service/              # 业务服务 (audio, ui, cloud, network)
    ├── drivers/              # 外设芯片驱动
    ├── soc_port_esp32/       # ESP32-S3 HAL 实现层
    ├── capability/           # 能力单元 (音频引擎等)
    └── sensor_if/            # 传感器抽象
```

---

## 3. 0.x 版本与当前版本的关键差异

当前版本是从 0.x (sound_dsp_project) 解耦重构而来。理解差异有助于迁移和深度使用。

| 维度 | 0.x (sound_dsp_project) | 当前版本 |
|------|------------------------|----------|
| **构建系统** | ESP-IDF component (`idf_component_register`) | 独立 CMake 静态库 (`add_library`) |
| **OSAL** | 直接调用 FreeRTOS API | OSAL 三后端统一抽象 |
| **点火方式** | 单阶段 `SystemRuntime::start()` | 两阶段 `Pre_OS_Init()` + `Start_Tasks()` |
| **业务耦合** | `system_runtime.cpp` 直接引 AudioService/CloudService | system 层零业务引用 |
| **网络栈** | `esp_netif_init()` 硬编码在 system_runtime | 由用户工程自行管理 |
| **C++ 标准** | C++11 | C++23 |
| **C 标准** | C11 | C23 |
| **Kconfig** | 无统一 Kconfig | 完整 menuconfig 体系 |
| **反汇编** | 无 | `CONFIG_BUILD_DISASM` 自动生成 .lst |
| **系统后端** | 仅 C++ | C++ / C 双后端 Kconfig 选择 |

### 解耦前后点火对比

**0.x 单阶段点火 (sound_dsp_project)：**
```cpp
// main.cpp
void app_main(void) {
    // 提前 touch 相关 Singleton, 防 ISR 死锁
    (void)EventBus::getInstance();
    (void)KeyInput::getInstance();
    (void)AudioService::getInstance();
    // ...

    SystemRuntime::getInstance().start();  // 一口气全做
}
```

**当前版本两阶段点火：**
```cpp
// main.cpp
int main(void) {
    platform_init();
    platform_register_drivers();

    MiniTree::System_Pre_OS_Init();  // Phase 1: 看门狗 + EventBus
    MiniTree::System_Start_Tasks();  // Phase 2: Probe + Scrubber

    vTaskStartScheduler();           // RTOS 接管
}
```

---

## 4. 快速开始

### 4.1 一键构建 (推荐)

```bash
# 构建: python tools/p2s.py -p <platform> -t <toolchain> -o <osal>
python tools/p2s.py -p arm_cm3 -t gcc -o freertos
python tools/p2s.py -p arm_cm4f -t keil5 -o rtthread
python tools/p2s.py -p arm_cm7 -t keil5 -o null
python tools/p2s.py -l          # 列出可用组合
python tools/p2s.py --menuconfig  # 配置后构建
python tools/p2s.py --clean       # 清理
```

### 4.2 作为独立静态库引入 (推荐)

用户工程的 `CMakeLists.txt`：

```cmake
add_subdirectory(path/to/mini_tree)
target_link_libraries(my_app PRIVATE mini_tree)
```

用户需提供：
- `hal_*` 符号（通过 soc_port_ 实现）
- FreeRTOS 后端：`FreeRTOSConfig.h`
- RT-Thread 后端：RT-Thread 内核配置

### 4.3 作为 ESP-IDF 组件 (0.x 兼容方式)

参考 sound_dsp_project，将各组件放入 `components/` 目录：

```cmake
# 顶层 CMakeLists.txt
list(APPEND EXTRA_COMPONENT_DIRS components/soc_port_esp32)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_project)
```

每个组件目录内使用 `idf_component_register`：

```cmake
# components/core/CMakeLists.txt
idf_component_register(
    SRCS "src/event_bus.cpp" "src/buffer_pool.c"
    INCLUDE_DIRS "include"
    REQUIRES osal board
)
```

---

## 5. 配置系统

### 5.1 menuconfig 图形化配置

```bash
python tools/menuconfig.py
```

### 5.2 核心配置项

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
| **System Log Config** | `SYS_LOG_USE_OSAL` | 通过 OSAL 日志 |
| | `SYS_LOG_USE_ESP` | ESP-IDF 日志 (ESP 平台) |
| | `SYS_LOG_USE_PRINTF` | printf 直接输出 (裸机) |
| **Board Features** | `PRODUCTION_LOG` | NVS 持久化日志环缓冲 |
| | `SAFETY_SHUTDOWN` | IEC 61508 安全停机 |
| **Build Options** | `BUILD_DISASM` | 构建期自动生成反汇编 |

### 5.3 配置值来源优先级

1. `.config` 文件 (menuconfig 生成)
2. `-DFREERTOS_PORT=GCC_ARM_CM3` 等 CMake 变量 (跳过 Kconfig)
3. 默认值 (无配置时)

### 5.4 手动配置 (无 menuconfig 环境)

当无法运行 menuconfig 时（CI 环境、无 Python 依赖、快速测试），可使用手动配置：

```bash
# 将模板复制到 Kconfig 生成路径，编辑后正常构建
cp config.example.h build/generated/kconfig/config.h
# 编辑 config.h，取消注释所需选项
```

配置文件范例见项目根目录 [`config.example.h`](config.example.h)。选项默认注释，取消注释即启用。

### 5.5 在代码中使用配置

配置由 `genconfig.py` 转化为 `build/generated/kconfig/config.h`（或手动填写）：

```c
#include "config.h"

#if defined(CONFIG_OSAL_FREERTOS)
    // FreeRTOS 特定代码
#elif defined(CONFIG_OSAL_NULL)
    // 裸机特定代码
#endif
```

---

## 6. 用户工程集成

### 6.1 硬件初始化

用户需要实现以下硬件抽象。参考 0.x 的 `soc_port_esp32/`：

```c
// soc_port_esp32/src/gpio.c
#include "hal_gpio.h"

void hal_gpio_init(hal_gpio_t* obj, uint32_t pin)
{
    // 配置 ESP32-S3 GPIO
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

void hal_gpio_set_level(hal_gpio_t* obj, uint32_t pin, uint32_t level)
{
    gpio_set_level(pin, level);
}
```

每个 HAL 接口文件对应 `hal_if/include/` 中的一个插座定义。

### 6.2 CMake 集成

```cmake
# 用户工程 CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(my_app)

# 引入 mini_tree
add_subdirectory(components/mini_tree)

# 注册 soc_port (HAL 实现)
add_subdirectory(components/soc_port_mychip)

# 链接
target_link_libraries(my_app.elf PRIVATE mini_tree soc_port_mychip)
```

对于 ESP-IDF 环境，参考 0.x 方式：

```cmake
list(APPEND EXTRA_COMPONENT_DIRS components/mini_tree/core)
list(APPEND EXTRA_COMPONENT_DIRS components/mini_tree/osal)
# ... 逐个添加
```

---

## 7. 点火时序

### 7.1 标准两段式点火 (推荐)

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
    //   - RTC 硬件看门狗初始化 (独立时钟)
    //   - 设备树数据结构 init
    //   - EventBus init (创建队列)

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
    //   - EventBus seal 封表 (禁止运行时 subscribe)

    /* ─── Step 5: 用户创建自有业务任务 ─── */
    xTaskCreate(my_app_task, "app", 4096, NULL, 5, NULL);

    /* ─── Step 6: 启动调度器 ─── */
#if CONFIG_OSAL_NULL
    while (1) { mini_tree_system_loop(); }  // 裸机轮询
#else
    vTaskStartScheduler();                  // RTOS 接管
#endif
}
```

### 7.2 0.x 单阶段兼容方式

如果直接从 sound_dsp_project 迁移，可保留单阶段入口：

```cpp
#include "system_runtime.hpp"

void app_main(void)
{
    // Touch 相关 Singleton (防 ISR 死锁)
    (void)EventBus::getInstance();
    (void)AudioService::getInstance();
    (void)UiService::getInstance();

    SystemRuntime::getInstance().start();
}
```

> 注意：0.x 方式存在业务耦合——`system_runtime.cpp` 直接引用了 AudioService、CloudService 等业务服务。当前版本已解耦，system 层不包含业务引用。

---

## 8. 服务编写规范

### 8.1 Meyers Singleton 模式

服务使用 C++11 线程安全的局部静态变量：

```cpp
// audio_service.hpp
class AudioService {
public:
    static AudioService& getInstance();

    bool init();
    bool start();
    void stop();
    // ...

private:
    AudioService() = default;
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;

    bool m_inited = false;
    bool m_started = false;
};

// audio_service.cpp
AudioService& AudioService::getInstance()
{
    static AudioService service;    // C++11 保证线程安全
    return service;
}
```

### 8.2 生命周期规范

服务应遵循 `init → start → [stop/suspend/resume]` 生命周期：

| 方法 | 何时调用 | 做什么 |
|------|---------|--------|
| `init()` | Phase 1 / 构造函数 | 分配资源，注册 EventBus 订阅 |
| `start()` | Phase 2 / 任务入口 | 启用硬件，开始工作 |
| `stop()` | 系统停机 | 关闭硬件，释放资源 |
| `suspend()` | 低功耗 | 暂停运行，保持配置 |
| `resume()` | 唤醒 | 从暂停点恢复 |

### 8.3 EventBus 通信

> **重要约束**:
> - **seal 封表**: subscribe() 只能在 Phase 2 点火完成前调用. 封表后 subscribe() 返回 false.
> - **SIOF 防御**: post() 在 Phase 1 完成前静默丢弃事件, 防止 C++ 全局构造函数在 main() 前偷跑.

```cpp
// 订阅事件
EventBus::getInstance().subscribe(
    EVENT_SYS_READY, EVENT_SYS_READY,  // 单事件区间
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

### 8.4 驱动写法参考

> **注意**: sound_dsp_project 是 mini_tree 解耦前的 0.x 原型, 未经过 RT-Thread 适配和系统级优先级审计. 其中发现的许多内存和优先级问题已在 mini_tree 中修复. **仅驱动层的 `DRIVER_REGISTER`、`device_t` 属性读取、goto 清理等写法值得参考, 整体架构和服务层请以前文的 mini_tree 两段式点火为准.**

声明的设备通过 `device_t*` 驱动 API 访问, 驱动实现示例:

**UiService** (ui_service.cpp)：
- 管理 LVGL 初始化与屏幕生命周期
- 从 `device.h` 获取显示设备和触摸/按键输入设备
- 通过 `system_wdt.hpp` 加入 TWDT 监控

**CloudService** (cloud_service.cpp)：
- 封装 MQTT + TCP 客户端
- 独立 Task 运行，与 UI Task 双核隔离

---

## 9. 应用层解耦规范

应用层代码应遵循事件驱动和 VFS 设备抽象两条原则，避免在业务逻辑中直接操作硬件寄存器或 SDK API。

### 9.1 面向 VFS 设备树编程

业务层通过 VFS 节点操作硬件，不直接调用芯片 SDK 接口：

```cpp
// 通过 VFS 操作硬件，更换平台时应用层无需修改
void led_status_task(void* param) {
    int fd = vfs_open("/dev/gpio_led", O_WRONLY);
    while (1) {
        uint8_t level = 1;
        vfs_write(fd, &level, 1);
        osal_task_delay(500);
    }
}
```

这样做的原因是：VFS 节点将底层 HAL 实现（可能是 ESP32 的 `gpio_set_level`，也可能是 STM32 的 `HAL_GPIO_WritePin`）屏蔽在驱动层。业务代码只依赖 `vfs_open/write/ioctl`，与具体芯片无关。

### 9.2 EventBus 事件驱动

模块间通过 EventBus 发布订阅通信，避免直接耦合：

```cpp
// UI 层：调整音量时发送事件，不直接调用 AudioService
void lvgl_volume_knob_callback(lv_event_t* e) {
    uint32_t volume = get_knob_value();
    EventBus::getInstance().post(EVENT_AUDIO_VOLUME_CHANGED, volume);
}

// 音频层：在独立上下文中订阅处理
void audio_service_callback(const Event& event, void* user_data) {
    uint32_t new_volume = event.payload;
    set_dsp_hardware_gain(new_volume);
}
```

EventBus 的发布者和订阅者无需感知对方存在。UI 模块不引用 AudioService，AudioService 不引用 UI 模块。新增功能只需注册新的事件 ID 和对应的订阅者，不影响现有模块。

### 9.3 参考项目

[esp32_sound_project](https://github.com/H-000-H/sound_dsp_project) 是 mini_tree 解耦前的 0.x 原型 (ESP32-S3 + LVGL 9.5 音频项目)。**注意**: 该项目是解耦前单体架构, 未经过 RT-Thread 适配和系统级优先级审计, 其中发现的内存和优先级问题已在 mini_tree 中修复。仅驱动层的 `DRIVER_REGISTER`、`device_t` 属性读取、goto 清理等写法可参考 (参见 §8.4), 整体架构和服务层请以 mini_tree 当前版本为准。具体驱动事例待完成。

---

## 10. 硬件移植

### 10.1 移植工作流

```
用户 SoC (如 ESP32-S3)               抽象接口 (hal_if/)
        │                                   │
        ├── GPIO  ──────────────────  hal_gpio.h
        ├── SPI   ──────────────────  hal_spi_bus.h
        ├── I2C   ──────────────────  hal_i2c.h
        ├── I2S   ──────────────────  hal_i2s_bus.h
        ├── PWM   ──────────────────  hal_pwm.h
        ├── UART  ──────────────────  hal_uart.h
        ├── ADC   ──────────────────  hal_adc.h
        ├── WDT   ──────────────────  hal_wdt.h
        ├── CPU   ──────────────────  hal_cpu.h
        └── Flash ──────────────────  hal_flash.h
```

### 10.2 实现 HAL 插座 (Subsystem Ops 模式)

每个 `hal_if/include/` 中的接口在 `hal_if/soc/<chip_name>/` 下提供实现。驱动通过纯虚 `hal_ops` 操作表绑定，核心层不直接引用芯片 SDK 符号：

```c
// 推荐路径: hal_if/soc/esp32s3/hal_gpio_esp32.c
#include "hal_gpio.h"
#include "driver/gpio.h"  // 芯片 SDK 仅在 .c 内部包含，不泄露到头文件

/* 1. 实现底层的无状态契约 */
static int esp32_gpio_set_level(device_t* dev, int level) {
    int pin = (int)(intptr_t)dev->priv_data;
    return gpio_set_level((gpio_num_t)pin, level);
}

/* 2. 实例化 Ops 表 (挂入 device_t->subsys_priv) */
static const hal_gpio_ops_t s_esp32_gpio_ops = {
    .set_level = esp32_gpio_set_level,
};
```

### 10.3 移植模板

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

## 11. 设备树与驱动

### 11.1 设备树文件 (board.dts)

硬件拓扑在 `board/board.dts` 中声明：

支持 `#include` 预处理，可使用 dt-bindings 头文件中的宏常量：

```dts
/dts-v1/;

#include "dt-bindings/gpio.h"

/ {
    compatible = "my-project";

    cpus {
        cpu@0 {
            compatible = "esp32,cpu";
        };
    };

    soc {
        uart@0 {
            compatible = "esp32,uart";
        };

        i2c@0 {
            compatible = "esp32,i2c-bus";
        };

        gpio@0 {
            compatible = "esp32,gpio";
        };
    };
};
```

### 11.2 DTS 属性参考

DTS 文件中的每个设备节点通过 `key = value;` 声明属性，驱动在 probe 时通过设备 API 读取。

#### 属性类型

| DTS 写法 | C 语言对应 | 读取 API |
|----------|-----------|---------|
| `status = "okay";` | 字符串 | `device_get_prop_str(dev, "status", &val)` |
| `max-speed = <1000000>;` | 整数 | `device_get_prop_int(dev, "max-speed", &val)` |
| `enable-dma;` | 布尔 (true) | `device_get_prop_bool(dev, "enable-dma", &val)` |
| `label = "spi1";` | 字符串 | `device_get_prop_str(dev, "label", &val)` |

#### 框架内置属性

这些属性由 dtc-lite 或 board 层自动识别，非自定义：

| 属性 | 作用 | 取值示例 |
|------|------|---------|
| `compatible` | 驱动匹配字，与 `DRIVER_REGISTER` 的 compatible 对应 | `"my,i2c-touch"` |
| `status` | 编译期启禁 | `"okay"` / `"disabled"` |
| `depends-on` | 依赖的设备（phandle 引用），驱动在此之后 probe | `<&i2c0>` |
| `criticality` | 安全关键等级，probe 失败时的系统行为 | `"fatal"` / `"warning"` / `"ignore"` |
| `reg` | 寄存器地址或设备地址 | `<0x40021000>` |
| （宏常量） | `#include` 头文件中的 `#define`，在 DTS 中直接使用 | `<GPIO_ACTIVE_HIGH>` |
| `interrupts` | 中断号 | `<25>` |
| `label` | 人类可读名 | `"spi_bus1"` |

#### chosen 和 aliases

`/chosen` 和 `/aliases` 是特殊节点，用于固定关键设备 ID，避免硬编码：

```dts
/ {
    chosen {
        console   = <&uart0>;     /* 生成 CHOSEN_CONSOLE 宏 */
        tick-rate = <1000>;       /* 生成 DTC_GEN_TICK_RATE_HZ */
        heap-size = <32768>;      /* 覆盖 configTOTAL_HEAP_SIZE */
    };

    aliases {
        touch = <&touch_dev>;     /* 生成 ALIAS_TOUCH 宏 */
    };

    i2c0: i2c@0 {
        compatible = "esp32,i2c-bus";
    };

    touch_dev: touch@0 {
        compatible = "my,i2c-touch";
    };
};
```

C 代码中通过宏直接引用：

```c
#include "board_handles.h"

device_t* console = board_dev_get(CHOSEN_CONSOLE);
device_t* touch   = board_dev_get(ALIAS_TOUCH);
```

#### 自定义属性

DTS 中声明的非内置属性，驱动均可在 probe 时读取：

```dts
spi_display: display@0 {
    compatible = "my,spi-lcd";
    reg = <0>;                       // SPI 片选号
    width  = <240>;                  // 自定义: 屏幕宽
    height = <320>;                  // 自定义: 屏幕高
    fps    = <60>;                   // 自定义: 帧率
};
```

驱动中读取：

```c
int board_driver_probe_spi_lcd(device_t* dev)
{
    int width, height, fps;

    device_get_prop_int(dev, "width",  &width);
    device_get_prop_int(dev, "height", &height);
    device_get_prop_int(dev, "fps",    &fps);

    return 0;
}
```

#### 多参数解析 (Array Parsing)

DTS 中支持多值数组属性，如 `reg = <0x40013000 0x400>;`。`dtc-lite.py` 将其编译为空格分隔的字符串，驱动通过 `device_get_prop_int_array` 读取：

```c
int board_driver_probe_spi(device_t* dev)
{
    int reg_info[2] = {0};

    if (device_get_prop_int_array(dev, "reg", reg_info, 2) == 2) {
        int base_addr = reg_info[0];   // 0x40013000
        int mem_size  = reg_info[1];   // 0x400
        // 初始化真实寄存器...
    }
    return 0;
}
```

#### 依赖关系

用 `depends-on` 声明设备间的 probe 顺序（phandle 引用标签）：

```dts
i2c0: i2c@0 {
    compatible = "esp32,i2c-bus";
};

audio_codec: codec@0 {
    compatible = "my,audio-codec";
    depends-on = <&i2c0>;   /* I2C 初始化后 probe */
};
```

`dtc-lite.py` 用 Kahn 算法按依赖排序。

#### 与 Linux DTS 的差异

mini_tree 的 DTS 是微型子集，**不是完整 Linux DTC**：

| 特性 | Linux DTC | mini_tree dtc-lite |
|------|-----------|-------------------|
| `/include/` 文件包含 | 支持 | 不支持（仅单文件） |
| `&label` overlay | 支持 | 仅解析不处理 |
| `#address-cells` / `#size-cells` | 地址翻译 | 仅识别不展开 |
| phandle 自动分配 | 自动 | 用 label 替代 |
| 编译输出 | dtb 二进制 | 直接生成 C 代码 |

**无需学 Linux DTS 的寻址模型**。mini_tree 的 DTS 就是"节点 + 键值对"，属性在驱动中用 `device_get_prop_*` 直接读，没有地址翻译和中断级联。

### 11.3 dtc-lite 编译流程

编译期由 `tools/dtc-lite.py` 处理 DTS：

1. **解析** — 读取 DTS 节点树
2. **拓扑排序** — Kahn 算法处理 `depends-on` 依赖
3. **代码生成** — 输出到 `build/generated/board_*.c/.h`
   - `board_devtable.c` — 设备表 (`.rodata` 结构数组)
   - `board_probe.c` — Probe 排序函数
   - `board_force_link.c` — 构造函数强制链接驱动
   - `board_nodes.h` — 节点 ID 枚举
   - `board_handles.h` — 设备句柄声明
   - `dt_config_gen.h` — DTS 配置宏

### 11.4 驱动注册与 Probe

```c
// 驱动注册 (构造函数自动调用)
DRIVER(board_safety_hw) {
    .compatible = "board,safety-hw",
    .probe = board_driver_probe_board_safety_hw,
    .remove = board_driver_remove_board_safety_hw,
};

// Probe 函数实现
int board_driver_probe_board_safety_hw(device_t* dev)
{
    // 从 DTS 获取硬件配置
    int pin = dt_device_get_irq(dev);
    // 初始化硬件
    hal_gpio_init(NULL, pin);
    return 0;
}
```

### 11.5 驱动 Probe 排序

`dtc-lite.py` 使用 Kahn 算法保证 Probe 顺序：

```
I2C 初始化 → I2C 设备 Probe
SPI 初始化 → SPI 设备 Probe
GPIO 初始化 → 按键驱动 Probe
显示控制器初始化 → LVGL 显示 Probe
音频编解码器初始化 → 音频 Probe (最后)
```

### 11.6 驱动 Probe 的 goto 清理模式

内核态驱动普遍使用 goto 实现"单一出口、逆序释放"的资源清理模式. 当 Probe 在多步初始化中间失败时, 已经申请的资源必须按与申请**相反的顺序**释放. goto 标签链天然表达了这种逆序关系.

```c
#define DRV_LOG_TAG  "my_drv"

int board_driver_probe_my_device(device_t* dev)
{
    void*       rx_buf = NULL;
    void*       tx_buf = NULL;
    osal_mutex_t* lock = NULL;
    int         ret;

    /* 第 1 步: 分配互斥锁 */
    if (osal_mutex_create(&lock) != 0)
    {
        DRV_LOGE(DRV_LOG_TAG, "mutex create failed");
        ret = -1;
        goto cleanup;
    }

    /* 第 2 步: 分配 DMA 接收缓冲区 */
    rx_buf = osal_calloc(1, 512);
    if (!rx_buf)
    {
        DRV_LOGE(DRV_LOG_TAG, "rx_buf alloc failed");
        ret = -1;
        goto cleanup_lock;
    }

    /* 第 3 步: 分配 DMA 发送缓冲区 */
    tx_buf = osal_calloc(1, 512);
    if (!tx_buf)
    {
        DRV_LOGE(DRV_LOG_TAG, "tx_buf alloc failed");
        ret = -1;
        goto cleanup_rx;
    }

    /* 第 4 步: 硬件初始化 (假如失败) */
    if (!hal_spi_bus_init(NULL, 1000000))
    {
        DRV_LOGE(DRV_LOG_TAG, "spi init failed");
        ret = -1;
        goto cleanup_tx;
    }

    /* 资源就绪, 挂入设备节点 */
    dev->priv_data = rx_buf;           /* 驱动持有 rx_buf, tx_buf 从 rx_buf 偏移得到 */
    dev_set_priv(dev, rx_buf);
    ret = 0;
    goto cleanup;                      /* 成功也走同一个出口 */

    /* ── 逆序释放 ── */
cleanup_tx:
    osal_free(tx_buf);
cleanup_rx:
    osal_free(rx_buf);
cleanup_lock:
    osal_mutex_destroy(lock);
cleanup:
    return ret;
}
```

**关键原则**:

| 原则 | 说明 |
|------|------|
| 标签名**标注释放对象** | `cleanup_rx` / `cleanup_tx` 直接表明释放什么, 而非泛化的 `err1` / `err2` |
| 标签链**严格逆序** | 第 3 步失败 → 释放第 2 步的资源 (rx), 不碰第 1 步 (lock). 每个标签不做多余的事 |
| 成功路径也走 goto | 确保出口统一经过同一段代码, 避免后续修改时漏掉清理逻辑 |
| 变量在函数顶**全初始化** | `rx_buf = NULL`, `lock = NULL`, 确保标签路径上的 `osal_free(NULL)` / `osal_mutex_destroy(NULL)` 安全 |

这个模式不限于 Probe, 也适用于驱动 `remove`、DMA 描述符链构建、多步骤芯片配置序列等需要"分配到一半失败也要干净回滚"的场景.

### 11.7 驱动编写规范

#### 生命周期与状态机

驱动方法在 `ops` 结构体中注册, 遵循 `probe → open → read/write/ioctl → close → remove` 的生命周期. 驱动不应假设方法调用顺序, 每次调用前通过 `device_get_status(dev)` 检查当前设备状态.

```
UNINIT → READY (dtc-lite 初始化完成)
READY  → PROBED (probe 成功)
PROBED → RUNNING (open 成功)
RUNNING → SUSPENDED (suspend)
RUNNING → PROBED (close)
PROBED → REMOVED (remove)
PROBED → ERROR | RUNNING → ERROR (硬件故障)
```

#### 设备锁使用

mini_tree 为每个设备预分配了递归互斥锁, 驱动方法无需自建锁:

```c
int my_drv_write(device_t* dev, const void* buf, size_t len, uint32_t timeout_ms)
{
    /* device_write 已在 board_device.c 中持锁调用, 驱动内不重复加锁 */
    /* 但如果驱动内部有额外临界区, 使用 dev->lock: */
    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    /* ... 写硬件寄存器 ... */
    device_unlock(dev);
    return len;
}
```

#### 通过 device_t 访问 DTS 配置

驱动在 Probe 阶段通过 `device_t*` 读取设备树属性, 避免硬编码引脚和参数:

```c
int my_drv_probe(device_t* dev)
{
    int irq_pin, spi_host;

    /* 读取 DTS 属性: 设备树中声明的值自动映射 */
    if (device_get_prop_int(dev, "irq-pin", &irq_pin) != 0) return -1;
    if (device_get_prop_int(dev, "spi-host", &spi_host) != 0) return -1;

    /* 读取字符串属性 */
    const char* label;
    device_get_prop_str(dev, "label", &label);

    /* 通过 phandle 引用获取依赖设备 (如 SPI 总线) */
    device_t* spi_bus = device_get_phandle_dev(dev, "spi-bus");
    if (!spi_bus) return -1;

    /* phandle 对应的节点 label 在 board.dts 中声明 */
    return 0;
}
```

#### ISR 上下文约束

中断服务例程中只能调用 ISR 安全的函数:

```c
/* ISR 中允许 */
void* buf = bp_alloc_isr(pool);         /* BufferPool ISR 安全 */
osal_queue_send(queue, &evt, 0);        /* 自动检测 ISR 上下文 */
event_bus_post(EVENT_X, (uintptr_t)buf);/* 仅入队, 不做遍历 */

/* ISR 中禁止 */
osal_mutex_lock(lock, timeout);         /* 不可在 ISR 中等锁 */
osal_task_delay(ms);                    /* ISR 无任务上下文 */
osal_calloc(1, size);                   /* 堆分配可能触发调度 */
```

需要长时间处理的中断应在 ISR 中快速记录事件并唤醒任务处理, 而非在 ISR 内完成全部工作。

#### 依赖设备访问

驱动通过 `device_get_parent()` 或 `device_get_phandle_dev()` 访问依赖设备. 框架保证依赖设备在 Probe 时已就绪.

```c
device_t* i2c = device_get_parent(dev);         /* deps[0] 即父节点 */
device_t* bus = device_get_phandle_dev(dev, "bus"); /* 按 label 查找 */
```

#### 关键度标记

每个设备有关键度属性 `criticality` (在 DTS 中配置), 影响安全关机的排序:

```c
/* DTS 声明: criticality = "high"; */
if (device_get_criticality(dev) == DEVICE_CRIT_HIGH)
{
    /* 此设备影响系统基本功能, 不可降级运行 */
}
```

| 关键度 | 关机行为 |
|--------|----------|
| `DEVICE_CRIT_LOW` | 可随意停 |
| `DEVICE_CRIT_WARNING` | 停时记录日志 |
| `DEVICE_CRIT_HIGH` | 停前确保数据持久化 |
| `DEVICE_CRIT_FATAL` | 停即进入 Safe State |

### 11.8 DTS 优势 — 实例参考

具体实例见 `examples/porting_template/`：

| 文件 | 说明 |
|------|------|
| `hal_init_stm32.c` | STM32 四种开发风格 (HAL/LL/SPL/寄存器) 统一接入 DTS |
| `hal_init_gd32.c` | GD32 标准外设库风格接入 DTS |

以 SPI 四线整体换引脚为例 —— 从 (MOSI=PA7, MISO=PA6, SCK=PA5, CS=PA4) 换到 (MOSI=PB3, MISO=PB4, SCK=PB5, CS=PB6)：

```dts
spi0: spi@0 {
    mosi  = <3>;         /* 只改这里，无需动 .c */
    miso  = <4>;         /* 只改这里，无需动 .c */
    sclk  = <5>;         /* 只改这里，无需动 .c */
    cs-gpios = <6>;      /* 只改这里，无需动 .c */
};
```

无 DTS 时要改 GPIO 端口、引脚号、AF 映射、时钟使能四处；有 DTS 只改四行属性值。

关键结论：
- **修改引脚只需改 `board.dts` 中的属性值，无需动 `.c` / `.h` 函数**
- GD32 与 STM32 共用同一套 DTS 适配模式，无缝切换
- HAL/LL/SPL/寄存器/GD32 库对同一 DTS 属性的读取方式一致

## 12. 调试与监控

### 12.1 反汇编审查

开启 `CONFIG_BUILD_DISASM=y`，构建后自动生成 `build/disasm/*.lst`：

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

### 12.2 安全监控

| 监控项 | 机制 | 告警阈值 |
|--------|------|---------|
| 栈溢出 | 魔术字扫描 (`system_wdt_stack_check_all`) | 剩余 < 512 字节 |
| Task 卡死 | TWDT 超时复位 | 3 秒未喂狗 |
| CPU 总线死锁 | RTC 独立时钟硬件看门狗 | 8 秒超时 |
| Flash Bit-Rot | CRC32 逐页巡检 | 任意校验失配 |
| Bootloop | NVS 计数器 + FCB 持久化 | ≥ 5 次连续崩溃 |

### 12.3 BufferPool 使用

```c
#include "buffer_pool.h"

// 申请缓冲区
uint8_t* buf = buffer_pool_alloc(128);
if (buf) {
    memcpy(buf, data, len);
    EventBus::getInstance().post(MY_EVENT, (uintptr_t)buf);
    // EventBus 不会释放 buf，接收方用完需要:
    // buffer_pool_free(buf);
}
```

BufferPool 特性：
- 位图管理，O(1) 分配与释放
- 支持 ISR 上下文（原子 CAS 操作）
- 零碎片（固定块大小池）
- 峰值跟踪 (`buffer_pool_peak_usage`)
- 池内存 32 字节对齐, DMA (I2S/SPI) 安全

### 12.4 HardFault 现场调试

嵌入式设备死机后第一要务是定位异常类型和指令地址. ARM Cortex-M 的硬故障现场包含以下关键寄存器:

| 寄存器 | 含义 | 调试方法 |
|--------|------|----------|
| `PC` (R15) | 异常发生时的指令地址 | 反查 `.lst` 文件符号 |
| `LR` (R14) | 返回地址或 EXC_RETURN | 区分线程/Handler 模式 |
| `PSR` | 程序状态寄存器 | 查看溢出/除零/对齐标志 |
| `CFSR` | 可配置故障状态寄存器 (0xE000ED28) | 细分: UsageFault/BusFault/MemFault |
| `HFSR` | 硬故障状态寄存器 (0xE000ED2C) | Forced = 上级故障 escalation |
| `MMFAR` | MemFault 地址寄存器 (0xE000ED34) | 野指针目标地址 |

**定位步骤**:

1. 从调试器或 Core Dump 提取 `PC` 值
2. `arm-none-eabi-objdump -S build/board/src/board_device.lst > board_device.S` — 生成带源码的反汇编
3. 在 `.lst` 中搜索 `PC` 地址, 定位到具体指令和 C 源码行
4. 判断异常类型:

| CFSR 位 | 异常 | 常见原因 |
|----------|------|----------|
| `[9] STKOF` | 栈溢出 | 任务栈分配不足, 检查 `kDispatchStack` 等配置 |
| `[8] UNDEFINSTR` | 未定义指令 | 函数指针被 NULL/野指针覆盖 |
| `[7] INVSTATE` | 无效状态 | 函数指针低 bit 非 1 (ARM Thumb 要求) |
| `[1] INVPC` | PC 装载错误 | EXC_RETURN 被意外修改 |
| `[0] IBUSERR` | 指令总线错误 | 从无效地址取指, 大概率函数指针损坏 |

**典型现场分析**:

```
HardFault: PC=0x40082B9A, CFSR=0x00000100  →  UsageFault[INVSTATE]
```
`INVSTATE` 标志 PC 地址最低位为 0 — C 函数指针的最低位需为 1 (Thumb 态). 说明某个函数指针被 NULL 赋值或结构体偏移计算错误导致 LSB 丢失. 排查 `.ops = {NULL}` 未实现的方法和 `memcpy`/`memset` 覆盖了函数指针表的情况.

```
HardFault: PC=0x40081D44, CFSR=0x00000082  →  MemFault[MMARVALID] + BusFault[PRECISERR]
```
`MMARVALID` 表示 `MMFAR` 寄存器包含合法地址. 读 `MMFAR=0x20001234`, 该地址落在 SRAM 范围内但无合法映射, 通常是访问了已释放的 BufferPool 内存或野指针.

### 12.5 OpenOCD + ST-Link 调试

mini_tree 是中间件库，调试配置由用户工程管理。以下说明供用户工程参考。

> **使用 STM32CubeIDE / STM32CubeMX 的用户工程无需手动编写调试配置**：CubeIDE 会自动生成 OpenOCD 启动脚本和 GDB 调试配置（基于你在 .ioc 中选的芯片型号和调试接口）。直接点 Debug 按钮即可。以下内容适用于**不使用 CubeIDE**（如 CMake + CLI、VSCode、其他 IDE）的项目。

#### 环境准备

| 工具 | 用途 | 获取方式 |
|------|------|---------|
| `arm-none-eabi-gdb` | ARM 架构调试器 (CM3/CM4F/CM7) | STM32CubeCLT / xPack ARM GCC |
| `riscv-none-elf-gdb` | RISC-V 调试器 (RV32) | xPack RISC-V GCC |
| OpenOCD | GDB 服务器 + 烧录器 | 单独安装 (`openocd --version` 确认) |
| ST-Link | 调试器硬件 | 板载或独立 ST-Link/V2/V3 |

**OpenOCD 安装**（当前环境未安装，需自行下载）：
- Windows: 从 [OpenOCD GitHub](https://github.com/openocd-org/openocd) 或 `pacman -S mingw-w64-x86_64-openocd`
- Linux: `sudo apt install openocd`
- macOS: `brew install openocd`

> 若不想安装 OpenOCD，STM32CubeCLT 自带 `ST-LINK_gdbserver.exe`（见 `STLink-gdb-server/bin/`），功能等价，GDB 命令流程相同。

#### 调试流程

```
用户工程 (编译) ──→ demo.elf ──→ OpenOCD (烧录 + GDB 服务) ◄── GDB (断点/单步)
```

**Step 1:** 用户工程生成带调试信息的 .elf（编译时加 `-g`，mini_tree CMake 的 Debug 配置默认已加 `-ggdb`）：

```bash
cmake -B build_debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
cmake --build build_debug
# 输出: build_debug/demo.elf
```

**Step 2:** 启动 OpenOCD（以 STM32F103C8 + ST-Link 为例）：

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg
# 启动后监听 gdb 端口 3333
```

目标配置文件根据具体芯片选择：

| 芯片系列 | OpenOCD target |
|----------|---------------|
| STM32F1 | `target/stm32f1x.cfg` |
| STM32F4 | `target/stm32f4x.cfg` |
| STM32F7 | `target/stm32f7x.cfg` |
| STM32G0 | `target/stm32g0x.cfg` |
| STM32H7 | `target/stm32h7x.cfg` |
| 通用 Cortex-M3 | `target/cortex_m3.cfg` |
| 通用 Cortex-M4 | `target/cortex_m4.cfg` |
| 通用 Cortex-M7 | `target/cortex_m7.cfg` |

**Step 3:** 启动 GDB 连接：

```bash
# ARM 架构 (CM3/CM4F/CM7)
arm-none-eabi-gdb build_debug/demo.elf \
    -ex "target remote :3333" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "continue"

# RISC-V RV32
riscv-none-elf-gdb build_debug/demo.elf \
    -ex "target remote :3333" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "continue"
```

#### 常用调试命令

| 命令 | 说明 |
|------|------|
| `target remote :3333` | 连接 OpenOCD GDB 端口 |
| `monitor reset halt` | 复位并停在复位向量 |
| `load` | 烧录 .elf 到 Flash |
| `continue` | 全速运行 |
| `break main` | 在 main 处设断点 |
| `break EventBus::subscribe` | C++ 符号断点 |
| `break buffer_pool_alloc` | C 函数断点 |
| `stepi` / `next` | 单步指令 / 单步源码 |
| `info registers` | 查看所有寄存器 |
| `monitor arm semihosting enable` | 使能半主机 (半主机输出) |
| `backtrace` | 查看调用栈 |
| `print g_system_os_initialized` | 打印全局变量 |
| `monitor reset` | 软复位目标 |

#### 架构验证结果

| 架构 | GDB | 架构识别 | 指令集 | 状态 |
|------|-----|---------|--------|------|
| ARM Cortex-M3 | `arm-none-eabi-gdb` (13.3.1) | `armv7` | Thumb/Thumb-2 | 通过 |
| ARM Cortex-M4F | `arm-none-eabi-gdb` (13.3.1) | `armv7e-m` | Thumb-2 + FPv4-SP | 通过 |
| ARM Cortex-M7 | `arm-none-eabi-gdb` (13.3.1) | `armv7e-m` | Thumb-2 + FPv5-D16 | 通过 |
| RISC-V RV32 | `riscv-none-elf-gdb` (15.2.0) | `riscv:rv32` | RV32IMAC | 通过 |

#### VSCode 配置 (用户工程建议)

在用户工程根目录创建 `.vscode/launch.json`：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "OpenOCD + ST-Link",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build_debug/demo.elf",
            "cwd": "${workspaceFolder}",
            "MIMode": "gdb",
            "miDebuggerPath": "arm-none-eabi-gdb",
            "miDebuggerServerAddress": "localhost:3333",
            "debugServerPath": "openocd",
            "debugServerArgs": "-f interface/stlink.cfg -f target/stm32f1x.cfg",
            "serverLaunchInTerminal": false
        }
    ]
}
```

### 12.6 J-Link 调试

J-Link (Segger) 支持 ARM 和 RISC-V 两种架构。有两种使用方式：

#### 方式 A：JLinkGDBServer（推荐）

Segger 官方的 GDB 服务器，功能和稳定性优于 OpenOCD + J-Link 组合。

**Step 1:** 启动 JLinkGDBServer：

```bash
# ARM 目标
JLinkGDBServer -device STM32F103C8 -if SWD -speed 4000 -port 2331

# RISC-V RV32 目标 (J-Link PLUS 及以上型号支持)
JLinkGDBServer -device FE310 -if JTAG -speed 4000 -port 2331
```

| 参数 | 说明 |
|------|------|
| `-device` | 目标芯片型号（`JLinkExe -device` 可查看支持列表） |
| `-if` | 调试接口: `SWD` (ARM, 2 线) 或 `JTAG` (ARM/RISC-V, 4 线) |
| `-speed` | SWD/JTAG 时钟频率 (Hz)，ARM 常用 4000，RISC-V 常用 1000 |
| `-port` | GDB 监听端口，默认 2331 |

**Step 2:** 启动 GDB 连接：

```bash
# ARM 架构
arm-none-eabi-gdb build_debug/demo.elf \
    -ex "target remote :2331" \
    -ex "monitor reset" \
    -ex "load" \
    -ex "continue"

# RISC-V RV32
riscv-none-elf-gdb build_debug/demo.elf \
    -ex "target remote :2331" \
    -ex "monitor reset" \
    -ex "load" \
    -ex "continue"
```

> J-Link 的 GDB 端口默认为 2331（区别于 OpenOCD 的 3333），避免同时使用时冲突。

#### 方式 B：通过 OpenOCD + J-Link 配置

若已安装 OpenOCD，将 interface 替换为 J-Link 即可：

```bash
openocd -f interface/jlink.cfg -f target/stm32f1x.cfg
```

接口配置文件对应：

| 调试器型号 | OpenOCD interface |
|-----------|-------------------|
| J-Link / J-Link PLUS | `interface/jlink.cfg` |
| J-Link (SWD 模式) | `interface/jlink_swd.cfg` |

之后 GDB 连接流程与 §12.5 相同（端口 3333）。

#### J-Link 特有命令

| GDB 命令 | 说明 |
|----------|------|
| `monitor reset` | 复位目标（不暂停）|
| `monitor halt` | 暂停目标 |
| `monitor flash download 1` | 启用 Flash 下载 |
| `monitor flash device STM32F407VG` | 指定 Flash 设备 |
| `monitor speed 4000` | 动态调整 SWD 速度 |
| `monitor exec SetRTTSafe 1` | RTT 安全模式 |

#### J-Link vs ST-Link 对比

| 项目 | J-Link | ST-Link + OpenOCD |
|------|--------|------------------|
| 适用架构 | ARM + RISC-V | ARM 为主 |
| 速度 | 可达 50 MHz (J-Link Ultra+) | 通常 4-10 MHz |
| GDB 服务器 | `JLinkGDBServer`（独立进程） | `openocd` |
| 默认端口 | 2331 | 3333 |
| RISC-V 支持 | J-Link PLUS 及以上 | 需 OpenOCD + 对应 target |
| Flash 烧录 | 集成在 GDB server 中 | OpenOCD 的 `load` 命令 |
| 价格 | 商业授权 ($) | 免费（板载 ST-Link） |

**建议**：日常原型用板载 ST-Link 足矣；需要 RISC-V 调试、高速下载、或稳定性和调试功能更强时上 J-Link。

---

## 13. 常见问题

### Q: 如何选择合适的 OSAL 后端？

| 场景 | 选择 |
|------|------|
| 产品级多任务 | FreeRTOS 或 RT-Thread |
| 纯前后台裸机 | OSAL_NULL |
| 资源极度受限 (< 8KB RAM) | OSAL_NULL + system_c |
| 需要 FinSH 调试终端 | RT-Thread |
| 社区生态广泛 | FreeRTOS |

### Q: 如何选择 system 后端？

> **个人推荐：应用层用 C++，工业合规用 C。** 日常原型、消费电子、带 GUI 或复杂业务逻辑的项目选 `SYSTEM_CPP`；医疗器械、功能安全 (IEC 61508 / ISO 26262)、军工等需要纯 C 审计交付的场景选 `SYSTEM_C`。

| 场景 | 推荐选择 |
|------|---------|
| 默认现代 C++ | SYSTEM_CPP |
| 医疗/工控合规要求纯 C | SYSTEM_C |
| 团队 C 技能为主 | SYSTEM_C |
| 使用 C++ 服务 (LVGL 等) | SYSTEM_CPP |
| 快速原型 / 消费电子 | SYSTEM_CPP |
| 功能安全认证交付 | SYSTEM_C |

### Q: 从 0.x (sound_dsp_project) 迁移要点？

1. 将业务服务从 `system_runtime.cpp` 抽出到用户工程
2. 替换 `SystemRuntime::start()` 为两段式点火
3. 将 `esp_netif_init()` 等平台调用移入用户 init
4. 升级 C++ 标准从 C++11 到 C++23
5. 引入 Kconfig 管理配置，替代硬编码宏

### Q: 如何添加新的 HAL 接口？

1. 在 `hal_if/include/` 中声明函数签名
2. 在 `soc_port_` 中实现
3. 驱动通过 `device_t*` 获取硬件配置并调用 HAL

### Q: 为什么需要在启动前 touch 相关 Singleton？

```cpp
(void)EventBus::getInstance();  // 预触摸
```

C++11 局部静态变量的首次初始化使用 `__cxa_guard_acquire` 互斥锁。如果在 ISR 中首次调用 `getInstance()`，互斥锁可能导致死锁。在 RTOS 启动前预先 touch 相关 Singleton 可避免此问题。

当前版本的两段式点火在 Phase 1（RTOS 启动前）自然完成了 Singleton 实例化，无需显式 touch。

### Q: 构建报错 `Target requires the language dialect "C23"`？

ARM GCC 13.3.1 使用 `-std=c2x` 而非 `-std=c23`。使用提供的 toolchain 文件自动处理：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
```

### Q: 反汇编 .lst 文件为空？

确认：
1. `CMAKE_OBJDUMP` 已设置（ARM 工具链自动，MinGW 需手动）
2. 目标文件已成功链接，非零大小
3. 构建系统为 `BUILD_DISASM=y`

---

## 14. 多核配置

mini_tree 的 OSAL 层和 VFS 层天然支持多核 RTOS (FreeRTOS SMP / ESP32 双核). [sound_dsp_project](https://github.com/H-000-H/sound_dsp_project) 中使用了双核隔离方案 (Core 0: 网络, Core 1: UI+音频) 作为参考:

### 14.1 任务绑核

用户工程通过 `osal_task_create_handle` 的 `core_id` 参数指定任务归属核心:

```cpp
/* Core 0: 网络栈 + 云服务 (中断密集型) */
osal_task_create_handle("net", 4096, 5, net_task, NULL, 0, &g_net_task);

/* Core 1: UI + 音频 (计算密集型) */
osal_task_create_handle("ui",  4096, 5, ui_task,  NULL, 1, &g_ui_task);
```

FreeRTOS SMP 下 `core_id = 0` 或 `1` 固定绑定, `core_id < 0` 为不指定 (由调度器自由迁移).

### 14.2 多核下的 EventBus

EventBus 的分发任务运行在创建它的核心上 (通常是调用 `System_Start_Tasks` 的核心). 跨核事件投递经过 `osal_queue_send` 入队, 由分发任务在所属核心上顺序处理. 队列本身是内核对象, 跨核读写由 RTOS 保证原子性.

### 14.3 缓存一致性注意事项

| 场景 | 风险 | 缓解措施 |
|------|------|----------|
| BufferPool 内存跨核访问 | Cache 脏行未刷回 | BufferPool 内存 32 字节对齐, 避免伪共享 |
| EventBus 共享数据 | 一个核心写入, 另核读取 | `osal_queue_send` 自带内存屏障, 无需额外 fence |
| 驱动 `priv_data` 跨核操作 | 核间寄存器竞争 | 驱动应在私有锁保护下操作硬件 |

> 框架不强制 Cache 一致性协议. 双核 Cortex-M7 / ESP32-S3 等平台需平台代码在 DMA 和跨核共享内存边界插入必要的 `DSB`/`ISB`/`__sync_synchronize()` 屏障. 参考 ESP-IDF 的 `spinlock` 和 `cache_sync` 机制.

---

## 15. OSAL_NULL 单元测试

`OSAL_NULL` 后端提供了一种"单线程状态机"运行模式, 可在无 RTOS 的主机上 (MinGW/Linux) 编译执行. 利用此后端可以在 host 环境运行驱动逻辑和业务状态机的单元测试, 无需 JTAG 或物理开发板.

### 15.1 测试编译配置

```bash
# host 本地编译, 全部功能在单线程状态机下运行
cmake -B build_test -DPLATFORM_POSIX=ON -DOSAL_BACKEND=NULL -DSYSTEM_BACKEND=CPP
cmake --build build_test
```

### 15.2 测试场景

```c
// 测试 buffer pool 分配释放
void test_buffer_pool(void)
{
    bp_config_t cfg = {
        .name = "test",
        .buf_size = 64,
        .buf_count = 4,
    };
    bp_t* pool = bp_create(&cfg);
    assert(pool != NULL);

    void* b1 = bp_alloc(pool);
    void* b2 = bp_alloc(pool);
    assert(b1 != NULL && b2 != NULL);
    assert(b1 != b2);  /* 不重叠 */

    bp_free(pool, b1);
    bp_free(pool, b2);

    void* b3 = bp_alloc(pool);
    assert(b3 != NULL);  /* 释放后可重用 */

    bp_destroy(pool);
}

// 测试设备树属性读取
void test_device_props(void)
{
    device_t* dev = board_dev_get(DEV_ID_MY_DEVICE);
    int val;
    int ret = device_get_prop_int(dev, "irq-pin", &val);
    assert(ret == 0);
    assert(val >= 0);
}

// 测试 EventBus 发布订阅
void test_eventbus(void)
{
    EventBus::getInstance().init();
    g_system_os_initialized = true;  /* 绕过 SIOF 防御 */

    bool received = false;
    EventBus::getInstance().subscribe(
        EVENT_SYS_READY, EVENT_SYS_READY,
        [](const Event& e, void* ud) { *(bool*)ud = true; }, &received);

    EventBus::getInstance().post(EVENT_SYS_READY);

    /* OSAL_NULL 下 EventBus dispatch 由 mini_tree_system_loop() 驱动 */
    mini_tree_system_loop();

    assert(received);
}
```

### 15.3 限制

| 特性 | OSAL_NULL 下的行为 |
|------|--------------------|
| `osal_task_create` | 返回 NULL (无多任务) |
| `osal_queue_receive(..., WAIT_FOREVER)` | 退化为自旋等待 |
| `osal_delay_ms(n)` | 忙等待 (占用 CPU) |
| 异步中断 | 无硬件中断, 需手动调用 ISR 模拟函数 |
| 多核并发 | 单线程执行, 无法暴露 SMP 竞态 |

OSAL_NULL 适用于**逻辑和状态机测试** (属性解析、状态转移、BufferPool 位图操作). 并发竞态、中断上下文、DMA 时序等硬件相关测试仍需在目标硬件上执行.

## 16. Keil MDK 集成说明

> **不推荐日常使用 Keil**。项目的构建、验证和优化全部围绕 GCC/Clang 进行。硬件调试优先使用逻辑分析仪和示波器，它们能直接观测信号时序，比 Keil IDE 的调试器更高效。只有遇到 GCC/Clang 无法重现的"神奇"编译错误时，才考虑用 Keil IDE 做调试入口。

### 16.1 工具链选择

Keil MDK 5.38 及以上版本已搭载 Arm Compiler 6 (ARMCLANG, AC6)，基于 LLVM/Clang 后端。**ARMCC v5.06 编译器 (Compiler 5) 已不被支持**，原因：

| 问题 | ARMCC v5 (已废弃) | ARMCLANG AC6 |
|------|-------------------|--------------|
| C 标准 | C99（不支持 C23 特性） | C17 / C2x |
| C++ 标准 | C++03（不支持 `enum class`、`constexpr` 等） | C++17 / C++20 |
| GNU 汇编 | 不支持 `.S` 文件（需手动转换） | 原生支持 |
| `__attribute__((constructor))` | 不支持 | 原生支持 |
| `__atomic_*` 内置函数 | 不支持 | 原生支持 |
| FreeRTOS GCC 端口 | 不兼容 | 可直接编译 |

### 16.2 如何启用 ARMCLANG

在 Keil IDE 中依次选择：

```
Project → Options for Target → Target → ARM Compiler → "Use default compiler version 6"
```

确认当前使用的确实是 Arm Compiler 6 后，即可正常编译。

### 16.3 调试优先方案

| 优先级 | 方案 | 适用场景 |
|--------|------|---------|
| 首选 | **逻辑分析仪** (Saleae/Kingst/DSLogic) | 观察 GPIO/SPI/I2C/UART 信号时序，定位驱动级问题 |
| 首选 | **示波器** | 模拟信号、PWM 占空比、电源噪声、信号完整性 |
| 备选 | Keil IDE 调试器 | 单步调试、寄存器查看、HardFault 现场分析 |

### 16.4 维护说明

- **keil5** — 作者不再适配。欢迎有能力将本架构降级兼容 ARMCC v5 的贡献者通过社区 PR 弥补这一空缺，作者不会主动完成此工作。
- **keil6** — 仅在大版本更新时同步调整，不做小版本追踪。工具链演进和问题修复优先面向 GCC/Clang。
- 欢迎社区贡献 Keil 相关的改进和升级，在不打乱整体架构的前提下乐意接纳。

### 16.5 Keil 影子工程联调指南

需要利用 Keil 的底层调试能力时，推荐以下工作流以确保架构隔离：

1. **在现代 IDE 中完成构建**：
   在 VS Code/Cursor 中使用 CMake 或 Makefile 完成代码编写，确保静态分析零警告。
2. **生成 Keil 影子工程**：
   通过框架提供的模板或 CMake 生成器创建 `.uvprojx`，在 Keil 中打开。
3. **烧录与调试**：
   - 配置目标调试器（J-Link / ST-Link）。
   - 确认编译器已切换至 **AC6 (ARMCLANG)**。
   - 执行 Download 或 Debug 进入硬件调试。
4. **调试反馈闭环**：
   若在调试过程中定位到代码逻辑问题，切换回 VS Code/Cursor 修改并保存，让 Keil 重新编译。不建议在 Keil 编辑器内直接修改代码。
5. **提交前清理**：
   调试结束后，确认 `.gitignore` 已过滤编译中间文件（`.crf`、`.o`、`.d` 等），保持仓库整洁。

---

## 17. 红线区 — 硬实时 Fast Path

### 17.1 红线/蓝线架构原则

代码量占比 95% 的**蓝线区**（UI、网络、按键、日志）强制使用 VFS/OSAL，牺牲性能换取可移植性和安全性。代码量 5% 的**红线区**（电机 FOC、音频 DSP、高频协议）允许暴力开后门，直接操作寄存器，为纳秒级实时性牺牲可移植性。

### 17.2 Fast Path 文件清单

| 文件 | 用途 | 平台通用性 |
|------|------|-----------|
| `hal_gpio_fast.h` | GPIO set/clr/toggle/read 寄存器直写 | ★★★ STM32/GD32/AT32 通用 |
| `hal_cpu_fast.h` | NVIC 使能/禁能/优先级 + ISR 上下文检测 + 全局中断开关 | ★★★ Cortex-M 通用 |
| `hal_cpu_delay.h` | 微秒级硬实时阻塞延时 (DWT/rdcycle) | ★★★ ARM + RISC-V |
| `hal_pwm_fast.h` | 运行时占空比/周期直写（**仅声明 API，无通用实现**） | ☆☆☆ 平台自行实现 |

### 17.3 GPIO Fast Path

"Fast" 的核心含义是 **绕过 VFS** (device_ioctl → 持锁 → 状态检查 → 函数指针跳转)，具体实现因平台而异，不一定是寄存器直写：

| 平台 | 实现方式 |
|------|---------|
| STM32/GD32/AT32 (Cortex-M) | BSRR/ODR/IDR 寄存器直写，单条 STR 指令 |
| ESP32 | 调用 `gpio_set_level()` 等 ESP-IDF API |
| NXP MCUXpresso | 调用 `GPIO_PinWrite()` 等 SDK API |

```c
#include "hal_gpio_fast.h"

/* 各平台统一 API, 底层实现不同 */
hal_gpio_fast_set(GPIOA_BASE, 1U << PIN_LED);
hal_gpio_fast_clr(GPIOA_BASE, 1U << PIN_LED);
hal_gpio_fast_toggle(GPIOA_BASE, 1U << PIN_LED);
uint32_t val = hal_gpio_fast_read(GPIOA_BASE);
```

> 适用于高于 10kHz 的 GPIO 翻转频率。低频操作请走标准 VFS 路径。
> SoC 移植时定义 `HAL_GPIO_FAST_OVERRIDE` 即可替换为平台自己的实现。

### 17.4 CPU / NVIC Fast Path

`hal_cpu_fast.h` 提供不依赖 CMSIS 的 NVIC 操作，以及 DEBUG 模式下的 ISR 安全断言。

```c
#include "hal_cpu_fast.h"

/* NVIC 直写 */
hal_irq_enable(29);             // 使能 TIM3 中断 (IRQn=29)
hal_irq_disable(29);            // 禁能
hal_irq_set_priority(29, 5);    // 设置优先级

/* 全局中断开关 — 临界段保护 */
uint32_t mask = hal_irq_disable_all();
// ... 原子操作 ...
hal_irq_restore(mask);

/* ISR 上下文检测 — 运行时环境判断 */
if (hal_is_in_isr()) {
    // 当前在中断中，仅调用 ISR 安全函数
    osal_queue_send(queue, &evt, 0);
}
```

#### ISR 安全红线

DEBUG 模式下，`HAL_ASSERT_NOT_ISR()` 插入在 VFS 入口（`device_write/read/ioctl/open/close/suspend/resume`）。若在中断中误调 VFS，自动触发 trap 并打印错误信息：

```
[FATAL] VFS call from ISR context! Remove VFS calls from ISR or bypass VFS.
```

Release 模式下断言编译为空，零开销。

### 17.5 PWM Fast Path

`hal_pwm_fast.h` **仅声明 API 签名**，不提供通用实现。因 PWM 定时器寄存器布局因芯片厂商而异，本框架无法给出统一的寄存器偏移。

如果项目的控制环需要在每个周期更新占空比（如电机 FOC 20kHz），用户应在 `hal_if/soc/<chip_name>/` 层自行实现：

```c
// 以 STM32 通用定时器为例 — 放入 soc_port_stm32f4 或类似文件中
#include "hal_pwm_fast.h"

#define STM32_TIM2_BASE    0x40000000UL
#define STM32_CCR1_OFFSET  0x34UL

static inline void hal_pwm_fast_set_duty(uint32_t tim_base, int channel, uint32_t duty)
{
    *(volatile uint32_t*)(tim_base + STM32_CCR1_OFFSET + ((uint32_t)channel << 2)) = duty;
}

// 使用:
hal_pwm_fast_set_duty(STM32_TIM2_BASE, 1, 500);
```

> 未来可能由社区或后续版本补充常见平台的默认实现。当前版本保持架构通用性，不引入平台特定代码。

### 17.6 DMA 块传输约束

DMA 个体传输的 setup 开销远大于传输本身。传 1 字节和传 1KB 消耗几乎相同的配置/中断周期，byte-by-byte 传输 ≈ 放大数百次 setup 开销。

**音频、屏幕刷新等高带宽场景，建议使用块传输：**

```c
#include "hal_dma.h"

/* ✅ 正确: 块传输，一次 DMA 传完整帧 */
hal_dma_config_block(chan, src, dst, 1024, HAL_DMA_WIDTH_WORD);

/* ❌ 错误: 逐字节传，setup 开销爆炸 */
chan->config(chan, src + i, dst + i, 1);   // 实际写到循环里就是灾难
```

DEBUG 模式下 `hal_dma_config_block()` 自动卡住：
- 地址未对齐（addr % width ≠ 0）
- 块大小 < 32 字节
- Release 编译为空，零开销

### 17.7 硬实时微秒延时

`hal_cpu_delay.h` 基于 CPU 硬件周期计数器，提供不受 OS 调度影响的阻塞延时。

```c
#define HAL_CPU_FREQ_HZ  240000000UL
#include "hal_cpu_delay.h"

void init(void) {
    hal_delay_init();                     // 启动 DWT 周期计数器
}

void pulse_us(void) {
    hal_gpio_fast_set(PORT, PIN);         // 置位
    hal_delay_us(10);                     // 精确 10μs
    hal_gpio_fast_clr(PORT, PIN);         // 复位
}
```

| 平台 | 底层机制 | 精度 |
|------|---------|------|
| Cortex-M3/4/7/33 | DWT_CYCCNT (0xE0001004) | 1 cycle (~5ns @ 200MHz) |
| Cortex-M0/M0+ | SysTick->VAL 回退 | 受 OS 配置影响 |
| RISC-V RV32 | rdcycle (mcycle) | 1 cycle |
| 其他 | NOP 循环 (粗略) | 精度差, 仅编译通过 |

> 非标准主频（非整 MHz）自动使用 64 位乘法，精度不损失。

### 17.8 RAM_EXEC — 零抖动代码驻留

Cortex-M7 @400MHz 下 Flash 读速约 40MHz（10+ wait states）。Cache 命中时取指 1 周期，Miss 时等 Flash 几十周期→控制环抖动。

`RAM_EXEC` 将高频函数搬移到 ITCM/DTCM/SRAM，Cache Miss 归零。

```c
#include "compiler_compat.h"

RAM_EXEC void hall_sensor_isr(void)
{
    /* 在 TCM 中执行, 零等待状态 */
    hal_gpio_fast_set(GPIOA_BASE, PIN_MASK);
}
```

**Linker script 改动（以 STM32 ITCM 为例）：**

```ld
/* 在 MEMORY 区声明 ITCM */
MEMORY
{
    ITCM    (rx)  : ORIGIN = 0x00000000, LENGTH = 16K
    FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    RAM     (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
}

/* 添加 .ram_code 段, 加载在 FLASH, 运行在 ITCM */
.ram_code : {
    *(.ram_code*)
} > ITCM AT> FLASH

_sram_code   = ADDR(.ram_code);
_eram_code   = ADDR(.ram_code) + SIZEOF(.ram_code);
_ram_code_flash = LOADADDR(.ram_code);
```

**Startup 搬运（调用 `System_Pre_OS_Init` 前执行）：**

```c
extern uint32_t _sram_code, _eram_code, _ram_code_flash;
memcpy(&_sram_code, &_ram_code_flash, (uint8_t*)&_eram_code - (uint8_t*)&_sram_code);
```

> 需要修改 linker script，芯片出厂启动文件可能自带 TCM 复制，先确认。

### 17.9 红线区接入检查清单

在决定对某段代码使用 Fast Path 前，确认以下条件：

- [ ] 该路径的执行频率是否 >10kHz？（低于此阈值走 VFS 即可）
- [ ] 是否可以接受此部分代码丧失跨平台移植性？
- [ ] 调用前，硬件外设时钟和初始化是否已完成？
- [ ] ISR 中是否避免了 VFS/OSAL 锁相关调用？

