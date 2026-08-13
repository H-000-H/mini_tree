/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SPI VFS 板级配置宏 (vfs/spi) — 中间件默认值 + 板级覆盖入口
 * 覆盖方式: 改本文件 或 编译 -D<NAME>=<N>; 未覆盖走默认。
 */
#ifndef BOARD_DEFINE_SPI_H
#define BOARD_DEFINE_SPI_H

/* host 池 = DTS spi-master + spi-slave 节点数之和 (缺省各 1) */
#ifndef DTC_GEN_COUNT_SPI_MASTER
#define DTC_GEN_COUNT_SPI_MASTER 1
#endif
#ifndef DTC_GEN_COUNT_SPI_SLAVE
#define DTC_GEN_COUNT_SPI_SLAVE 1
#endif
#ifndef SPI_VFS_PRIV_COUNT
#define SPI_VFS_PRIV_COUNT (DTC_GEN_COUNT_SPI_MASTER + DTC_GEN_COUNT_SPI_SLAVE)
#endif

/* client 池 = 两个 client compatible 节点数之和 (缺省各 1) */
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_SPI_MASTER_CLIENT
#define DTC_GEN_COUNT_HETEROGENEOUS_SPI_MASTER_CLIENT 1
#endif
#ifndef DTC_GEN_COUNT_HETEROGENEOUS_SPI_SLAVE_CLIENT
#define DTC_GEN_COUNT_HETEROGENEOUS_SPI_SLAVE_CLIENT 1
#endif
#ifndef SPI_VFS_CLIENT_COUNT
#define SPI_VFS_CLIENT_COUNT \
    (DTC_GEN_COUNT_HETEROGENEOUS_SPI_MASTER_CLIENT + DTC_GEN_COUNT_HETEROGENEOUS_SPI_SLAVE_CLIENT)
#endif

#endif /* BOARD_DEFINE_SPI_H */
