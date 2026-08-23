/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_i2s.h
 *@brief board define i2s 头文件
 *@author H-000-H
 *@details
 *   I2S VFS 板级配置宏 (vfs/i2s) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_I2S_H
#define BOARD_DEFINE_I2S_H

/* host 池 = DTS i2s-master + i2s-slave 节点数之和 (缺省各 1) */
#ifndef DTC_GEN_COUNT_I2S_MASTER
#define DTC_GEN_COUNT_I2S_MASTER 1
#endif
#ifndef DTC_GEN_COUNT_I2S_SLAVE
#define DTC_GEN_COUNT_I2S_SLAVE 1
#endif
#ifndef I2S_HOST_POOL
#define I2S_HOST_POOL (DTC_GEN_COUNT_I2S_MASTER + DTC_GEN_COUNT_I2S_SLAVE)
#endif

/* client 池 = 两个 client compatible 节点数之和 (缺省各 1) */
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_I2S_MASTER_CLIENT
#define DTC_GEN_COUNT_HETEROGENEOUS_I2S_MASTER_CLIENT 1
#endif
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_I2S_SLAVE_CLIENT
#define DTC_GEN_COUNT_HETEROGENEOUS_I2S_SLAVE_CLIENT 1
#endif
#ifndef I2S_CLIENT_POOL
#define I2S_CLIENT_POOL                                                                            \
    (DTC_GEN_COUNT_HETEROGENEOUS_I2S_MASTER_CLIENT + DTC_GEN_COUNT_HETEROGENEOUS_I2S_SLAVE_CLIENT)
#endif

/* 环形 FIFO 深度 (必须是 2 的幂) */
#ifndef I2S_CIRC_FIFO_SIZE
#define I2S_CIRC_FIFO_SIZE 512U
#endif

#endif /* BOARD_DEFINE_I2S_H */
