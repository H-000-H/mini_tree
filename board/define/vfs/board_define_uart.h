/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_uart.h
 *@brief board define uart 头文件
 *@author H-000-H
 *@details
 *   UART VFS 板级配置宏 (vfs/uart) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_UART_H
#define BOARD_DEFINE_UART_H

/* host 池 = DTS "mt-uart" 节点数 (缺省 1) */
/* include the dtc-lite generated truth table first: otherwise the default
   value below conflicts with the real one (macro redefinition) */
#if defined(__has_include)
#if __has_include("dt_config_gen.h")
#include "dt_config_gen.h"
#endif
#endif
#ifndef DTC_GEN_COUNT_MT_UART
#define DTC_GEN_COUNT_MT_UART 1
#endif
#ifndef UART_VFS_PRIV_COUNT
#define UART_VFS_PRIV_COUNT DTC_GEN_COUNT_MT_UART
#endif

/* client 池 = DTS "mt-uart-client" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_MT_UART_CLIENT
#define DTC_GEN_COUNT_MT_UART_CLIENT 1
#endif
#ifndef UART_VFS_COUNT
#define UART_VFS_COUNT DTC_GEN_COUNT_MT_UART_CLIENT
#endif

#endif /* BOARD_DEFINE_UART_H */
