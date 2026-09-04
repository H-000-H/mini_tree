/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_define_i2c.h
 *@brief board define i2c 头文件
 *@author H-000-H
 *@details
 *   I2C VFS 板级配置宏 (vfs/i2c) — 中间件默认值 + 板级覆盖入口
 *   覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */

#ifndef BOARD_DEFINE_I2C_H
#define BOARD_DEFINE_I2C_H

/* host 池 = DTS i2c-master + i2c-slave 节点数之和 (缺省各 1) */
#ifndef DTC_GEN_COUNT_I2C_MASTER
#define DTC_GEN_COUNT_I2C_MASTER 1
#endif
#ifndef DTC_GEN_COUNT_I2C_SLAVE
#define DTC_GEN_COUNT_I2C_SLAVE 1
#endif
#ifndef I2C_VFS_PRIV_COUNT
#define I2C_VFS_PRIV_COUNT (DTC_GEN_COUNT_I2C_MASTER + DTC_GEN_COUNT_I2C_SLAVE)
#endif

/* client 池 = 两个 client compatible 节点数之和 (缺省各 1) */
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_I2C_MASTER_CLIENT
#define DTC_GEN_COUNT_HETEROGENEOUS_I2C_MASTER_CLIENT 1
#endif
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_I2C_SLAVE_CLIENT
#define DTC_GEN_COUNT_HETEROGENEOUS_I2C_SLAVE_CLIENT 1
#endif
#ifndef I2C_VFS_CLIENT_COUNT
#define I2C_VFS_CLIENT_COUNT (DTC_GEN_COUNT_HETEROGENEOUS_I2C_MASTER_CLIENT + DTC_GEN_COUNT_HETEROGENEOUS_I2C_SLAVE_CLIENT)
#endif

#endif /* BOARD_DEFINE_I2C_H */
