#ifndef HAL_GPIO_FAST_H
#define HAL_GPIO_FAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── GPIO Fast Path ──
 *
 * 绕过 VFS (device_ioctl → 持锁 → 状态检查 → 函数指针跳转) 的直接路径。
 * "Fast" 的核心含义是 bypass VFS，具体实现因平台而异:
 *
 *   STM32/GD32/AT32 (Cortex-M): 寄存器直写 BSRR/ODR/IDR，单条 STR 指令
 *   ESP32:                      调用 gpio_set_level() 等 ESP-IDF API
 *   NXP MCUXpresso:             调用 GPIO_PinWrite() 等 SDK API
 *
 * 本文件提供 STM32/GD32/AT32 兼容 Cortex-M 的寄存器级默认实现。
 * 其他平台应在 hal_if/soc/<chip_name>/ 中提供同名 static inline
 * 函数或宏, 通过头文件覆盖本默认实现。
 *
 * 用法:
 *   #define GPIOA_BASE 0x40020000U
 *   #define PIN_LED    5
 *
 *   hal_gpio_fast_set(GPIOA_BASE, 1U << PIN_LED);
 *   hal_gpio_fast_clr(GPIOA_BASE, 1U << PIN_LED);
 *   hal_gpio_fast_toggle(GPIOA_BASE, 1U << PIN_LED);
 *   uint32_t val = hal_gpio_fast_read(GPIOA_BASE);
 *
 * 注意:
 *   - 调用者需确保外设时钟已在 board 层使能。
 *   - pin_mask 为位掩码 (1 << pin)，可同时操作多引脚。
 *   - 寄存器版本不校验地址合法性，无效地址导致硬 fault。
 *   - 平台实现若调用 vendor API（如 gpio_set_level），需包含对应头文件。
 */

/* ── 默认实现: STM32/GD32/AT32 Cortex-M 通用 GPIO 寄存器布局 ──
 *
 *   +0x08  IDR  - 输入数据寄存器
 *   +0x0C  ODR  - 输出数据寄存器
 *   +0x10  BSRR - 位设置/复位寄存器 ([15:0] set, [31:16] reset)
 *
 * SoC 层可定义 HAL_GPIO_FAST_OVERRIDE 来完全替换为平台特定实现。
 */
#ifndef HAL_GPIO_FAST_OVERRIDE

#define HAL_GPIO_BSRR_OFFSET   0x10U
#define HAL_GPIO_ODR_OFFSET    0x0CU
#define HAL_GPIO_IDR_OFFSET    0x08U

static inline void hal_gpio_fast_set(uint32_t gpio_base, uint32_t pin_mask)
{
    *(volatile uint32_t*)(gpio_base + HAL_GPIO_BSRR_OFFSET) = pin_mask;
}

static inline void hal_gpio_fast_clr(uint32_t gpio_base, uint32_t pin_mask)
{
    *(volatile uint32_t*)(gpio_base + HAL_GPIO_BSRR_OFFSET) = (pin_mask << 16U);
}

static inline void hal_gpio_fast_toggle(uint32_t gpio_base, uint32_t pin_mask)
{
    *(volatile uint32_t*)(gpio_base + HAL_GPIO_ODR_OFFSET) ^= pin_mask;
}

static inline uint32_t hal_gpio_fast_read(uint32_t gpio_base)
{
    return *(volatile uint32_t*)(gpio_base + HAL_GPIO_IDR_OFFSET);
}

#else
/* HAL_GPIO_FAST_OVERRIDE 已定义 — 由外部提供实现 */
#endif /* HAL_GPIO_FAST_OVERRIDE */

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_FAST_H */
