/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * UART BUS — UART 总线子系统 bus 层
 *
 * 架构: VFS → [Bus (本文件)] → HAL; hal_uart_dev 嵌入 uart_bus_host (无 vtable)
 * 职责: host/client 池 + atomic ref_count + controller_ops + client I/O (open/close/read/write)
 *
 * 隔离: 未定义 UART_BUS_IMPL 时 #pragma GCC poison 禁止外部调 hal_uart_* 与 UART_STATE_*;
 *   允许 hal_uart_config 供 VFS 填充, 强制走 uart_bus API
 *
 * 引用计数: host->ref_count atomic, register +1/unregister -1, deinit >0 返回 BUSY
 * @see bus/bus.h  通用总线框架
 *@=========================================================================================================================*/
#ifndef UART_BUS_H
#define UART_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include "compiler_compat.h"
#include "hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;

/*===========================================================================================================================================================*/
                                                              /*Host API (VFS 层调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief UART host 初始化 (config 类型直接用 hal_uart_config, bus 零翻译透传)
 * @param dev controller device (host)
 * @param cfg host 配置 (VFS 填充 DTSI 硬件直投值)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  uart_bus_host_init(struct device* dev,
                        const struct hal_uart_config* cfg)
    COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief UART host 反初始化 (ref_count > 0 时返回 BUSY)
 * @param dev controller device (host)
 * @return 成功返回 VFS_OK, BUSY 返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
 */
int  uart_bus_host_deinit(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

/*===========================================================================================================================================================*/
                                                              /*Client API (VFS 层调用)*/
/*===========================================================================================================================================================*/
/**
 * @brief UART client 注册 (UART 无 per-client 配置, 单 host 单 client, 无需 cfg)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  uart_bus_client_register(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief UART client 注销 (ref_count -1, 清零槽位)
 * @param dev client device
 */
void uart_bus_client_unregister(struct device* dev);

/**
 * @brief 打开 UART client (ref_count 在 register/unregister 维护, 此处仅 IO gate)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 */
int  uart_bus_open(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 关闭 UART client (仅 IO gate, 不改 ref_count)
 * @param dev client device
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_NODEV
 */
int  uart_bus_close(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief UART 写数据
 * @param dev client device
 * @param data 待写入数据
 * @param len 数据长度
 * @param timeout_ms 超时 (毫秒, 当前实现未使用)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  uart_bus_write(struct device* dev,const uint8_t* data, size_t len,uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief UART 读数据
 * @param dev client device
 * @param data 读取缓冲区
 * @param len 读取长度
 * @param timeout_ms 超时 (毫秒, 当前实现未使用)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int  uart_bus_read(struct device* dev,uint8_t* data, size_t len,uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#ifndef UART_BUS_IMPL
/* 禁止 bus 层外部直接调用 HAL 函数 — 强制走 uart_bus API。
 * 允许 config 类型 (hal_uart_config, hal_uart_pin_cfg) 供 VFS 层填充 DTSI 值。 */
#pragma GCC poison hal_uart_dev_init hal_uart_dev_hw_open hal_uart_dev_hw_close
#pragma GCC poison hal_uart_write hal_uart_read hal_uart_force_stop
#pragma GCC poison hal_uart_write_dma hal_uart_dma_abort
#pragma GCC poison UART_STATE_UNINIT UART_STATE_READY UART_STATE_BUSY UART_STATE_ERROR
#endif

#endif /* UART_BUS_H */
