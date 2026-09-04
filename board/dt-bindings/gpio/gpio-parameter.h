/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file gpio-parameter.h
 *@brief gpio-parameter 头文件
 *@author H-000-H
 *@details
 *   GPIO 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *   只放 #define 常量, 不写设备节点.
 *   板级引脚在 board *.dts &gpios_pin { } 中覆盖.
 */

#ifndef __GPIO_PARAMETER_H__
#define __GPIO_PARAMETER_H__

/* SoC 默认 GPIO 模板用的占位常量 (板级 &gpios_pin 可覆盖) */
#define DTS_GPIO_DEFAULT_INTR 0
#define DTS_GPIO_DEFAULT_LEVEL 0

#endif /* __GPIO_PARAMETER_H__ */
