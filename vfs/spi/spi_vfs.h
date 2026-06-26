/*@=========================================================================================================================*
 * SPI VFS — SPI 总线子系统 VFS 层
 *
 * 架构位置: [VFS Layer (本文件)] → Bus Layer → HAL Layer
 *
 * 职责:
 *   - file_operations 挂载 (open/close/read/write/ioctl)
 *   - dev_lifecycle (open/close/io 互斥, 引用计数)
 *   - DTS 参数解析 (host 级 + client 级)
 *   - I/O 全走 bus 层接口, 不直接碰 HAL
 *   - 对上层驱动提供统一的 file_operations 接口
 *
 * 隔离机制:
 *   - spi_vfs.c 定义 SPI_VFS_IMPL, 可调用 spi_bus API
 *   - 其他文件包含本头, spi_bus 符号被 #pragma GCC poison, 强制走 spi_vfs API
 *
 * 便捷 API:
 *   - spi_vfs_transfer: 封装 ioctl(SPI_CMD_TRANSFER), 上层驱动 (如 w25q64) 调用
 *
 * Driver 注册:
 *   - spi_host_esp32_master: compatible="esp32,spi-master" (host controller)
 *   - spi_host_esp32_slave:  compatible="esp32,spi" (slave host controller)
 *   - spi_vfs_master:        compatible="heterogeneous,spi-master-client" (bus client)
 *   - spi_vfs_slave:         compatible="heterogeneous,fft-spi-slave" (slave client)
 *
 * @see bus/spi/spi_bus.h  bus 层接口
 * @see bus/bus.h          通用总线框架
 *@=========================================================================================================================*/
#ifndef SPI_VFS_H
#define SPI_VFS_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

                                                            /*SPI VFS ioctl 命令与参数*/
/*===========================================================================================================================================================*/
/** @defgroup spi_vfs_ioctl SPI VFS ioctl 命令
 *  @{
 */
#define SPI_CMD_BASE             COMPAT_MAGIC(SPI)
#define SPI_CMD_TRANSFER         SPI_CMD_BASE + 0x01  /**< Master 全双工传输 */
#define SPI_CMD_QUEUE_TX         SPI_CMD_BASE + 0x02  /**< Slave: 入队发送 */
#define SPI_CMD_GET_TRANS_RESULT SPI_CMD_BASE + 0x03  /**< Slave: 取传输结果 */

/** @brief 全双工传输参数 */
struct spi_transfer_arg {
    const uint8_t* tx;     /**< 发送缓冲, NULL 表示只收 */
    uint8_t*       rx;     /**< 接收缓冲, NULL 表示只发 */
    size_t         len;    /**< 传输字节数 */
};

/** @brief Slave 队列发送参数 */
struct spi_queue_arg {
    const uint8_t* data;   /**< 数据指针 */
    size_t         len;    /**< 数据长度 */
};

/** @brief Slave 取传输结果参数 */
struct spi_trans_result_arg {
    uint8_t* data;         /**< 接收缓冲 */
    size_t   len;          /**< 缓冲大小 */
    size_t*  trans_len;    /**< 实际接收长度 (输出) */
};
/** @} */
/*===========================================================================================================================================================*/

                                                            /*便捷 API (上层驱动调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief 便捷 SPI 传输 (带锁)
 * @param dev  SPI 设备
 * @param tx   发送缓冲
 * @param rx   接收缓冲
 * @param len  传输长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回传输字节数, 失败返回 VFS_ERR_*
 */
int spi_vfs_transfer(struct device* dev,
                     const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;

/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#ifndef SPI_VFS_IMPL
/* 禁止 VFS 层外部直接调用 spi_bus 层任何符号 — 强制走 spi_vfs API */
#pragma GCC poison spi_bus_host_init spi_bus_host_deinit spi_bus_host_role
#pragma GCC poison spi_bus_client_register spi_bus_client_unregister
#pragma GCC poison spi_bus_open spi_bus_close spi_bus_transfer
#pragma GCC poison spi_bus_slave_sync spi_bus_slave_queue_tx spi_bus_slave_get_trans_result
#pragma GCC poison spi_bus_transfer_async spi_bus_transfer_poll
#pragma GCC poison spi_bus_host_config spi_bus_client_config
#pragma GCC poison SPI_BUS_ROLE_MASTER SPI_BUS_ROLE_SLAVE
#endif

#endif /* SPI_VFS_H */
