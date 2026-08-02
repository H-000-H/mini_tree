/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "hal_can.h"

#include "compiler_compat.h"
#include "status.h"

COMPAT_WEAK int hal_can_bus_host_init(struct hal_can_bus_host* host, int hw_idx,
                                      const struct hal_can_bus_config* cfg)
{
    (void)host;
    (void)hw_idx;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_bus_host_deinit(struct hal_can_bus_host* host)
{
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_hw_open(struct hal_can_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_hw_close(struct hal_can_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_init(struct hal_can_dev* pdev, struct hal_can_bus_host* host)
{
    (void)pdev;
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_deinit(struct hal_can_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_transmit(struct hal_can_dev* pdev, const struct can_frame* frame,
                                 uint32_t timeout_ms)
{
    (void)pdev;
    (void)frame;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_receive(struct hal_can_dev* pdev, struct can_frame* frame, uint32_t fifo,
                                uint32_t timeout_ms)
{
    (void)pdev;
    (void)frame;
    (void)fifo;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_filter_config(struct hal_can_bus_host* host,
                                      const struct hal_can_filter_config* filter)
{
    (void)host;
    (void)filter;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_get_state(struct hal_can_bus_host* host, uint32_t* out_state)
{
    (void)host;
    (void)out_state;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_virtual_can_irq_callback(void* arg, uint16_t irq_num)
{
    (void)arg;
    (void)irq_num;
    return VFS_ERR_NOTSUPP;
}
