/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bme280_drv.h
 * @brief BME280 温湿度/气压传感器驱动 ioctl 命令与采样结构
 *
 * 挂在 I2C 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */
#ifndef BME280_DRV_H
#define BME280_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define BME280_CMD_BASE COMPAT_MAGIC(BME280)
/** 读取温度/湿度/气压（arg: struct bme280_env*） */
#define BME280_CMD_READ_ENV (BME280_CMD_BASE + 0x01)
/** 命令总数 */
#define BME280_CMD_COUNT 1

    /** @brief BME280 环境采样结果 */
    struct bme280_env
    {
        int16_t temp_c_x100; /**< 温度，摄氏度 ×100 */
        uint16_t humidity_x100; /**< 相对湿度 ×100（0..10000） */
        uint32_t pressure; /**< 气压，Pa */
    };
#ifdef __cplusplus
}
#endif
#endif /* BME280_DRV_H */
