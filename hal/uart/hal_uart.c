/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_uart.c
 *@brief hal uart 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_uart.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK int hal_uart_dev_init(struct hal_uart_bus_host* host, const struct hal_uart_config* cfg)
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

COMPAT_WEAK int hal_uart_write(struct hal_uart_dev* pdev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_read(struct hal_uart_dev* pdev, uint8_t* data, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_write_dma(struct hal_uart_dev* pdev, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    (void)pdev;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_uart_dma_abort(struct hal_uart_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
