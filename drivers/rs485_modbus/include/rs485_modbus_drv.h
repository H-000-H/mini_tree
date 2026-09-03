/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file rs485_modbus_drv.h
 *@brief RS485 Modbus RTU 驱动 ioctl 命令与读写参数结构
 *@author H-000-H
 *@details
 *   挂在 UART 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef RS485_MODBUS_DRV_H
#define RS485_MODBUS_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define RS485_MODBUS_CMD_BASE MINI_MAGIC(RS485_MODBUS)
/** 读保持寄存器（arg: struct modbus_read*，value 回填） */
#define RS485_MODBUS_CMD_READ_HOLDING (RS485_MODBUS_CMD_BASE + 0x01)
/** 写单寄存器（arg: struct modbus_write*） */
#define RS485_MODBUS_CMD_WRITE_SINGLE (RS485_MODBUS_CMD_BASE + 0x02)
/** 命令总数 */
#define RS485_MODBUS_CMD_COUNT 2

/** @brief Modbus 读保持寄存器参数 */
struct modbus_read
{
    uint8_t  slave; /**< 从站地址（1..247） */
    uint16_t addr;  /**< 寄存器地址（0 基） */
    uint16_t value; /**< 读回值（回填） */
};

/** @brief Modbus 写单寄存器参数 */
struct modbus_write
{
    uint8_t  slave; /**< 从站地址（1..247） */
    uint16_t addr;  /**< 寄存器地址（0 基） */
    uint16_t value; /**< 写入值 */
};
#ifdef __cplusplus
}
#endif
#endif /* RS485_MODBUS_DRV_H */
