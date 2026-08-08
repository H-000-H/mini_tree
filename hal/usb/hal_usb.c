/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#define HAL_USB_IMPL
#include "hal_usb.h"

#include "compiler_compat.h"
#include "status.h"

COMPAT_WEAK int hal_usb_bus_host_init(struct hal_usb_bus_host* host,
                                      const struct hal_usb_bus_config* cfg)
{
    (void)host;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_usb_bus_host_deinit(struct hal_usb_bus_host* host)
{
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_usb_irq_enable(const struct hal_usb_bus_host* host)
{
    COMPAT_UNUSED_PARAM(host);
    return VFS_OK;
}

COMPAT_WEAK int hal_usb_irq_disable(const struct hal_usb_bus_host* host)
{
    COMPAT_UNUSED_PARAM(host);
    return VFS_OK;
}

COMPAT_WEAK int hal_usb_resolve_xfer_mode(const struct hal_usb_bus_host* host, uint32_t xfer_mode)
{
    (void)host;
    (void)xfer_mode;
    return VFS_ERR_NOTSUPP;
}
