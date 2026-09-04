/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file at24c02_drv.h
 *@brief AT24C02 EEPROM 驱动 ioctl 命令与读写参数结构
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef AT24C02_DRV_H
#define AT24C02_DRV_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** 存储容量（字节） */
#define AT24C02_SIZE 256U

/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define AT24C02_CMD_BASE MINI_MAGIC(AT24C02)
/** 读数据（arg: struct at24c02_io_arg*） */
#define AT24C02_CMD_READ (AT24C02_CMD_BASE + 0x01)
/** 写数据（arg: struct at24c02_io_arg*） */
#define AT24C02_CMD_WRITE (AT24C02_CMD_BASE + 0x02)
/** 命令总数 */
#define AT24C02_CMD_COUNT 2

/** @brief 读写参数（offset 起始地址，buf/len 数据） */
struct at24c02_io_arg
{
    uint8_t  offset; /**< 起始地址（0..255） */
    uint8_t* buf;    /**< 数据缓冲 */
    size_t   len;    /**< 数据长度 */
};

#ifdef __cplusplus
}
#endif

#endif /* AT24C02_DRV_H */
