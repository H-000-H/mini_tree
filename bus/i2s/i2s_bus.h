/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file i2s_bus.h
 *@brief i2s bus 头文件
 *@author H-000-H
 *@details
 *   --------------------------------------------------------------------------
 *   I2S BUS — I2S 总线子系统 bus 层
 *   架构: VFS → [Bus (本文件)] → HAL; hal_i2s_bus_host 嵌入 i2s_bus_host
 *   职责: host/client 池 + ref_count + open/close/transfer; sync/poll/DMA + circular +
 *   irq_mode/async。 open: 若 it_enable, 注册 VIRQ(i2s, hw_idx) + NVIC (对齐 ADC probe; 不进
 *ioctl)。 隔离: 未定义 I2S_BUS_IMPL 时 #pragma GCC poison 禁止外部调 hal_i2s_*; 允许 config 类型供
 *VFS 填充, 强制走 i2s_bus API。
 *   @see bus/bus.h  通用总线框架
 *   @see hal/i2s/hal_i2s.h
 *   --------------------------------------------------------------------------
 */

#ifndef I2S_BUS_H
#define I2S_BUS_H

#include "compiler_compat.h"
#include "hal_i2s.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct device;
struct i2s_bus_client;

#define I2S_BUS_ROLE_MASTER HAL_I2S_BUS_ROLE_MASTER
#define I2S_BUS_ROLE_SLAVE HAL_I2S_BUS_ROLE_SLAVE

/**
 * @brief I2S host 初始化
 * @param[in] pdev controller device (host)
 * @param[in] cfg host 配置 (VFS 填充 DTSI 值)
 * @return MINI_OK 或 VFS_ERR_*
 */
/**
 * @brief 初始化 I2S 总线主机
 * @param[in] pdev controller device (host)
 * @param[in] cfg host 配置 (VFS 填充 DTSI 值)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
int i2s_bus_host_init(struct device* pdev, const struct hal_i2s_bus_config* cfg) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 反初始化 I2S 总线主机, 释放硬件资源
 * @param[in] pdev controller device (host)
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int i2s_bus_host_deinit(struct device* pdev) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 查询 I2S 总线主机角色
 * @param[in] pdev controller device (host)
 * @return 主机角色 (I2S_BUS_ROLE_MASTER/SLAVE); 失败返回负错误码
 */
int i2s_bus_host_role(struct device* pdev) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 注册 I2S client 设备并绑定配置
 * @param[in] pdev controller device (host)
 * @param[in] cfg client 级设备配置 (DTSI 直投)
 * @param[out] out 回传已注册 client 对象指针
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
int i2s_bus_client_register(struct device* pdev, const struct hal_i2s_device_config* cfg, struct i2s_bus_client** out) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 注销 I2S client 设备
 * @param[in] pdev controller device (host)
 */
void i2s_bus_client_unregister(struct device* pdev);
/**
 * @brief 打开 I2S 设备 (引用计数 +1, 首次触发主机硬件 init)
 * @param[in] pdev controller device (host)
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int i2s_bus_open(struct device* pdev) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 关闭 I2S 设备 (引用计数 -1)
 * @param[in] pdev controller device (host)
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int i2s_bus_close(struct device* pdev) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 同步传输 (samples 为 16-bit 采样数)
 * @param[in] pdev controller device (host)
 * @param[in] tx 发送采样缓冲区 (纯接收可为空)
 * @param[out] rx 接收采样缓冲区 (纯发送可为空)
 * @param[in] samples 传输采样数 (16-bit)
 * @param[in] timeout_ms 超时毫秒数 (0=不等待)
 * @param[in] xfer_mode 传输模式 (sync/poll/DMA)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_* (超时 MINI_ERR_TIMEOUT)
 */
int i2s_bus_transfer(struct device* pdev, const uint16_t* tx, uint16_t* rx, size_t samples, uint32_t timeout_ms,
                     uint32_t xfer_mode) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 异步传输 (参数存档占位; DMA+IT 后续补)
 * @param[in] pdev controller device (host)
 * @param[in] tx 发送采样缓冲区
 * @param[out] rx 接收采样缓冲区
 * @param[in] samples 传输采样数 (16-bit)
 * @param[in] cb 传输完成回调 (device, 上下文, userdata)
 * @param[in] userdata 回调私有数据
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
int i2s_bus_transfer_async(struct device* pdev, const uint16_t* tx, uint16_t* rx, size_t samples, void (*cb)(struct device*, const void*, void*),
                           void* userdata) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 轮询异步传输完成 (占位)
 * @param[in] pdev controller device (host)
 * @param[in] timeout_ms 超时毫秒数 (0=不等待)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
int i2s_bus_transfer_poll(struct device* pdev, uint32_t timeout_ms) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 设置 DMA 中断模式
 * @param[in] pdev controller device (host)
 * @param[in] irq_mode 中断模式 (0=禁用, 1=启用)
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int i2s_bus_set_dma_irq_mode(struct device* pdev, uint32_t irq_mode) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 获取 DMA 中断模式
 * @param[in] pdev controller device (host)
 * @param[out] irq_mode 回传当前中断模式
 * @return 成功返回 MINI_OK, pdev 或 irq_mode 为空返回 MINI_ERR_INVAL
 */
int i2s_bus_get_dma_irq_mode(struct device* pdev, uint32_t* irq_mode) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 启动 DMA 循环缓冲传输
 * @param[in] pdev controller device (host)
 * @param[in] tx_enable 使能 TX 循环 (0/1)
 * @param[in] rx_enable 使能 RX 循环 (0/1)
 * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
 */
int i2s_bus_dma_circ_start(struct device* pdev, int tx_enable, int rx_enable) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 停止 DMA 循环缓冲传输
 * @param[in] pdev controller device (host)
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int i2s_bus_dma_circ_stop(struct device* pdev) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 向 DMA 循环 TX 缓冲写入采样
 * @param[in] pdev controller device (host)
 * @param[in] data 发送采样缓冲区
 * @param[in] samples 写入采样数 (16-bit)
 * @return 成功返回 MINI_OK, 缓冲满返回 MINI_ERR_NOMEM, 失败返回 VFS_ERR_*
 */
int i2s_bus_dma_circ_write(struct device* pdev, const uint16_t* data, uint32_t samples) MINI_WARN_UNUSED_RESULT;
/**
 * @brief 从 DMA 循环 RX 缓冲读取采样
 * @param[in] pdev controller device (host)
 * @param[out] data 接收采样缓冲区
 * @param[in] samples 读取采样数 (16-bit)
 * @return 成功返回 MINI_OK, 数据不足返回 MINI_ERR_AGAIN, 失败返回 VFS_ERR_*
 */
int i2s_bus_dma_circ_read(struct device* pdev, uint16_t* data, uint32_t samples) MINI_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#ifndef I2S_BUS_IMPL
#pragma GCC poison hal_i2s_bus_host_init hal_i2s_bus_host_deinit
#pragma GCC poison hal_i2s_dev_init hal_i2s_dev_deinit
#pragma GCC poison hal_i2s_dev_hw_open hal_i2s_dev_hw_close
#pragma GCC poison hal_i2s_sync hal_i2s_transfer_async hal_i2s_transfer_poll
#pragma GCC poison hal_i2s_set_dma_irq_mode hal_i2s_get_dma_irq_mode
#pragma GCC poison hal_i2s_dma_circ_start hal_i2s_dma_circ_stop
#pragma GCC poison hal_i2s_dma_circ_write hal_i2s_dma_circ_read
#endif

#endif
