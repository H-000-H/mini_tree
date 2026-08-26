/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file ina219_drv.h
 *@brief INA219 电流/功率监测驱动 ioctl 命令与采样结构
 *@author H-000-H
 *@details
 *   挂在 I2C 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问，不直接操作 I2C 总线。
 */

#ifndef INA219_DRV_H
#define INA219_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define INA219_CMD_BASE MINI_MAGIC(INA219)
/** 读取电压/电流/功率（arg: struct ina219_sample*） */
#define INA219_CMD_READ_POWER (INA219_CMD_BASE + 0x01)
/** 命令总数 */
#define INA219_CMD_COUNT 1

    /** @brief INA219 电源监测结果 */
    struct ina219_sample
    {
        int16_t bus_mV; /**< 总线电压，mV */
        int16_t current_mA; /**< 分流电流，mA */
        int16_t power_mW; /**< 功率，mW */
    };
#ifdef __cplusplus
}
#endif
#endif /* INA219_DRV_H */
