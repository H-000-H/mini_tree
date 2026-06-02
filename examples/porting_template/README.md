# mini_tree 移植模板

本目录包含 mini_tree 框架所需的 HAL（硬件抽象层）函数桩实现。
将框架移植到新 MCU 平台时，以此作为起点。

## 移植步骤

1. **复制** 本目录到你的项目中（如 `my_project/hal_port/`）

2. **实现** 每个 `.c` 文件，调用你的 MCU SDK：
   - `hal_cpu.c` — 周期计数器、忙等待
   - `hal_flash.c` — Flash 读取（用于 bit-rot scrubber）
   - `hal_force_stop.c` — 紧急外设停机
   - `hal_storage.c` — 持久化配置存储（A/B 槽）
   - `hal_wdt.c` — RTC 看门狗 + 任务看门狗
   - `hal_platform_safety.c` — 故障 LED/蜂鸣器硬件锁定

3. **重命名** `CMakeLists.txt` 中的库名，从 `your_hal_port` 改为实际名称

4. **更新项目 CMakeLists.txt**：

   ```cmake
   add_subdirectory(path/to/mini_tree)
   add_subdirectory(path/to/hal_port)    # 你的 HAL 实现
   target_link_libraries(my_app PRIVATE mini_tree hal_port)
   ```

5. **提供 FreeRTOSConfig.h**，放在项目的 include 路径中

6. **编写 main.c**：

   ```c
   #include "system_init.hpp"
   
   int main(void) {
       platform_init();                // MCU HAL 初始化
       MiniTree::System_Pre_OS_Init(); // 框架阶段 1
       register_my_drivers();          // 注册 hal_* 到 VFS
       MiniTree::System_Start_Tasks(); // 框架阶段 2
       my_app_init();                  // 应用任务
       vTaskStartScheduler();
   }
   ```

## 设备树 (DTS) 适配说明

本框架通过设备树实现 **配置与实现解耦**，移植时只需在板级 `board.dts` 中声明外设的引脚/时钟属性，
驱动代码无需修改。

### 继承覆盖模式

```
基节点: 默认值硬编码在 C 中 (如 DTS_DEF_UART_TX_PIN)
覆盖:   device_find_by_label("uart0") → 读取 DTS 节点属性
        属性不存在 → 保持默认值
```

### 实例参考

| 文件 | 说明 |
|------|------|
| `hal_init_stm32.c` | STM32 四种开发风格 (HAL/LL/SPL/寄存器) 统一接入 DTS |
| `hal_init_gd32.c` | GD32 标准外设库风格接入 DTS |

两个实例展示了同一场景 —— **SPI 四线整体换引脚**：

```dts
spi0: spi@0 {
    mosi  = <3>;         /* 改这里, 无需动 .c */
    miso  = <4>;         /* 改这里, 无需动 .c */
    sclk  = <5>;         /* 改这里, 无需动 .c */
    cs-gpios = <6>;      /* 改这里, 无需动 .c */
};
```

关键结论：
- **修改引脚只需改 `board.dts` 中的属性值，无需动 `.c` / `.h` 函数**
- GD32 与 STM32 共用同一套 DTS 适配模式，无缝切换
- 不同开发风格 (HAL/LL/SPL/寄存器/GD32 库) 对同一 DTS 属性的读取方式一致

### DTS 宏展开 (dtc-lite #include)

`dtc-lite.py` 支持 `#include` 头文件和 `#define` 宏替换，可配合 `dt-bindings/` 头文件使用常量宏：

```dts
/dts-v1/;

#include "dt-bindings/gpio.h"

/ {
    gpio-led {
        compatible = "my,gpio-led";
        pin = <GPIO_ACTIVE_HIGH>;      /* 宏展开为 0 */
    };

    gpio-btn {
        compatible = "my,gpio-btn";
        pin = <GPIO_INPUT_PULLUP>;     /* 宏展开为 2 */
    };
};
```

```c
/* dt-bindings/gpio.h — 在项目中创建此文件 */
#ifndef DT_BINDINGS_GPIO_H
#define DT_BINDINGS_GPIO_H

#define GPIO_ACTIVE_HIGH   0
#define GPIO_ACTIVE_LOW    1
#define GPIO_INPUT         0
#define GPIO_OUTPUT        1
#define GPIO_INPUT_PULLUP  2

#endif
```

