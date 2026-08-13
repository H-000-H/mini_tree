/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bmp280_drv.h
 * @brief BMP280 气压/温度传感器驱动 ioctl 命令与采样结构
 *
 * 挂在 I2C 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */
#ifndef BMP280_DRV_H
#define BMP280_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define BMP280_CMD_BASE COMPAT_MAGIC(BMP280)
/** 读取气压/温度（arg: struct bmp280_sample*） */
#define BMP280_CMD_READ_PRESS_TEMP (BMP280_CMD_BASE + 0x01)
/** 命令总数 */
#define BMP280_CMD_COUNT 1

    /** @brief BMP280 采样结果 */
    struct bmp280_sample
    {
        int32_t press_pa; /**< 气压，Pa */
        int16_t temp_c_x100; /**< 温度，摄氏度 ×100 */
    };
#ifdef __cplusplus
}
#endif
#endif /* BMP280_DRV_H */
