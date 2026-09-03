/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file pn532_drv.h
 *@brief PN532 NFC 模块驱动 ioctl 命令与固件信息结构
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 */

#ifndef PN532_DRV_H
#define PN532_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define PN532_CMD_BASE MINI_MAGIC(PN532)
/** 获取固件版本（arg: struct pn532_fw*） */
#define PN532_CMD_GET_FIRMWARE (PN532_CMD_BASE + 0x01)
/** 命令总数 */
#define PN532_CMD_COUNT 1

/** @brief PN532 固件版本信息 */
struct pn532_fw
{
    uint8_t ic;      /**< IC 型号 */
    uint8_t ver;     /**< 主版本 */
    uint8_t rev;     /**< 修订版本 */
    uint8_t support; /**< 支持位图 */
};
#ifdef __cplusplus
}
#endif
#endif /* PN532_DRV_H */
