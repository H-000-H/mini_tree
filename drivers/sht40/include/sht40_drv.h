/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sht40_drv.h
 *@brief SHT40 温湿度传感器驱动 ioctl 命令与采样结构
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */

#ifndef SHT40_DRV_H
#define SHT40_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define SHT40_CMD_BASE COMPAT_MAGIC(SHT40)
/** 读取温度/湿度（arg: struct sht40_sample*） */
#define SHT40_CMD_READ_TEMP_RH (SHT40_CMD_BASE + 0x01)
/** 命令总数 */
#define SHT40_CMD_COUNT 1

    /** @brief SHT40 采样结果 */
    struct sht40_sample
    {
        int16_t temp_c_x100; /**< 温度，摄氏度 ×100 */
        uint16_t rh_x100; /**< 相对湿度 ×100（0..10000） */
    };
#ifdef __cplusplus
}
#endif
#endif /* SHT40_DRV_H */
