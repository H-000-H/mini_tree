/* GPIO 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 数值与 hal_if/gpio/hal_gpio.h 中 hal_gpio_*_t 枚举一致.
 * 板级 port/pin/mode 在 board *.dts &gpio_pin { } 中覆盖.
 * STM32: gpio-port = DTS_GPIOA..I (0..8), gpio-pin = DTS_GPIO_PIN_0..15.
 * dtc-lite 仅做 #define 展开; 勿加 #ifndef guard (会破坏宏展开).
 */

/* 方向模式 (hal_gpio_mode_t) */
#define DTS_GPIO_MODE_INPUT          0
#define DTS_GPIO_MODE_OUTPUT         1
#define DTS_GPIO_MODE_INPUT_OUTPUT   2
#define DTS_GPIO_MODE_OPEN_DRAIN     3

/* 上下拉 (hal_gpio_pull_t) */
#define DTS_GPIO_PULL_NONE           0
#define DTS_GPIO_PULL_UP             1
#define DTS_GPIO_PULL_DOWN           2

/* 中断触发 (hal_gpio_intr_t) */
#define DTS_GPIO_INTR_DISABLE        0
#define DTS_GPIO_INTR_RISING         1
#define DTS_GPIO_INTR_FALLING        2
#define DTS_GPIO_INTR_ANY_EDGE       3

/* 输出默认电平 / 有效电平 */
#define DTS_GPIO_LEVEL_LOW           0
#define DTS_GPIO_LEVEL_HIGH          1
#define DTS_GPIO_ACTIVE_LOW          2
#define DTS_GPIO_ACTIVE_HIGH         3

/* 逻辑端口 (hal_pin_t.v[0]) */
#define DTS_GPIOA                    0
#define DTS_GPIOB                    1
#define DTS_GPIOC                    2
#define DTS_GPIOD                    3
#define DTS_GPIOE                    4
#define DTS_GPIOF                    5
#define DTS_GPIOG                    6
#define DTS_GPIOH                    7
#define DTS_GPIOI                    8

/* 端口内引脚位 (0..15) */
#define DTS_GPIO_PIN_0               0
#define DTS_GPIO_PIN_1               1
#define DTS_GPIO_PIN_2               2
#define DTS_GPIO_PIN_3               3
#define DTS_GPIO_PIN_4               4
#define DTS_GPIO_PIN_5               5
#define DTS_GPIO_PIN_6               6
#define DTS_GPIO_PIN_7               7
#define DTS_GPIO_PIN_8               8
#define DTS_GPIO_PIN_9               9
#define DTS_GPIO_PIN_10              10
#define DTS_GPIO_PIN_11              11
#define DTS_GPIO_PIN_12              12
#define DTS_GPIO_PIN_13              13
#define DTS_GPIO_PIN_14              14
#define DTS_GPIO_PIN_15              15

/* 常用默认组合 */
#define DTS_GPIO_DEFAULT_OUTPUT          DTS_GPIO_MODE_OUTPUT
#define DTS_GPIO_DEFAULT_OUTPUT_PULL     DTS_GPIO_PULL_NONE
#define DTS_GPIO_DEFAULT_OPEN_DRAIN      DTS_GPIO_MODE_OPEN_DRAIN
#define DTS_GPIO_DEFAULT_OPEN_DRAIN_PULL DTS_GPIO_PULL_NONE
#define DTS_GPIO_DEFAULT_INPUT           DTS_GPIO_MODE_INPUT
#define DTS_GPIO_DEFAULT_INPUT_PULL      DTS_GPIO_PULL_NONE
#define DTS_GPIO_DEFAULT_INPUT_PULLUP    DTS_GPIO_PULL_UP
#define DTS_GPIO_DEFAULT_INPUT_PULLDOWN  DTS_GPIO_PULL_DOWN
#define DTS_GPIO_DEFAULT_INTR            DTS_GPIO_INTR_DISABLE
#define DTS_GPIO_DEFAULT_LEVEL           DTS_GPIO_LEVEL_LOW
