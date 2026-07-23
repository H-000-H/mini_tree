/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "compiler_compat.h"
#include "status.h"
#include "hal_adc.h"

COMPAT_WEAK int hal_adc_device_init(hal_adc_device* pdev, hal_adc_platform_unique_config* unique_cfg, hal_adc_host_config* host)
{
    (void)pdev;
    (void)unique_cfg;
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_device_deinit(hal_adc_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_init(hal_adc_device *pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_deinit_all_adcx(hal_adc_device *pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_deinit_adcx_channel(hal_adc_device *pdev, uint32_t channel_id)
{
    (void)pdev;
    (void)channel_id;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_start(hal_adc_device *pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_stop(hal_adc_device *pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_read_value(hal_adc_device *pdev, uint32_t channel_num, uint16_t *out_val)
{
    (void)pdev;
    (void)channel_num;
    (void)out_val;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_poll_for_conversion(hal_adc_device *pdev, uint32_t *out_status)
{
    (void)pdev;
    (void)out_status;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_get_channel_count(hal_adc_device *pdev, uint32_t *count)
{
    (void)pdev;
    (void)count;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_get_channel_id(hal_adc_device *pdev, int index, uint32_t *channel_id)
{
    (void)pdev;
    (void)index;
    (void)channel_id;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_get_channel_sample_time(hal_adc_device *pdev, int index, uint32_t *sample_time)
{
    (void)pdev;
    (void)index;
    (void)sample_time;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_dma_start(hal_adc_device *pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_dma_it_start(hal_adc_device *pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_dma_it_read_value(hal_adc_device *pdev, uint16_t *out_val)
{
    (void)pdev;
    (void)out_val;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_adc_dma_read_value(hal_adc_device *pdev, uint16_t *out_val)
{
    (void)pdev;
    (void)out_val;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_virtual_adc_irq_callback(void* arg, uint16_t irq_num)
{
    (void)arg;
    (void)irq_num;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK void hal_adc_dma_bottom_half_handler(void* arg)
{
    (void)arg;
}
