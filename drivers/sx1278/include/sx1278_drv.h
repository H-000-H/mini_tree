/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sx1278_drv.h
 * @brief SX1278 LoRa 模块驱动 ioctl 命令与载荷结构
 *
 * 挂在 SPI 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef SX1278_DRV_H
#define SX1278_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define SX1278_CMD_BASE COMPAT_MAGIC(SX1278)
/** 复位模块 */
#define SX1278_CMD_RESET (SX1278_CMD_BASE+0x01)
/** 设置中心频率（arg: uint32_t*，Hz） */
#define SX1278_CMD_SET_FREQ (SX1278_CMD_BASE+0x02)
/** 发送载荷（arg: struct sx1278_payload*） */
#define SX1278_CMD_SEND (SX1278_CMD_BASE+0x03)
/** 接收载荷（arg: struct sx1278_payload*） */
#define SX1278_CMD_RECV (SX1278_CMD_BASE+0x04)
/** 命令总数 */
#define SX1278_CMD_COUNT 4
/** SEND: data=TX；RECV: data=RX 缓冲（可写） */
struct sx1278_payload
{
    uint8_t* data;  /**< 载荷缓冲 */
    size_t   len;   /**< 载荷长度 */
};
#ifdef __cplusplus
}
#endif
#endif /* SX1278_DRV_H */
