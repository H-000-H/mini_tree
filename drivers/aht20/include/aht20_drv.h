/**
 * SPDX-License-Identifier: Apache-2.0
 * @file aht20_drv.h
 * @brief AHT20 温湿度传感器驱动 ioctl 命令与采样结构
 *
 * 挂在 I2C 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */
#ifndef AHT20_DRV_H
#define AHT20_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define AHT20_CMD_BASE COMPAT_MAGIC(AHT20)
/** 读取温度/湿度（arg: struct aht20_sample*） */
#define AHT20_CMD_READ_TEMP_RH (AHT20_CMD_BASE + 0x01)
/** 命令总数 */
#define AHT20_CMD_COUNT 1

    /** @brief AHT20 采样结果 */
    struct aht20_sample
    {
        int16_t temp_c_x100; /**< 温度，摄氏度 ×100（25.5℃ → 2550） */
        uint16_t rh_x100; /**< 相对湿度 ×100（0..10000，60% → 6000） */
    };
#ifdef __cplusplus
}
#endif
#endif /* AHT20_DRV_H */
