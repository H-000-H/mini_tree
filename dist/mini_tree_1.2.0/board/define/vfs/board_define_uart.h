/* SPDX-License-Identifier: Apache-2.0 */
/*
 * UART VFS 板级配置宏 (vfs/uart) — 中间件默认值 + 板级覆盖入口
 * 覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */
#ifndef BOARD_DEFINE_UART_H
#define BOARD_DEFINE_UART_H

/* host 池 = DTS "uart" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_UART
#define DTC_GEN_COUNT_UART 1
#endif
#ifndef UART_VFS_PRIV_COUNT
#define UART_VFS_PRIV_COUNT DTC_GEN_COUNT_UART
#endif

/* client 池 = DTS "uart-client" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_UART_CLIENT
#define DTC_GEN_COUNT_UART_CLIENT 1
#endif
#ifndef UART_VFS_COUNT
#define UART_VFS_COUNT DTC_GEN_COUNT_UART_CLIENT
#endif

#endif /* BOARD_DEFINE_UART_H */
