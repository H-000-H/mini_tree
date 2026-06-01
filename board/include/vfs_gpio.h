#ifndef VFS_GPIO_H
#define VFS_GPIO_H

#include <stdint.h>
#include "hal_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── GPIO 模式/上下拉/中断类型 ──
 * 与 hal_if/include/hal_gpio.h 定义相同类型，二者选一即可。
 * 驱动用 vfs_gpio.h，HAL 实现用 hal_gpio.h。
 */
#ifndef HAL_GPIO_H
typedef enum
{
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT,
    HAL_GPIO_MODE_INPUT_OUTPUT,
} hal_gpio_mode_t;

typedef enum
{
    HAL_GPIO_PULL_DISABLE = 0,
    HAL_GPIO_PULL_ENABLE,
} hal_gpio_pull_t;

typedef enum
{
    HAL_GPIO_INTR_DISABLE = 0,
    HAL_GPIO_INTR_RISING,
    HAL_GPIO_INTR_FALLING,
    HAL_GPIO_INTR_ANY_EDGE,
} hal_gpio_intr_t;

typedef struct
{
    int pin;
    hal_gpio_mode_t mode;
    hal_gpio_pull_t pullup;
    hal_gpio_pull_t pulldown;
    hal_gpio_intr_t intr_type;
} hal_gpio_config_t;

typedef void (*hal_gpio_isr_t)(void* arg);

#define GPIO_CMD_CONFIG       0x10
#define GPIO_CMD_TOGGLE       0x11
#define GPIO_CMD_INSTALL_ISR  0x12
#define GPIO_CMD_ADD_ISR      0x13
#define GPIO_CMD_REMOVE_ISR   0x14
#define GPIO_CMD_SET_LEVEL    0x15
#define GPIO_CMD_GET_LEVEL    0x16

typedef struct
{
    int pin;
    hal_gpio_isr_t handler;
    void* arg;
} gpio_isr_arg_t;

typedef struct
{
    int pin;
    int level;
} gpio_level_arg_t;

#endif /* ndef HAL_GPIO_H — type section */

/* ── 快速路径：直通 HAL 绕过 VFS ioctl 派发，供高频调用场景 ── */
static inline int vfs_gpio_set_level(int pin, int level)
{
    return hal_gpio_set_level(pin, level);
}

static inline int vfs_gpio_get_level(int pin)
{
    return hal_gpio_get_level(pin);
}

#ifdef __cplusplus
}
#endif

#endif /* VFS_GPIO_H */
