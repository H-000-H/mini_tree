/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * SPI BUS — SPI 总线子系统 bus 层
 *
 * 架构: VFS → [Bus (本文件)] → HAL; hal_spi_bus_host 嵌入 spi_bus_host (无 vtable)
 * 职责: host/client 池 + atomic ref_count + controller_ops (host 生命周期) +
 *   client I/O (open/close/transfer) + 异步传输 (async/poll, master, DMA+ISR)
 *
 * 隔离: 未定义 SPI_BUS_IMPL 时 #pragma GCC poison 禁止外部调 hal_spi_* 与 spi_sync;
 *   允许 config 类型供 VFS 填充, 强制走 spi_bus API
 *
 * 引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY
 * @see bus/bus.h  通用总线框架
 *@=========================================================================================================================*/
#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include "compiler_compat.h"
#include "hal_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;
struct spi_bus_client;

#define SPI_BUS_ROLE_MASTER HAL_SPI_BUS_ROLE_MASTER
#define SPI_BUS_ROLE_SLAVE  HAL_SPI_BUS_ROLE_SLAVE

/*===========================================================================================================================================================*/
                                                              /*Host API (VFS 层调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI host 初始化 (config 类型直接用 hal_spi_bus_config, bus 零翻译透传)
 * @param dev controller device (host)
 * @param cfg host 配置 (VFS 填充 DTSI 硬件直投值)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_host_init(struct device* dev,
                       const struct hal_spi_bus_config* cfg)
    COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief SPI host 反初始化 (ref_count > 0 时返回 BUSY)
 * @param dev controller device (host)
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
 */
int  spi_bus_host_deinit(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 查询 SPI host 角色 (master/slave)
 * @param dev controller device (host)
 * @return master 返回 SPI_BUS_ROLE_MASTER, slave 返回 SPI_BUS_ROLE_SLAVE, 失败返回 -1
 */
int  spi_bus_host_role(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                              /*Client API (VFS 层调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI client 注册 (config 类型直接用 hal_spi_device_config, bus 零翻译透传)
 * @param dev client device
 * @param cfg client 配置 (VFS 填充 DTSI 硬件直投值)
 * @param out 输出 spi_bus_client 指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_client_register(struct device* dev,
                             const struct hal_spi_device_config* cfg,
                             struct spi_bus_client** out)
    COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief SPI client 注销 (ref_count -1, 清零槽位)
 * @param dev client device
 */
void spi_bus_client_unregister(struct device* dev);

/**
 * @brief 打开 SPI client 硬件 (幂等)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_open(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 关闭 SPI client 硬件 (幂等)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_close(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief SPI 同步全双工传输
 * @param dev client device
 * @param tx 发送缓冲区 (可 NULL)
 * @param rx 接收缓冲区 (可 NULL)
 * @param len 传输字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_transfer(struct device* dev,
                      const uint8_t* tx, uint8_t* rx,
                      size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief SPI slave 模式同步传输
 * @param dev client device
 * @param tx 发送缓冲区
 * @param rx 接收缓冲区
 * @param len 传输字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_slave_sync(struct device* dev,
                        const uint8_t* tx, uint8_t* rx,
                        size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief SPI slave 模式排队发送
 * @param dev client device
 * @param data 发送数据
 * @param len 数据长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_slave_queue_tx(struct device* dev,
                            const uint8_t* data, size_t len,
                            uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief SPI slave 模式获取传输结果
 * @param dev client device
 * @param rx_data 接收缓冲区
 * @param rx_cap 接收缓冲区容量
 * @param trans_len 输出实际传输长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_slave_get_trans_result(struct device* dev,
                                    uint8_t* rx_data, size_t rx_cap,
                                    size_t* trans_len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief SPI 异步传输 (master 模式, 回调在 ISR 触发)
 * @param dev client device
 * @param tx 发送缓冲区
 * @param rx 接收缓冲区
 * @param len 传输字节数
 * @param cb 传输完成回调
 * @param userdata 回调用户数据
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_transfer_async(struct device* dev,
                            const uint8_t* tx, uint8_t* rx,
                            size_t len, void (*cb)(struct device* dev,
                                                   const void* trans,
                                                   void* userdata),
                            void* userdata)
    COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 轮询等待异步传输完成
 * @param dev client device
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  spi_bus_transfer_poll(struct device* dev, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#ifndef SPI_BUS_IMPL
/* 禁止 bus 层外部直接调用 HAL 函数 — 强制走 spi_bus API。
 * 允许 config 类型 (hal_spi_bus_config, hal_spi_device_config, hal_spi_pin_cfg 等)
 * 供 VFS 层填充 DTSI 值。 */
#pragma GCC poison hal_spi_bus_host_init hal_spi_bus_host_deinit
#pragma GCC poison hal_spi_dev_init hal_spi_dev_hw_open hal_spi_dev_hw_close
#pragma GCC poison hal_spi_dev_register hal_spi_dev_unregister
#pragma GCC poison spi_sync spi_slave_sync spi_slave_queue_tx hal_spi_get_trans_result
#pragma GCC poison hal_spi_transfer
#pragma GCC poison hal_spi_transfer_async hal_spi_transfer_poll
#pragma GCC poison hal_spi_callback_t
#pragma GCC poison spi_device_config spi_bus_config spi_controller spi_device
#endif

#endif /* SPI_BUS_H */
