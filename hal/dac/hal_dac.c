/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "hal_dac.h"

#include "compiler_compat.h"
#include "status.h"

COMPAT_WEAK int hal_dac_device_init(hal_dac_device* pdev, hal_dac_host_config* host_cfg,
                                    hal_dac_platform_unique_config* unique_cfg)
{
    (void)pdev;
    (void)host_cfg;
    (void)unique_cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_close(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_init(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_start(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_dma_pause(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_base_pause(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_pause(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_resume(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_set_value(hal_dac_device* pdev, uint32_t value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_get_value(hal_dac_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_get_dma_progress(hal_dac_device* pdev, uint32_t* remaining)
{
    (void)pdev;
    (void)remaining;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_write_dma_buffer(hal_dac_device* pdev, const uint16_t* data, uint32_t len)
{
    (void)pdev;
    (void)data;
    (void)len;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_stop_dma(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_base_stop(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_dac_force_stop(hal_dac_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_virtual_dac_irq_callback(void* arg, uint16_t irq_num)
{
    (void)arg;
    (void)irq_num;
    return VFS_ERR_NOTSUPP;
}
