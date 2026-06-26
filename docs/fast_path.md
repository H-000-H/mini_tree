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
