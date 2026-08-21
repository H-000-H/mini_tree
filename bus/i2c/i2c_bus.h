/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file i2c_bus.h
 *@brief i2c bus 头文件
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   I2C BUS — I2C 总线子系统 bus 层
 *   架构: VFS → [Bus (本文件)] → HAL; hal_i2c_bus_host 嵌入 i2c_bus_host (无 vtable)
 *   职责: host/client 池 + atomic ref_count + controller_ops (host 生命周期) +
 *   client I/O (open/close/transfer/read/write) + 同步传输 (poll/DMA via xfer_mode)
 *   隔离: 未定义 I2C_BUS_IMPL 时 #pragma GCC poison 禁止外部调 hal_i2c_*;
 *   允许 config 类型供 VFS 填充, 强制走 i2c_bus API
 *   引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY
 *   @see bus/bus.h  通用总线框架
 *   @=========================================================================================================================
 */

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "compiler_compat.h"
#include "hal_i2c.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct device;
    struct i2c_bus_client;

#define I2C_BUS_ROLE_MASTER HAL_I2C_BUS_ROLE_MASTER
#define I2C_BUS_ROLE_SLAVE HAL_I2C_BUS_ROLE_SLAVE

    /*===========================================================================================================================================================*/
    /*Host API (VFS 层调用)*/
    /*===========================================================================================================================================================*/
    /**
     * @brief I2C host 初始化 (config 类型直接用 hal_i2c_bus_config, bus 零翻译透传)
     * @param pdev controller device (host)
     * @param cfg host 配置 (VFS 填充 DTSI 硬件直投值)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_host_init(struct device* pdev,
                          const struct hal_i2c_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief I2C host 反初始化 (ref_count > 0 时返回 BUSY)
     * @param pdev controller device (host)
     * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
     */
    int i2c_bus_host_deinit(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 查询 I2C host 角色 (master/slave)
     * @param pdev controller device (host)
     * @return master 返回 I2C_BUS_ROLE_MASTER, slave 返回 I2C_BUS_ROLE_SLAVE, 失败返回 -1
     */
    int i2c_bus_host_role(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /*===========================================================================================================================================================*/

    /*Client API (VFS 层调用)*/
    /*===========================================================================================================================================================*/
    /**
     * @brief I2C client 注册 (config 类型直接用 hal_i2c_device_config, bus 零翻译透传)
     * @param pdev client device
     * @param cfg client 配置 (VFS 填充 DTSI 硬件直投值)
     * @param out 输出 i2c_bus_client 指针
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_client_register(struct device* pdev, const struct hal_i2c_device_config* cfg,
                                struct i2c_bus_client** out) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 注销 I2C client 并递减 host 引用计数 (ref_count -1, 清零槽位)
     * @param pdev client device
     */
    void i2c_bus_client_unregister(struct device* pdev);

    /**
     * @brief 打开 I2C client 硬件 (幂等)
     * @param pdev client device
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_open(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭 I2C client 硬件 (幂等)
     * @param pdev client device
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_close(struct device* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief I2C 同步传输 (tx 非空 rx 空→写; tx 空 rx 非空→读; 两者非空→先写后读)
     * @param pdev client device
     * @param tx 发送缓冲区 (可 NULL)
     * @param rx 接收缓冲区 (可 NULL)
     * @param len 传输字节数
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_transfer(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                         uint32_t timeout_ms, uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief I2C 读数据
     * @param pdev client device
     * @param rx 接收缓冲区
     * @param len 读取长度
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_read(struct device* pdev, uint8_t* rx, size_t len, uint32_t timeout_ms,
                     uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief I2C 写数据
     * @param pdev client device
     * @param tx 发送缓冲区
     * @param len 写入长度
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_write(struct device* pdev, const uint8_t* tx, size_t len, uint32_t timeout_ms,
                      uint32_t xfer_mode) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief I2C slave 模式同步传输 (空壳: STM32 返回 NOTSUPP)
     * @param pdev client device
     * @param tx 发送缓冲区
     * @param rx 接收缓冲区
     * @param len 传输字节数
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_slave_sync(struct device* pdev, const uint8_t* tx, uint8_t* rx, size_t len,
                           uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief I2C slave 模式排队发送 (空壳: STM32 返回 NOTSUPP)
     * @param pdev client device
     * @param data 发送数据
     * @param len 数据长度
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_slave_queue_tx(struct device* pdev, const uint8_t* data, size_t len,
                               uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief I2C slave 模式获取传输结果 (空壳: STM32 返回 NOTSUPP)
     * @param pdev client device
     * @param rx_data 接收缓冲区
     * @param rx_cap 接收缓冲区容量
     * @param trans_len 输出实际传输长度
     * @param timeout_ms 超时 (毫秒)
     * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
     */
    int i2c_bus_slave_get_trans_result(struct device* pdev, uint8_t* rx_data, size_t rx_cap,
                                       size_t* trans_len,
                                       uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#ifndef I2C_BUS_IMPL
/* 禁止 bus 层外部直接调用 HAL 函数 — 强制走 i2c_bus API。
 * 允许 config 类型 (hal_i2c_bus_config, hal_i2c_device_config, hal_i2c_pin_cfg 等)
 * 供 VFS 层填充 DTSI 值。 */
#pragma GCC poison hal_i2c_bus_host_init hal_i2c_bus_host_deinit
#pragma GCC poison hal_i2c_dev_init hal_i2c_dev_deinit
#pragma GCC poison hal_i2c_dev_hw_open hal_i2c_dev_hw_close
#pragma GCC poison hal_i2c_sync hal_i2c_write hal_i2c_read
#pragma GCC poison hal_i2c_dma_write hal_i2c_dma_read hal_i2c_dma_write_then_read
#endif

#endif /* I2C_BUS_H */
