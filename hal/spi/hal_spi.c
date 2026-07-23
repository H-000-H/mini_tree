/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "compiler_compat.h"
#include "status.h"
#include "hal_spi.h"

COMPAT_WEAK int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,const struct hal_spi_bus_config* cfg)
{
    (void)host;
    (void)hw_idx;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host)
{
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_dev_init(struct hal_spi_dev* dev,struct hal_spi_bus_host* host,const struct hal_spi_device_config* dev_cfg)
{
    (void)dev;
    (void)host;
    (void)dev_cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_dev_hw_open(struct hal_spi_dev* dev)
{
    (void)dev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_dev_hw_close(struct hal_spi_dev* dev)
{
    (void)dev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms, uint32_t xfer_mode)
{
    (void)dev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    (void)xfer_mode;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_transfer_async(struct hal_spi_dev* dev,const uint8_t* tx, uint8_t* rx,size_t len, hal_spi_callback_t cb,void* userdata)
{
    (void)dev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)cb;
    (void)userdata;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms)
{
    (void)dev;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,size_t* trans_len, uint32_t timeout_ms)
{
    (void)dev;
    (void)rx_data;
    (void)rx_cap;
    (void)trans_len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,size_t len, uint32_t timeout_ms)
{
    (void)dev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,uint32_t timeout_ms)
{
    (void)dev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_virtual_spi_irq_callback(void* arg, uint16_t irq_num)
{
    (void)arg;
    (void)irq_num;
    return VFS_ERR_NOTSUPP;
}
