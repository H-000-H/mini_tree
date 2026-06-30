## 11. 红线区 — 硬实时 Fast Path

### 11.1 红线/蓝线架构原则

代码量 95% 的**蓝线区**强制使用 VFS/OSAL，牺牲性能换取可移植性。代码量 5% 的**红线区**（电机 FOC、音频 DSP、高频协议、WS2812/1-Wire 等位时序协议）允许直接操作硬件，为纳秒级实时性牺牲可移植性。

mini_tree 的 fast-path 不再使用独立的 `*_fast.h` 头文件，而是采用以下两种模式：

| 模式 | 实现位置 | 调用方式 | 适用场景 |
|------|----------|----------|----------|
| **HAL .c extern 函数** | `hal/<periph>/hal_<periph>_<chip>.c` | VFS 层 inline wrapper 调用 | GPIO set/get/toggle 等高频路径 |
| **HAL 头 inline 函数** | `hal/<periph>/hal_<periph>.h` | 直接 inline 调用 | CPU 中断控制、ISR 检测、延时 |

设计要点：
- HAL 头文件保持厂商中性（仅含 `uintptr_t`/`int`/`void*`），不暴露任何 vendor 类型
- 厂商头文件由 HAL `.c` 内部 `#include`，外部调用者无感知
- fast-path 函数零分支、零查表，直接解引用对象指针刷寄存器或调用厂商 LL 库
- HAL 结构体使用 `uintptr_t`/`int`/`void*` 作为硬件指针，DTSI 参数直接传给厂商 LL 库宏

### 11.2 GPIO Fast Path

GPIO fast-path 函数在 `hal/gpio/hal_gpio.h` 中声明，由各平台的 `hal_gpio_stm32.c` / `hal_gpio_ch32.c` / `hal_gpio_esp32.c` 实现：

```c
#include "hal_gpio.h"

/* 声明在 hal_gpio.h, 实现在 hal_gpio_<chip>.c, 零分支零查表 */
int hal_gpio_fast_set_level(hal_gpio_obj_t* obj, int level);
int hal_gpio_fast_get_level(hal_gpio_obj_t* obj, int* level_out);
int hal_gpio_fast_toggle(hal_gpio_obj_t* obj);
```

`hal_gpio_obj_t` 直接嵌入 VFS priv 结构（非指针），由 VFS probe 填值，HAL 无池管理。各平台实现差异：

| 平台 | 实现方式 | 字段语义 |
|------|---------|---------|
| STM32/WCH | 解引用 `obj->port` 为 `GPIO_TypeDef*`，直接写 `BSRR`/`ODR` 寄存器 | `port` = GPIO 基地址, `pin` = `GPIO_PIN_x` |
| ESP32 | 调用 `gpio_set_level()` / `gpio_get_level()` 等 ESP-IDF API | `port` = 0, `pin` = SoC GPIO 编号 |

VFS 层在 `vfs/gpio/vfs-gpio.h` 中提供 inline wrapper，转发到 HAL fast-path 函数：

```c
/* vfs-gpio.h — VFS inline wrapper, 调用 HAL fast-path */
static inline int vfs_gpio_set_level(struct vfs_gpio_arg* vfs_arg)
{
    if (IS_ERR(vfs_arg))      return PTR_ERR(vfs_arg);
    if (!vfs_arg->obj)        return VFS_ERR_INVAL;
    return hal_gpio_fast_set_level(vfs_arg->obj, vfs_arg->level);
}

static inline int vfs_gpio_toggle(struct vfs_gpio_arg* vfs_arg) { /* 同上 */ }
static inline int vfs_gpio_get_level(struct vfs_gpio_arg* vfs_arg) { /* 同上 */ }
```

> 适用于高于 10 kHz 的 GPIO 翻转频率。低频操作请走标准 VFS `device_ioctl(dev, GPIO_CMD_SET_LEVEL, ...)` 路径。
> 分层隔离由 `#pragma GCC poison` 强制：非 VFS 实现文件不得直接调用 `hal_gpio_init`/`hal_gpio_deinit`，但保留 `hal_gpio_fast_*` 供 inline wrapper 内部使用。

### 11.3 CPU / 中断控制 Fast Path

CPU 中断控制和 ISR 检测函数全部在 `hal/cpu/hal_cpu.h` 中以 `static inline` 实现，零开销直接操作 NVIC/PRIMASK 寄存器：

