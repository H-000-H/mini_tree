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
