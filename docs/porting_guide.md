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
    ├── Timer ──────────────────  hal/system/hal_timer.h
    ├── RTC   ──────────────────  hal/system/hal_rtc.h
    ├── DMA   ──────────────────  hal/system/hal_dma.h
    ├── SDIO  ──────────────────  hal/system/hal_sdio.h
    ├── CPU   ──────────────────  hal/cpu/hal_cpu.h
    ├── Flash ──────────────────  hal/storage/hal_flash.h
    ├── Storage ────────────────  hal/storage/hal_storage.h
    └── Platform Safety ───────  hal/system/hal_platform_safety.h
```

### 6.2 实现 HAL 插座 (Subsystem Ops 模式)

每个 `hal/` 子目录中的接口定义一组操作表（纯虚结构体），驱动通过 ops 指针调用，不直接引用芯片 SDK 符号：

```c
// 1. 芯片 SDK 仅在 .c 内部包含
#include "hal_gpio.h"
#include "chip_sdk.h"

// 2. 实现无状态契约函数
static int mychip_gpio_set_level(struct device* dev, int level) {
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

### 6.3 ESP32 (ESP-IDF) 移植

ESP32 作为异构架构（Xtensa LX6/LX7），不参与 ARM/RISC-V 通用基准。移植要点：

- **构建路径**：走原生 Linux / Windows 工具链（`idf.py` + Xtensa GCC，ESP-IDF 官方双端支持），不走 Docker，保证 Linux 与 Windows 双端可编译
- **组件集成**：mini_tree 以 ESP-IDF 组件形式引入，通过 `idf_component_register()` 注册
- **配置流程**：沿用原生 Kconfig 流程（`Kconfig` + `tools/menuconfig.py` + `tools/genconfig.py`），与 ESP-IDF `sdkconfig` 不冲突
- **RTOS 后端**：直接使用 ESP-IDF 自带的 SMP FreeRTOS（双核），不编译本仓库 `lib/freeRTOS/` 中的 ARM/RISC-V 汇编端口
- **裸机后端**（`OSAL_NULL`）无 RTOS 端口限制，可正常使用

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

> 中间件本身不提供 HAL 实现文件，所有 `hal_*.c` 由用户工程按目标芯片 SDK 编写。

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