```c
#include "hal_cpu.h"

/* NVIC 中断控制 (inline, 直接刷寄存器) */
hal_irq_enable(29);             // 使能中断
hal_irq_disable(29);            // 禁能
hal_irq_set_priority(29, 5);    // 设置优先级
int prio = hal_irq_get_priority(29);

/* 全局中断开关 (ARM: PRIMASK, RISC-V: 占位实现) */
uint32_t mask = hal_irq_disable_all();
// ... 原子操作 ...
hal_irq_restore(mask);

/* ISR 上下文检测 (ARM: IPSR, RISC-V: mcause) */
if (hal_is_in_isr()) {
    osal_queue_send(queue, &evt, 0);
}
```

| 平台 | ISR 检测机制 | 全局中断机制 |
|------|-------------|-------------|
| ARM Cortex-M3/4/7 | `mrs ipsr` | `mrs/msr primask` + `cpsid i` |
| RISC-V RV32 | `csrr mcause` | 占位实现（返回未定义值，需 SoC 移植层覆盖） |

> AMP 紧急停止与副核启动 API（`hal_cpu_emergency_stop_all_cores`、`hal_cpu_secondary_startup`、`hal_cpu_baremetal_entry`、`hal_cpu_get_id`）由 `hal/cpu/hal_cpu_amp.c` 提供弱符号实现，SoC 移植层可覆盖。

### 11.4 PWM 寄存器直写

`hal/pwm/hal_pwm.h` 提供定时器寄存器直写 inline stub，供硬实时环（如电机 FOC）使用：

```c
#include "hal_pwm.h"

/* 头文件提供默认 stub, SoC 移植层可覆盖为真实寄存器直写 */
static inline void hal_pwm_set_duty_reg(uint32_t tim_base, int channel, uint32_t duty)
{
    /* 默认空实现, 平台 soc 层可重写 */
}

static inline void hal_pwm_set_period_reg(uint32_t tim_base, uint32_t period)
{
    /* 默认空实现, 平台 soc 层可重写 */
}
```

SoC 移植层如需启用占空比直写，可在板级头文件中提供同名 `static inline` 覆盖：

```c
/* 板级 hal_pwm_fast_override.h (示例) */
static inline void hal_pwm_set_duty_reg(uint32_t tim_base, int channel, uint32_t duty)
{
    *(volatile uint32_t*)(tim_base + 0x34 + ((uint32_t)channel << 2)) = duty;
}
```

> PWM 通道级 API（`init`/`set_duty`/`get_duty`/`deinit`）通过 `hal_pwm_channel` 结构体的函数指针提供，由 SoC 移植层实例化。

### 11.5 硬实时微秒延时

`hal/cpu/hal_cpu_delay.h` 提供基于硬件周期计数器的微秒级阻塞延时，不受 OS tick 与调度影响：

```c
#define HAL_CPU_FREQ_HZ  240000000UL
#include "hal_cpu_delay.h"

void init(void) {
    hal_delay_init();                     // 启动周期计数器
}

void pulse_us(void) {
    hal_gpio_fast_set_level(obj, 1);
    hal_delay_us(10);                     // 精确 10μs
    hal_gpio_fast_set_level(obj, 0);
}
```

| API | 用途 |
|-----|------|
| `hal_delay_init()` | 启动周期计数器（必须先调用） |
| `hal_delay_us(n)` | 阻塞 n 微秒 |
| `hal_delay_ms(n)` | 阻塞 n 毫秒 |
| `hal_delay_cycles(n)` | 阻塞 n 个 CPU 周期 |

| 平台 | 底层机制 | 精度 |
|------|---------|------|
| STM32 (Cortex-M3/4/7) | CMSIS DWT_CYCCNT | 1 cycle |
| CH32 (RISC-V) | WCH `Delay_Init` / `Delay_Us` / `Delay_Ms` (SysTick) | 1 cycle |
| ESP32 (Xtensa) | esp_rom_delay_us / ets_delay_us | 1 cycle |

> Phase 1（OS 启动前）和 ISR 中只能使用 `hal_delay_*`，不能使用 `osal_task_delay`。`hal_delay_ms` 依赖 `hal_delay_init()` 已开启周期计数器，否则会卡死。

### 11.6 RAM_EXEC — 零抖动代码驻留

将高频函数搬运到 ITCM/DTCM/SRAM，避免 Flash 等待状态导致的抖动：

```c
#include "compiler_compat.h"

RAM_EXEC void hall_sensor_isr(void)
{
    /* 在 TCM 中执行, 零等待状态 */
    hal_gpio_fast_set_level(obj, 1);
}
```

需要根据芯片修改 linker script 添加 `.ram_code` 段。
