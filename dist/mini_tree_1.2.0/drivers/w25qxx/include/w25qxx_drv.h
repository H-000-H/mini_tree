/**
 * SPDX-License-Identifier: Apache-2.0
 * @file w25qxx_drv.h
 * @brief W25Qxx SPI NOR Flash 驱动 ioctl 命令与 JEDEC ID 结构
 *
 * 挂在 SPI 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef W25QXX_DRV_H
#define W25QXX_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define W25QXX_CMD_BASE COMPAT_MAGIC(W25QXX)
/** 读取 JEDEC ID（arg: struct w25qxx_jedec*） */
#define W25QXX_CMD_READ_JEDEC_ID (W25QXX_CMD_BASE + 0x01)
/** 命令总数 */
#define W25QXX_CMD_COUNT 1

    /** @brief JEDEC 厂商 ID（MFR + 型号 + 容量） */
    struct w25qxx_jedec
    {
        uint8_t id[3]; /**< id[0]=厂商, id[1]=型号, id[2]=容量 */
    };
#ifdef __cplusplus
}
#endif
#endif /* W25QXX_DRV_H */
