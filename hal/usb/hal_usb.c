/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_usb.c
 *@brief hal usb 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#define HAL_USB_IMPL
#include "hal_usb.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
MINI_WEAK int hal_usb_bus_host_init(struct hal_usb_bus_host* host,
                                      const struct hal_usb_bus_config* cfg)
{
    (void)host;
    (void)cfg;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_usb_bus_host_deinit(struct hal_usb_bus_host* host)
{
    (void)host;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_usb_irq_enable(const struct hal_usb_bus_host* host)
{
   MINI_UNUSED_PARAM(host);
    return MINI_OK;
}

MINI_WEAK int hal_usb_irq_disable(const struct hal_usb_bus_host* host)
{
   MINI_UNUSED_PARAM(host);
    return MINI_OK;
}

MINI_WEAK int hal_usb_resolve_xfer_mode(const struct hal_usb_bus_host* host, uint32_t xfer_mode)
{
    (void)host;
    (void)xfer_mode;
    return MINI_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
