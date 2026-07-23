/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "compiler_compat.h"
#include "status.h"
#include "hal_i2c.h"

COMPAT_WEAK int hal_i2c_bus_host_init(struct hal_i2c_bus_host* host, int hw_idx, const struct hal_i2c_bus_config* cfg)
{
    (void)host;
    (void)hw_idx;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_bus_host_deinit(struct hal_i2c_bus_host* host)
{
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_dev_hw_open(struct hal_i2c_dev* dev)
{
    (void)dev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_dev_hw_close(struct hal_i2c_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_dev_init(struct hal_i2c_dev* pdev, struct hal_i2c_bus_host* host, const struct hal_i2c_device_config* dev_cfg)
{
    (void)pdev;
    (void)host;
    (void)dev_cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_dev_deinit(struct hal_i2c_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_sync(struct hal_i2c_dev* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_write(struct hal_i2c_dev* pdev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)tx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_read(struct hal_i2c_dev* pdev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_dma_write(struct hal_i2c_dev* pdev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)tx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_dma_read(struct hal_i2c_dev* pdev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_i2c_dma_write_then_read(struct hal_i2c_dev* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}
