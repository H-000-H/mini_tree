/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file nrf24l01_drv.h
 *@brief NRF24L01 2.4G 无线驱动 ioctl 命令与参数结构
 *@author H-000-H
 *@details
 *   挂在 SPI 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef NRF24L01_DRV_H
#define NRF24L01_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define NRF24L01_CMD_BASE MINI_MAGIC(NRF24L01)
/** 写寄存器（arg: struct nrf24l01_reg*） */
#define NRF24L01_CMD_WRITE_REG (NRF24L01_CMD_BASE + 0x01)
/** 读寄存器（arg: struct nrf24l01_reg*，val 回填） */
#define NRF24L01_CMD_READ_REG (NRF24L01_CMD_BASE + 0x02)
/** 发送载荷（arg: struct nrf24l01_payload*） */
#define NRF24L01_CMD_SEND (NRF24L01_CMD_BASE + 0x03)
/** 命令总数 */
#define NRF24L01_CMD_COUNT 3

    /** @brief 寄存器读写参数 */
    struct nrf24l01_reg
    {
        uint8_t reg; /**< 寄存器地址 */
        uint8_t val; /**< 写入值/读出值 */
    };

    /** @brief 发送载荷参数（长度 ≤ NRF24L01_MAX_PAYLOAD） */
    struct nrf24l01_payload
    {
        uint8_t* data; /**< 载荷缓冲 */
        size_t len; /**< 载荷长度 */
    };
#ifdef __cplusplus
}
#endif
#endif /* NRF24L01_DRV_H */
