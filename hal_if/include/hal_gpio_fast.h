#ifndef HAL_GPIO_FAST_H
#define HAL_GPIO_FAST_H

#include "hal_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── GPIO 快速内联路径 ──
 *
 * 在不支持 VFS 的环境下直接调用 HAL 底层函数, 避免 ioctl 调用开销。
 * 平台实现可替换为寄存器直写宏以获得极致性能。
 */
static inline void hal_gpio_set_level_fast(hal_pin_t pin, int level)
{
    hal_gpio_set_level(pin, level);
}

static inline int hal_gpio_get_level_fast(hal_pin_t pin)
{
    return hal_gpio_get_level(pin);
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_FAST_H */
