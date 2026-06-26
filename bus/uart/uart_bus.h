/*@=========================================================================================================================*
 * UART BUS — UART 总线子系统 bus 层
 *
 * 架构位置: VFS Layer → [Bus Layer (本文件)] → HAL Layer
 *
 * 职责:
 *   - host/client 池管理 (静态数组, O(n) 查找, n=UART_BUS_HOST_MAX/CLIENT_MAX)
 *   - atomic ref_count (无锁引用计数, ISR 安全)
 *   - controller_ops (host 级生命周期: init/deinit/role/client_register/unregister)
 *   - bus_ops (client 级 I/O: open/close/read/write)
 *   - HAL vtable 封装 (hal_uart_bus_get() 返回的 ops 表)
 *
 * 隔离机制:
 *   - 本文件 (uart_bus.h) 在未定义 UART_BUS_IMPL 时, 用 #pragma GCC poison
 *     禁止外部直接调用 hal 层任何符号 (hal_uart_*, UART_STATE_*, 等)
 *   - 强制走 uart_bus_host_init / uart_bus_client_register 等 bus API
 *
 * 引用计数:
 *   - host->ref_count 为 atomic_int, client_register +1, unregister -1
 *   - host_deinit 检查 ref_count > 0 时返回 VFS_ERR_BUSY, 拒绝销毁
 *   - state 变更 (host init/deinit) 由上层 (uart_vfs.c probe/remove) 序列化
 *
 * controller_ops 设计:
 *   - init/deinit: host 生命周期 (绑定/解绑 bus_controller)
 *   - role: 返回 0 (UART 无 master/slave 之分)
 *   - client_register/unregister: client 挂载/卸载
 *
 * @see bus/bus.h  通用总线框架 (bus_controller_ops, bus_ops, bus_controller_bind_full)
 *@=========================================================================================================================*/
#ifndef UART_BUS_H
#define UART_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device;

typedef enum {
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN,
    UART_PARITY_ODD
} uart_bus_parity_t;

typedef enum {
    UART_STOP_BITS_1 = 0,
    UART_STOP_BITS_1_5,
    UART_STOP_BITS_2
} uart_bus_stop_bits_t;

typedef enum {
    UART_DATA_BITS_5 = 5,
    UART_DATA_BITS_6 = 6,
    UART_DATA_BITS_7 = 7,
    UART_DATA_BITS_8 = 8
} uart_bus_data_bits_t;

struct uart_bus_host_config {
    uint32_t             baud_rate;
    uart_bus_data_bits_t data_bits;
    uart_bus_parity_t    parity;
    uart_bus_stop_bits_t stop_bits;
    int                  tx_pin;
    int                  rx_pin;
    int                  uart_host;
};

struct uart_bus_client_config {
    int host_id;
};

/*===========================================================================================================================================================*/
                                                              /* Host API (VFS 层调用) */
/*===========================================================================================================================================================*/
int  uart_bus_host_init(struct device* dev,
                        const struct uart_bus_host_config* cfg)
    COMPAT_WARN_UNUSED_RESULT;
int  uart_bus_host_deinit(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

/*===========================================================================================================================================================*/
                                                              /* Client API (VFS 层调用) */
/*===========================================================================================================================================================*/
int  uart_bus_client_register(struct device* dev,const struct uart_bus_client_config* cfg)COMPAT_WARN_UNUSED_RESULT;
void uart_bus_client_unregister(struct device* dev);
int  uart_bus_open(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int  uart_bus_close(struct device* dev) COMPAT_WARN_UNUSED_RESULT;

int  uart_bus_write(struct device* dev,const uint8_t* data, size_t len,uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
int  uart_bus_read(struct device* dev,uint8_t* data, size_t len,uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#ifndef UART_BUS_IMPL
/* 禁止 bus 层外部直接调用 hal 层任何符号 — 强制走 uart_bus API */
#pragma GCC poison hal_uart_bus_get hal_uart_force_stop
#pragma GCC poison hal_uart_xfer_begin hal_uart_xfer_end
#pragma GCC poison hal_uart_config_t hal_uart_dev hal_uart_bus
#pragma GCC poison hal_uart_parity_t hal_uart_stop_bits_t hal_uart_data_bits_t
#pragma GCC poison hal_uart_rx_irq_callback_t
#pragma GCC poison hal_uart_bus_get
#pragma GCC poison UART_STATE_UNINIT UART_STATE_READY UART_STATE_BUSY UART_STATE_ERROR
#endif

#endif /* UART_BUS_H */
