/*
 * dt-bindings/gpio.h — GPIO 常量宏 (Linux DTS 兼容)
 *
 * 用法: 在 .dtsi / .dts 中 #include <dt-bindings/gpio.h>
 *       然后在 gpios 属性中引用: gpios = <&gpio0 1 GPIO_ACTIVE_HIGH>;
 */

#ifndef _DT_BINDINGS_GPIO_H
#define _DT_BINDINGS_GPIO_H

#define GPIO_ACTIVE_HIGH    0
#define GPIO_ACTIVE_LOW     1

#define GPIO_PUSH_PULL      0
#define GPIO_OPEN_DRAIN     1

#endif /* _DT_BINDINGS_GPIO_H */
