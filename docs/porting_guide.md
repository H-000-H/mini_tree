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

### 6.3 ESP32 (ESP-IDF) 移植

示例工程：[https://github.com/H-000-H/mini-tree-example](https://github.com/H-000-H/mini-tree-example)

mini_tree 以 ESP-IDF 组件形式引入，配置沿用原生 Kconfig 流程（`Kconfig` + `tools/menuconfig.py` + `tools/genconfig.py`），通过 `idf_component_register()` 注册组件。详见示例工程中的 `components/mini_tree/Kconfig`、`.config` 和 `CMakeLists.txt`。

### 6.4 移植模板

参考 `examples/porting_template/` 中的文件骨架：

| 文件 | 说明 |
|------|------|
| `hal_cpu.c` | CPU 紧急停止、Cache 操作 |
| `hal_flash.c` | Flash 读/写/擦除 |
| `hal_wdt.c` | 硬件看门狗喂狗与超时设置 |
| `hal_storage.c` | NVS 存储接口 |
| `hal_platform_safety.c` | 平台级安全停机 |
| `hal_force_stop.c` | 强制外设停止 |

### 6.5 AMP 双核移植

当目标 MCU 为双核且启用 `CONFIG_CPU_CORES=2` 时，需实现以下三个 HAL 函数：

| 函数 | 必须/可选 | 说明 |
|------|----------|------|
| `hal_cpu_secondary_startup()` | **必须** | 释放副核 (Core 1) 复位，让其从 `hal_cpu_baremetal_entry` 开始执行 |
| `hal_cpu_baremetal_entry()` | 可选 | 副核裸机入口，默认死循环；板级覆盖实现 Core 1 主循环 |
| `hal_cpu_get_id()` | 可选 | 返回当前核心 ID（0/1），默认返回 0 |

**各平台典型实现参考：**

- **STM32H7 (Cortex-M7 + M4)：**
  ```c
  void hal_cpu_secondary_startup(void) {
      RCC->GCR |= RCC_GCR_BOOT_C2;  // 释放 CM4 核复位
  }
  int hal_cpu_get_id(void) {
      return (SCB->CPUID & 0xF0000000) ? 1 : 0;
  }
  ```

- **GD32 双核 Cortex-M：**
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

启动时机：框架在 `System_Start_Tasks()` (Phase 2) 末尾、`vTaskStartScheduler()` 之前自动调用 `hal_cpu_secondary_startup()`。
