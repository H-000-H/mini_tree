/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_i2s.c
 *@brief hal i2s 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_i2s.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
MINI_WEAK int hal_i2s_bus_host_init(struct hal_i2s_bus_host* host, int hw_idx,
                                      const struct hal_i2s_bus_config* cfg)
{
    (void)host;
    (void)hw_idx;
    (void)cfg;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_bus_host_deinit(struct hal_i2s_bus_host* host)
{
    (void)host;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dev_init(struct hal_i2s_dev* pdev, struct hal_i2s_bus_host* host,
                                 const struct hal_i2s_device_config* cfg)
{
    (void)pdev;
    (void)host;
    (void)cfg;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dev_deinit(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dev_hw_open(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dev_hw_close(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_sync(struct hal_i2s_dev* pdev, const uint16_t* tx, uint16_t* rx,
                             size_t samples, uint32_t timeout_ms, uint32_t xfer_mode)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)samples;
    (void)timeout_ms;
    (void)xfer_mode;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_set_dma_irq_mode(struct hal_i2s_dev* pdev, uint32_t irq_mode)
{
    (void)pdev;
    (void)irq_mode;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_get_dma_irq_mode(struct hal_i2s_dev* pdev, uint32_t* irq_mode)
{
    (void)pdev;
    (void)irq_mode;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dma_circ_start(struct hal_i2s_dev* pdev, int tx_enable, int rx_enable)
{
    (void)pdev;
    (void)tx_enable;
    (void)rx_enable;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dma_circ_stop(struct hal_i2s_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dma_circ_write(struct hal_i2s_dev* pdev, const uint16_t* data,
                                       uint32_t samples)
{
    (void)pdev;
    (void)data;
    (void)samples;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_dma_circ_read(struct hal_i2s_dev* pdev, uint16_t* data, uint32_t samples)
{
    (void)pdev;
    (void)data;
    (void)samples;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_transfer_async(struct hal_i2s_dev* pdev, const uint16_t* tx, uint16_t* rx,
                                       size_t samples, hal_i2s_callback_t cb, void* userdata)
{
    (void)pdev;
    (void)tx;
    (void)rx;
    (void)samples;
    (void)cb;
    (void)userdata;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_i2s_transfer_poll(struct hal_i2s_dev* pdev, uint32_t timeout_ms)
{
    (void)pdev;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
