## 6. 硬件移植

### 6.1 移植工作流

```
用户 SoC                          抽象接口 (hal/)
    │                                   │
    ├── GPIO  ──────────────────  hal/gpio/hal_gpio.h
    ├── SPI   ──────────────────  hal/spi/hal_spi.h
    ├── PWM   ──────────────────  hal/pwm/hal_pwm.h
    ├── UART  ──────────────────  hal/uart/hal_uart.h
    ├── ADC   ──────────────────  hal/analog/hal_adc.h
    ├── DAC   ──────────────────  hal/analog/hal_dac.h
    ├── WDT   ──────────────────  hal/system/hal_wdt.h
    ├── RTC   ──────────────────  hal/system/hal_rtc.h
    ├── DMA   ──────────────────  hal/system/hal_dma.h
    ├── SDIO  ──────────────────  hal/system/hal_sdio.h
    ├── CPU   ──────────────────  hal/cpu/hal_cpu.h
    ├── Flash ──────────────────  hal/storage/hal_flash.h
    ├── Storage ────────────────  hal/storage/hal_storage.h
    └── Platform Safety ───────  hal/system/hal_platform_safety.h
```

> HAL 头文件统一厂商中性：仅含 `uintptr_t`/`int`/`void*` 与结构体定义、init 函数、`static inline` 快速路径。所有 vendor 类型由 HAL `.c` 内部 `#include` 厂商头文件并内部 cast。

### 6.2 实现 HAL（硬件直投模式）

mini_tree 不使用 vtable/ops 表，也不存在 `hal_gpio_ops_t`、`s_spi_bus_ops`、`s_uart_bus_ops`、`bus_ops` 等结构。HAL 层采用**硬件直投模式**：DTSI 提供的厂商宏值直接透传给厂商 LL 库/标准外设库/ESP-IDF driver，HAL 零翻译零查表。

#### 6.2.1 三层架构与字段语义

```
VFS 层 (vfs/)        → 设备节点 + file_operations + container_of
   ↓
bus 层 (bus/)        → host/client 池 + atomic ref_count + controller_ops
   ↓
HAL 层 (hal/)        → 结构体定义 + init 函数 + static inline 快速路径
```

- HAL 结构体（`hal_spi_bus_host`、`hal_uart_dev`、`hal_gpio_obj_t` 等）**嵌入** bus/VFS 层结构（非指针），HAL 无池管理、无 alloc/free
- 硬件指针统一使用 `uintptr_t`/`int`/`void*`，HAL `.c` 内部 cast 为厂商类型
- fast-path 缓存：`host->spi = cfg->spi`、`dev->uart = cfg->uart`，避免运行时多级解引用

#### 6.2.2 HAL .c 实现模式

每个 HAL 子系统的实现文件命名遵循 `hal/<periph>/hal_<periph>_<chip>.c` 模式，由各平台项目目录组织（不再有 `HAL_SRCS` 变量）：

```c
/* hal/spi/hal_spi_stm32.c — STM32 实现 */
#include "hal_spi.h"          /* 框架头: 厂商中性, 仅含 uintptr_t/int/void* */
#include "dma.h"
#include "VFS.h"
#include "osal.h"

/* 厂商头文件仅在 .c 内部 include */
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"

/* DTSI 提供的厂商宏值零翻译透传给 LL 库 */
static void hal_spi_config_af_pin(const struct hal_spi_pin_cfg* pin)
{
    GPIO_TypeDef* port = (GPIO_TypeDef*)pin->port;   /* uintptr_t → vendor ptr */
    LL_AHB1_GRP1_EnableClock(pin->clk_periph);       /* DTSI 值直接给 LL 库 */
    LL_GPIO_SetPinMode(port, pin->pin, LL_GPIO_MODE_ALTERNATE);
    if (pin->pin < 0x100U)
        LL_GPIO_SetAFPin_0_7(port, pin->pin, pin->af);
    else
        LL_GPIO_SetAFPin_8_15(port, pin->pin, pin->af);
    LL_GPIO_SetPinOutputType(port, pin->pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port, pin->pin, LL_GPIO_SPEED_FREQ_HIGH);
}

int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,
                          const struct hal_spi_bus_config* cfg)
{
    /* 直接灌入 LL 库, 无 enum 转换无 switch 映射 */
    SPI_TypeDef* spi = (SPI_TypeDef*)cfg->spi;
    LL_APB2_GRP1_EnableClock(cfg->spi_clk_periph);
    /* ... LL_SPI_Init(spi, &init_struct) ... */
    host->spi = cfg->spi;          /* fast-path 缓存 */
    host->hw_inited = true;
    return VFS_OK;
}
```

