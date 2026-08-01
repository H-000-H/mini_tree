/**
 * SPDX-License-Identifier: Apache-2.0
 * @file rc522_drv.h
 * @brief RC522 RFID 读卡驱动 ioctl 命令与 UID 结构
 *
 * 挂在 SPI 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef RC522_DRV_H
#define RC522_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define RC522_CMD_BASE COMPAT_MAGIC(RC522)
/** 初始化读卡器（含天线配置） */
#define RC522_CMD_INIT (RC522_CMD_BASE + 0x01)
/** 读取卡片 UID（arg: struct rc522_uid*） */
#define RC522_CMD_READ_UID (RC522_CMD_BASE + 0x02)
/** 命令总数 */
#define RC522_CMD_COUNT 2

/** @brief 卡片 UID（最长 10B，len 为有效长度） */
struct rc522_uid
{
    uint8_t uid[10];  /**< UID 字节 */
    uint8_t len;      /**< 有效长度 */
};
#ifdef __cplusplus
}
#endif
#endif /* RC522_DRV_H */
