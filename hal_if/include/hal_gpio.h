#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 跨平台引脚抽象 ──
 *
 * hal_pin_t 为 32-bit 复合引脚标识:
 *   [31:16] 端口号 (Port), 由平台实现定义
 *   [15: 0] 引脚号 (Pin),  0 开始
 *
 * 使用方式:
 *   hal_pin_t pin = HAL_MAKE_PIN(port, pin_num);
 */
typedef uint32_t hal_pin_t;

#define HAL_PIN_PORT_SHIFT 16
#define HAL_PIN_NUM_MASK   0xFFFFU
#define HAL_MAKE_PIN(port, num)  (((hal_pin_t)(port) << HAL_PIN_PORT_SHIFT) | ((hal_pin_t)(num) & HAL_PIN_NUM_MASK))
#define HAL_PIN_PORT(pin)        ((int)((pin) >> HAL_PIN_PORT_SHIFT))
#define HAL_PIN_NUM(pin)         ((int)((pin) & HAL_PIN_NUM_MASK))

/* GPIO 方向模式 */
typedef enum
{
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT,
    HAL_GPIO_MODE_INPUT_OUTPUT,
} hal_gpio_mode_t;

/* 上下拉模式 */
typedef enum
{
    HAL_GPIO_PULL_NONE,         /* 无上下拉 */
    HAL_GPIO_PULL_UP,           /* 上拉 */
    HAL_GPIO_PULL_DOWN,         /* 下拉 */
} hal_gpio_pull_t;

/* 中断触发模式 */
typedef enum
{
    HAL_GPIO_INTR_DISABLE = 0,
    HAL_GPIO_INTR_RISING,
    HAL_GPIO_INTR_FALLING,
    HAL_GPIO_INTR_ANY_EDGE,
} hal_gpio_intr_t;

/* GPIO 配置 */
typedef struct
{
    hal_pin_t pin;
    hal_gpio_mode_t mode;
    hal_gpio_pull_t pull;
    hal_gpio_intr_t intr_type;
} hal_gpio_config_t;

typedef void (*hal_gpio_isr_t)(void* arg);

/* ioctl 命令 */
#define GPIO_CMD_CONFIG       0x10
#define GPIO_CMD_TOGGLE       0x11
#define GPIO_CMD_INSTALL_ISR  0x12
#define GPIO_CMD_ADD_ISR      0x13
#define GPIO_CMD_REMOVE_ISR   0x14
#define GPIO_CMD_SET_LEVEL    0x15
#define GPIO_CMD_GET_LEVEL    0x16

typedef struct
{
    hal_pin_t pin;
    hal_gpio_isr_t handler;
    void* arg;
} gpio_isr_arg_t;

typedef struct
{
    hal_pin_t pin;
    int level;
} gpio_level_arg_t;

/* 快速路径函数(直呼 HAL 实现, 绕过 ioctl) */
int hal_gpio_set_level(hal_pin_t pin, int level);
int hal_gpio_get_level(hal_pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
