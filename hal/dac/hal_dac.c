/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_dac.c
 *@brief hal dac 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_dac.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
MINI_WEAK int hal_dac_device_init(hal_dac_device* pdev, hal_dac_host_config* host_cfg,
                                    hal_dac_platform_unique_config* unique_cfg)
{
    (void)pdev;
    (void)host_cfg;
    (void)unique_cfg;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_close(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_init(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_start(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_dma_pause(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_base_pause(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_pause(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_resume(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_set_value(hal_dac_device* pdev, uint32_t value)
{
    (void)pdev;
    (void)value;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_get_value(hal_dac_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_get_dma_progress(hal_dac_device* pdev, uint32_t* remaining)
{
    (void)pdev;
    (void)remaining;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_write_dma_buffer(hal_dac_device* pdev, const uint16_t* data, uint32_t len)
{
    (void)pdev;
    (void)data;
    (void)len;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_stop_dma(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_base_stop(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_dac_force_stop(hal_dac_device* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
