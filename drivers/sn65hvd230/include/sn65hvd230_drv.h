/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sn65hvd230_drv.h
 * @brief SN65HVD230 CAN 收发器驱动 ioctl 命令
 *
 * 挂在 GPIO 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef SN65HVD230_DRV_H
#define SN65HVD230_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define SN65HVD230_CMD_BASE COMPAT_MAGIC(SN65HVD230)
/** 设置待机模式（arg: int*，0=正常 1=待机） */
#define SN65HVD230_CMD_SET_STANDBY (SN65HVD230_CMD_BASE+0x01)
/** 命令总数 */
#define SN65HVD230_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif /* SN65HVD230_DRV_H */