dtc-lite 在编译期完成展开，最终生成的数据表中存储的是展开后的数值，**零运行时开销**。

也可直接在 DTS 中定义宏：

```dts
/dts-v1/;

#define MY_IRQ_PRIO  5
#define MY_TIM_FREQ  1000000

/ {
    timer@0 {
        compatible = "my,timer";
        interrupts = <MY_IRQ_PRIO>;
        clock-freq = <MY_TIM_FREQ>;
    };
};
```

---

## 红线区 — 硬实时 Fast Path 示例 (STM32)

框架提供一组绕过 VFS 的 Fast Path 接口，适用于电机 FOC、音频 DSP、软件模拟协议等硬实时场景。以下示例基于 STM32。

### GPIO 寄存器直写

```c
#include "hal_gpio_fast.h"

/* STM32 GPIOA 基址 */
#define GPIOA_BASE  0x40020000U
#define PIN_LED     5

void blink_fast(void)
{
    hal_delay_init();                         // 初始化 DWT 周期计数器

    hal_gpio_fast_set(GPIOA_BASE, 1U << PIN_LED);
    hal_delay_us(100);
    hal_gpio_fast_clr(GPIOA_BASE, 1U << PIN_LED);
    hal_delay_us(100);
}
```

编译器内联后为单条 STR 指令写 BSRR 寄存器。

### NVIC + ISR 上下文检测

```c
#include "hal_cpu_fast.h"

void enable_my_irq(void)
{
    hal_irq_set_priority(29, 5);    // TIM3 中断, 优先级 5
    hal_irq_enable(29);             // 使能

    /* 关全局中断做临界段保护 */
    uint32_t mask = hal_irq_disable_all();
    /* ... 原子操作 ... */
    hal_irq_restore(mask);
}

void my_isr(void)
{
    if (hal_is_in_isr()) {
        /* 当前在中断上下文 — 只调 ISR 安全函数 */
    }

    /* WRONG: 调 device_write() 会触 HAL_ASSERT_NOT_ISR() (DEBUG 下) */
}
```

### 微秒级硬实时延时

```c
#define HAL_CPU_FREQ_HZ  168000000UL    /* STM32F4 主频 */
#include "hal_cpu_delay.h"

void ws2812_send_bit(int bit)
{
    if (bit) {
        hal_gpio_fast_set(GPIOA_BASE, 1U << PIN);
        hal_delay_us(1);                // 精确 1μs, 不受 OS tick 影响
        hal_gpio_fast_clr(GPIOA_BASE, 1U << PIN);
        hal_delay_us(0);
    } else {
        hal_gpio_fast_set(GPIOA_BASE, 1U << PIN);
        hal_delay_us(0);
        hal_gpio_fast_clr(GPIOA_BASE, 1U << PIN);
        hal_delay_us(1);
    }
}
```

DWT_CYCCNT 精度 1 周期 (≈6ns @ 168MHz)，不受任务调度打断。

### PWM 占空比直写 (SoC 层实现)

```c
#include "hal_pwm_fast.h"

/* STM32 TIM2 CCR1 偏移 0x34 */
#define STM32_TIM2_BASE    0x40000000UL
#define STM32_CCR1_OFFSET  0x34UL

/* 实现声明(在 soc_port 层填充) — 替换 hal_pwm_fast.h 的空实现 */
#undef hal_pwm_fast_set_duty
static inline void hal_pwm_fast_set_duty(uint32_t tim_base, int ch, uint32_t duty)
{
    *(volatile uint32_t*)(tim_base + STM32_CCR1_OFFSET + ((uint32_t)ch << 2)) = duty;
}

void motor_foc_loop(void)
{
    hal_pwm_fast_set_duty(STM32_TIM2_BASE, 1, 500);  // 20kHz 控制环
}
```

> `hal_pwm_fast.h` 仅声明 API 签名，具体寄存器偏移由 SoC 层填写。

---

### 移植步骤

1. 在 `board/<platform>/board.dts` 中定义外设节点（参考两个实例顶部的 DTS 注释）
2. 填写实际的引脚号、时钟频率等属性
3. `device_find_by_label()` + `device_get_prop_int()` 在驱动中读取
4. 如需变更引脚，只改 DTS 属性值，不碰 C 代码
5. 硬实时路径启用 Fast Path（`hal_gpio_fast.h` / `hal_cpu_fast.h` / `hal_cpu_delay.h`）
