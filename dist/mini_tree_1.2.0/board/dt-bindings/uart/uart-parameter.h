/* SPDX-License-Identifier: Apache-2.0 */
/* UART 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 板级 UART 配置在 board *.dts 中覆盖.
 */
#ifndef __UART_PARAMETER_H__
#define __UART_PARAMETER_H__
#define DTS_UART_BAUD 115200
#endif