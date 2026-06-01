#ifndef HAL_GPIO_FAST_H
#define HAL_GPIO_FAST_H

#include "hal_gpio.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void hal_gpio_set_level_fast(hal_pin_t pin, int level)
{
    int num = HAL_PIN_NUM(pin);
    if (num >= 0 && num < GPIO_NUM_MAX)
    {
        gpio_set_level((gpio_num_t)num, level);
    }
}

static inline int hal_gpio_get_level_fast(hal_pin_t pin)
{
    int num = HAL_PIN_NUM(pin);
    if (num >= 0 && num < GPIO_NUM_MAX)
    {
        return gpio_get_level((gpio_num_t)num);
    }
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_FAST_H */