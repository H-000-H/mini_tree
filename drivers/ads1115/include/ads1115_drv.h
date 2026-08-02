/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ads1115_drv.h
 * @brief ADS1115 16bit ADC 驱动 ioctl 命令与采样结构
 *
 * 挂在 I2C 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */
#ifndef ADS1115_DRV_H
#define ADS1115_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define ADS1115_CMD_BASE COMPAT_MAGIC(ADS1115)
/** 读取指定通道原始值（arg: struct ads1115_sample*） */
#define ADS1115_CMD_READ_CHANNEL (ADS1115_CMD_BASE + 0x01)
/** 命令总数 */
#define ADS1115_CMD_COUNT 1

    /** @brief ADS1115 单通道采样 */
    struct ads1115_sample
    {
        int channel; /**< 通道号（0..3） */
        int16_t raw; /**< 16bit 原始采样值（补码） */
    };
#ifdef __cplusplus
}
#endif
#endif /* ADS1115_DRV_H */