#### 6.2.3 DTSI 直投示例

DTSI 中所有外设参数直接使用厂商 LL 库宏值，框架不做任何翻译：

```dts
/* board/dtsi/stm32f407-spi.dtsi — STM32 SPI 平台能力 */
&spi1 {
    compatible = "spi-platform-cap";
    spi-base = <SPI1_BASE>;
    spi-clk  = <LL_APB2_GRP1_PERIPH_SPI1>;
    mosi-port = <GPIOA_BASE>;
    mosi-pin  = <GPIO_PIN_7>;
    mosi-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA>;
    mosi-af   = <GPIO_AF5_SPI1>;
};
```

```dts
/* board/dtsi/esp32s3-spi.dtsi — ESP32 SPI 平台能力 */
&spi3 {
    compatible = "spi-platform-cap";
    spi-base = <SPI3_HOST>;
    spi-clk  = <0>;            /* ESP32 无 clk_periph 概念 */
    mosi-port = <0>;
    mosi-pin  = <11>;          /* SoC GPIO 编号 */
    mosi-clk  = <0>;
    mosi-af   = <0>;           /* ESP32 无 AF 概念 */
};
```

> 兼容字符串统一无平台前缀：`spi-master`、`spi-slave`、`uart`、`gpio`、`*-platform-cap`（不再有 `stm32,`/`ch32,`/`esp32,` 前缀）。

#### 6.2.4 引脚配置统一结构

SPI/UART 引脚配置使用统一结构体（含 `af` 字段），各平台字段语义：

```c
struct hal_spi_pin_cfg {
    uintptr_t port;       /* STM32/WCH: GPIO 基地址; ESP32: 0 */
    uint16_t  pin;        /* STM32/WCH: GPIO_PIN_x; ESP32: SoC GPIO 编号 */
    uint32_t  clk_periph; /* STM32/WCH: RCC 时钟; ESP32: 0 */
    uint32_t  af;         /* STM32: GPIO_AFx_xxx; WCH: GPIOMode_TypeDef; ESP32: 0 */
};
```

| 平台 | `port` | `pin` | `clk_periph` | `af` |
|------|--------|-------|--------------|------|
| STM32 | `GPIOA_BASE` 等 | `GPIO_PIN_5` | `LL_AHB1_GRP1_PERIPH_GPIOA` | `GPIO_AF5_SPI1` |
| WCH | `GPIOA_BASE` | `GPIO_Pin_5` | `RCC_APB2Periph_GPIOA` | `GPIO_Mode_AF_PP`（mode+af 编码在一起） |
| ESP32 | `0` | SoC GPIO 编号 | `0` | `0` |

> 所有引脚字段不再使用 `hal_pin_t` 虚拟引脚抽象（已删除）。pin 字段使用 plain `int`，未用引脚填 `-1`。

#### 6.2.5 约束清单

- HAL 层只含结构体定义、init 函数、`static inline` 快速路径，**无内存池管理**
- 必须使用 LL 库函数进行所有硬件操作，严禁直接寄存器操作（fast-path 除外，如 GPIO BSRR）
- DTSI 参数直接传给厂商 LL 库宏，**无自定义 enum 或 switch 映射**
- HAL `.c` 内部 cast `uintptr_t` 为厂商类型，外部调用者无感知
- VFS 层和 bus 层必须平台中性：只允许标准 C、框架代码、HAL/bus 接口，无厂商 SDK 宏/include/类型
- 命名规范：使用 `port`/`pin`/`spi`/`uart`，无 `base`/`mask` 后缀

### 6.3 ESP32 (ESP-IDF) 移植

ESP32 作为异构架构（Xtensa LX6/LX7），不参与 ARM/RISC-V 通用基准。移植要点：

