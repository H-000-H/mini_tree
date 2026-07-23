/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "compiler_compat.h"
#include "status.h"
#include "hal_uart.h"

COMPAT_WEAK int hal_uart_dev_init(struct hal_uart_bus_host* host,const struct hal_uart_config* cfg)
{
    (void)host;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_dev_hw_open(struct hal_uart_bus_host* host)
{
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_dev_hw_close(struct hal_uart_bus_host* host)
{
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_write(struct hal_uart_dev* dev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    (void)dev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_read(struct hal_uart_dev* dev, uint8_t* data, size_t len, uint32_t timeout_ms)
{
    (void)dev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_write_dma(struct hal_uart_dev* dev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    (void)dev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_dma_abort(struct hal_uart_dev* dev)
{
    (void)dev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_virtual_uart_irq_callback(void* arg, uint16_t irq_num)
{
    (void)arg;
    (void)irq_num;
    return VFS_ERR_NOTSUPP;
}
