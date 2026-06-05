/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal dt-bindings for rk3288.dtsi compatibility with dtc-lite.
 * Values match Linux kernel v6.x include/dt-bindings/interrupt-controller/arm-gic.h
 *
 * NOTE: dtc-lite 不支持函数式宏 (如 GIC_CPU_MASK_SIMPLE(nr)),
 *       不影响 MCU 实际外设中断解析.
 */
#ifndef _DT_BINDINGS_ARM_GIC_H
#define _DT_BINDINGS_ARM_GIC_H

#define GIC_SPI 0
#define GIC_PPI 1

#endif
