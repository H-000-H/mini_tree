/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_can.h
 *@brief board define can 头文件
 *@author H-000-H
 *@details
 *   CAN VFS 板级配置宏 (vfs/can) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_CAN_H
#define BOARD_DEFINE_CAN_H

/* host 池 = DTS "can-host" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_CAN_HOST
#define DTC_GEN_COUNT_CAN_HOST 1
#endif
#ifndef CAN_VFS_PRIV_COUNT
#define CAN_VFS_PRIV_COUNT DTC_GEN_COUNT_CAN_HOST
#endif

/* client 池 = DTS "heterogeneous,can-client" 节点数 (缺省 1) */
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_CAN_CLIENT
#define DTC_GEN_COUNT_HETEROGENEOUS_CAN_CLIENT 1
#endif
#ifndef CAN_VFS_CLIENT_COUNT
#define CAN_VFS_CLIENT_COUNT DTC_GEN_COUNT_HETEROGENEOUS_CAN_CLIENT
#endif

#endif /* BOARD_DEFINE_CAN_H */
