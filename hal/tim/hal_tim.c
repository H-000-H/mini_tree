/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_tim.c
 *@brief hal tim 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_tim.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK int hal_tim_device_init(hal_tim_device* pdev, hal_tim_platform_unique_config* unique, hal_tim_host_config* host)
{
    (void)pdev;
    (void)unique;
    (void)host;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_device_deinit(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_open(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_close(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_pwm_update(hal_tim_device* pdev, uint32_t channel, uint32_t frequency, uint32_t duty)
{
    (void)pdev;
    (void)channel;
    (void)frequency;
    (void)duty;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_interrupt_config(hal_tim_device* pdev, uint32_t interrupt_config)
{
    (void)pdev;
    (void)interrupt_config;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_counter(const hal_tim_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_capture_value(const hal_tim_device* pdev, uint32_t channel, uint32_t* value)
{
    (void)pdev;
    (void)channel;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_encoder_value(const hal_tim_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_hall_value(const hal_tim_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_force_stop(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_encoder_start(hal_tim_device* pdev, uint32_t encoder_mode)
{
    (void)pdev;
    (void)encoder_mode;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_hall_start(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_set_counter(hal_tim_device* pdev, uint32_t value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_set_autoreload(hal_tim_device* pdev, uint32_t value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_autoreload(const hal_tim_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_set_prescaler(hal_tim_device* pdev, uint32_t value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_prescaler(const hal_tim_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_set_clock_division(hal_tim_device* pdev, uint32_t value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_clock_division(const hal_tim_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_set_counter_mode(hal_tim_device* pdev, uint32_t value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_get_counter_mode(const hal_tim_device* pdev, uint32_t* value)
{
    (void)pdev;
    (void)value;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_enable_arr_preload(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_disable_arr_preload(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_base_start(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_base_stop(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_tim_clear_update_flag(hal_tim_device* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
