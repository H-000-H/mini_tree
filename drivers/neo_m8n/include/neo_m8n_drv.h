/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file neo_m8n_drv.h
 *@brief NEO-M8N GPS 模块驱动 ioctl 命令与 NMEA 缓冲结构
 *@author H-000-H
 *@details
 *   挂在 UART 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef NEO_M8N_DRV_H
#define NEO_M8N_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define NEO_M8N_CMD_BASE MINI_MAGIC(NEO_M8N)
/** 读取 NMEA 语句（arg: struct neo_m8n_buf*） */
#define NEO_M8N_CMD_READ_NMEA (NEO_M8N_CMD_BASE + 0x01)
/** 命令总数 */
#define NEO_M8N_CMD_COUNT 1

/** @brief NMEA 语句接收缓冲（len 回填实际长度） */
struct neo_m8n_buf
{
    char*  data; /**< 接收缓冲 */
    size_t cap;  /**< 缓冲容量 */
    size_t len;  /**< 实际长度（回填） */
};
#ifdef __cplusplus
}
#endif
#endif /* NEO_M8N_DRV_H */
