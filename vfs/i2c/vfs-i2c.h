/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-i2c.h
 *@brief vfs-i2c 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   I2C VFS — I2C 总线子系统 VFS 层
 *   Driver 注册:
 *   - i2c_host_master / i2c_host_slave
 *   - heterogeneous,i2c-master-client / heterogeneous,i2c-slave-client
 *   write/read 默认 I2C_XFER_AUTO; ioctl SET_XFER_MODE 可选 POLL/DMA。
 *   @=========================================================================================================================
 */

#ifndef I2C_VFS_H
#define I2C_VFS_H

#include "compiler_compat.h"
#include "device.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define I2C_CMD_BASE COMPAT_MAGIC(I2C)
#define I2C_CMD_TRANSFER I2C_CMD_BASE + 0x01 /**< Master 同步传输 (arg.xfer_mode 可选) */
#define I2C_CMD_QUEUE_TX I2C_CMD_BASE + 0x02 /**< Slave: 入队发送 */
#define I2C_CMD_GET_TRANS_RESULT I2C_CMD_BASE + 0x03 /**< Slave: 取传输结果 */
#define I2C_CMD_SET_XFER_MODE                                                                      \
    I2C_CMD_BASE + 0x04 /**< 设置后续 write/read/transfer 的 xfer_mode                        \
                         */
#define I2C_CMD_GET_XFER_MODE I2C_CMD_BASE + 0x05 /**< 查询当前 xfer_mode */
#define I2C_CMD_COUNT 5

/** 与 HAL_I2C_XFER_* 同值 */
#define I2C_XFER_AUTO 0U /**< 隐式: DMA 可用则 DMA, 否则 poll */
#define I2C_XFER_POLL 1U /**< 强制 poll */
#define I2C_XFER_DMA 2U /**< 强制 DMA, 不可用返回 NOTSUPP */

    /** @brief I2C 传输参数 (ioctl TRANSFER) */
    struct i2c_transfer_arg
    {
        const uint8_t* tx; /**< 发送缓冲区 (可为 NULL) */
        uint8_t* rx; /**< 接收缓冲区 (可为 NULL) */
        size_t len; /**< 传输字节数 */
        uint32_t xfer_mode; /**< AUTO 时用 client 偏好 */
    };

    /** @brief I2C 传输模式切换参数 (ioctl SET_XFER_MODE / GET_XFER_MODE) */
    struct i2c_xfer_mode_arg
    {
        uint32_t xfer_mode; /**< I2C_XFER_AUTO / POLL / DMA */
    };

    /** @brief I2C 写队列参数 (ioctl WRITE) */
    struct i2c_queue_arg
    {
        const uint8_t* data; /**< 待发送数据 */
        size_t len; /**< 数据长度 */
    };

    /** @brief I2C 读结果参数 (ioctl READ) */
    struct i2c_trans_result_arg
    {
        uint8_t* data; /**< 接收缓冲区 */
        size_t len; /**< 缓冲区容量 */
        size_t* trans_len; /**< 输出: 实际接收字节数 */
    };

#ifdef __cplusplus
}
#endif

#ifndef I2C_VFS_IMPL
#pragma GCC poison i2c_bus_host_init i2c_bus_host_deinit i2c_bus_host_role
#pragma GCC poison i2c_bus_client_register i2c_bus_client_unregister
#pragma GCC poison i2c_bus_open i2c_bus_close i2c_bus_transfer i2c_bus_write i2c_bus_read
#pragma GCC poison i2c_bus_slave_sync i2c_bus_slave_queue_tx i2c_bus_slave_get_trans_result
#pragma GCC poison I2C_BUS_ROLE_MASTER I2C_BUS_ROLE_SLAVE
#endif

#endif /* I2C_VFS_H */
