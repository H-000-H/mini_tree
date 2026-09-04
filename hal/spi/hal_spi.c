/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_spi.c
 *@brief hal spi 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_spi.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
MINI_WEAK int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx, const struct hal_spi_bus_config* cfg)
{
    (void)host;
    (void)hw_idx;
    (void)cfg;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host)
{
    (void)host;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_dev_init(struct hal_spi_dev* pdev, struct hal_spi_bus_host* host, const struct hal_spi_device_config* dev_cfg)
{
    (void)pdev;
    (void)host;
    (void)dev_cfg;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_dev_hw_open(struct hal_spi_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_dev_hw_close(struct hal_spi_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_sync(struct hal_spi_dev* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms, uint32_t xfer_mode)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    (void)xfer_mode;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_transfer_async(struct hal_spi_dev* pdev, const uint8_t* tx, uint8_t* rx, size_t len, hal_spi_callback_t cb, void* userdata)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)cb;
    (void)userdata;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_transfer_poll(struct hal_spi_dev* pdev, uint32_t timeout_ms)
{
    (void)pdev;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_get_trans_result(struct hal_spi_dev* pdev, uint8_t* rx_data, size_t rx_cap, size_t* trans_len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)rx_data;
    (void)rx_cap;
    (void)trans_len;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_slave_sync(struct hal_spi_dev* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)len;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_spi_slave_queue_tx(struct hal_spi_dev* pdev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