- **构建路径**：走原生 Linux / Windows 工具链（`idf.py` + Xtensa GCC，ESP-IDF 官方双端支持），不走 Docker，保证 Linux 与 Windows 双端可编译
- **组件集成**：mini_tree 以 ESP-IDF 组件形式引入，通过 `idf_component_register()` 注册
- **配置流程**：沿用原生 Kconfig 流程（`Kconfig` + `tools/menuconfig.py` + `tools/genconfig.py`），与 ESP-IDF `sdkconfig` 不冲突
- **RTOS 后端**：直接使用 ESP-IDF 自带的 SMP FreeRTOS（双核），不编译本仓库 `lib/freeRTOS/` 中的 ARM/RISC-V 汇编端口
- **裸机后端**（`OSAL_NULL`）无 RTOS 端口限制，可正常使用
- **DMA 适配**：ESP32 无 STM32/WCH 风格 DMA，`hal_uart_write_dma` 返回 `VFS_ERR_NOTSUPP`，`hal_uart_dma_abort` 为空实现

典型目录结构：
```
my_esp_project/
├── CMakeLists.txt
├── components/
│   └── mini_tree/          # 本仓库作为 ESP-IDF 组件
│       ├── CMakeLists.txt  # idf_component_register()
│       ├── Kconfig
│       └── ...
└── main/
    └── main.c
```

OSAL 默认 FreeRTOS（IDF 内置），语言默认 C++（物联网场景），均可通过 Kconfig 切换。

### 6.4 移植模板骨架

新平台移植时建议在用户工程中创建以下 HAL 实现文件（命名仅为示例，可按团队规范调整）：

| 文件 | 说明 |
|------|------|
| `hal_cpu.c` | CPU 紧急停止、Cache 操作 |
| `hal_flash.c` | Flash 读/写/擦除 |
| `hal_wdt.c` | 硬件看门狗喂狗与超时设置 |
| `hal_storage.c` | NVS 存储接口 |
| `hal_platform_safety.c` | 平台级安全停机 |
| `hal_force_stop.c` | 强制外设停止 |

> 中间件本身不提供 HAL 实现文件，所有 `hal_*.c` 由用户工程按目标芯片 SDK 编写。HAL 实现按平台目录组织（如 `hal/gpio/hal_gpio_<chip>.c`、`hal/spi/hal_spi_<chip>.c`），不再使用 `HAL_SRCS` 变量统一收集。

### 6.5 AMP 双核移植

当目标 MCU 为双核且启用 `CONFIG_CPU_CORES=2` 时，需实现以下三个 HAL 函数：

| 函数 | 必须/可选 | 说明 |
|------|----------|------|
| `hal_cpu_secondary_startup()` | **必须** | 释放副核 (Core 1) 复位，让其从 `hal_cpu_baremetal_entry` 开始执行 |
| `hal_cpu_baremetal_entry()` | 可选 | 副核裸机入口，默认死循环；板级覆盖实现 Core 1 主循环 |
| `hal_cpu_get_id()` | 可选 | 返回当前核心 ID（0/1），默认返回 0 |

**各架构典型实现参考：**

- **ARM Cortex-M7 + M4 双核（如 STM32H7 系列）：**
  ```c
  void hal_cpu_secondary_startup(void) {
      RCC->GCR |= RCC_GCR_BOOT_C2;  // 释放 CM4 核复位
  }
  int hal_cpu_get_id(void) {
      return (SCB->CPUID & 0xF0000000) ? 1 : 0;
  }
  ```

- **ARM Cortex-M 同构双核（如 GD32 系列）：**
  ```c
  void hal_cpu_secondary_startup(void) {
      SYS_CFG->CTL |= SYS_CFG_CTL_CPU1_BOOT;
  }
  ```

- **RISC-V 双核：**
  ```c
  void hal_cpu_secondary_startup(void) {
      // 发核间中断或操作复位控制寄存器
  }
  int hal_cpu_get_id(void) {
      uint32_t hartid;
      __asm__ volatile("csrr %0, mhartid" : "=r"(hartid));
      return (int)hartid;
  }
  ```

启动时机：框架在 `mini_tree_start_tasks()` (Phase 2) 末尾、`vTaskStartScheduler()` 之前自动调用 `hal_cpu_secondary_startup()`。
