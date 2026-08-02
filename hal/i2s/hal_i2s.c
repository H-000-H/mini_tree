/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "hal_i2s.h"

#include "compiler_compat.h"
#include "status.h"

COMPAT_WEAK int hal_i2s_bus_host_init(struct hal_i2s_bus_host* host, int hw_idx,
                                      const struct hal_i2s_bus_config* cfg)
{
    (void)host;
    (void)hw_idx;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_bus_host_deinit(struct hal_i2s_bus_host* host)
{
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dev_init(struct hal_i2s_dev* pdev, struct hal_i2s_bus_host* host,
                                 const struct hal_i2s_device_config* cfg)
{
    (void)pdev;
    (void)host;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dev_deinit(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dev_hw_open(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dev_hw_close(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_sync(struct hal_i2s_dev* pdev, const uint16_t* tx, uint16_t* rx,
                             size_t samples, uint32_t timeout_ms, uint32_t xfer_mode)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)samples;
    (void)timeout_ms;
    (void)xfer_mode;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_set_dma_irq_mode(struct hal_i2s_dev* pdev, uint32_t irq_mode)
{
    (void)pdev;
    (void)irq_mode;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_get_dma_irq_mode(struct hal_i2s_dev* pdev, uint32_t* irq_mode)
{
    (void)pdev;
    (void)irq_mode;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dma_circ_start(struct hal_i2s_dev* pdev, int tx_enable, int rx_enable)
{
    (void)pdev;
    (void)tx_enable;
    (void)rx_enable;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dma_circ_stop(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dma_circ_write(struct hal_i2s_dev* pdev, const uint16_t* data,
                                       uint32_t samples)
{
    (void)pdev;
    (void)data;
    (void)samples;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_dma_circ_read(struct hal_i2s_dev* pdev, uint16_t* data, uint32_t samples)
{
    (void)pdev;
    (void)data;
    (void)samples;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_transfer_async(struct hal_i2s_dev* pdev, const uint16_t* tx, uint16_t* rx,
                                       size_t samples, hal_i2s_callback_t cb, void* userdata)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)samples;
    (void)cb;
    (void)userdata;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2s_transfer_poll(struct hal_i2s_dev* pdev, uint32_t timeout_ms)
{
    (void)pdev;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_virtual_i2s_irq_callback(void* arg, uint16_t irq_num)
{
    (void)arg;
    (void)irq_num;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK void hal_i2s_dma_bottom_half_handler(void* arg) { (void)arg; }
