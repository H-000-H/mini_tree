/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_gpio.c
 *@brief hal gpio 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_gpio.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK int hal_gpio_fast_set_level(hal_gpio_dev_t* pdev, int level)
{
    (void)pdev;
    (void)level;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_fast_get_level(hal_gpio_dev_t* pdev, int* level_out)
{
    (void)pdev;
    (void)level_out;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_fast_toggle(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_init(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_deinit(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_mode(hal_gpio_dev_t* pdev, uint32_t mode)
{
    (void)pdev;
    (void)mode;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_mode(hal_gpio_dev_t* pdev, uint32_t* mode)
{
    (void)pdev;
    (void)mode;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_pull(hal_gpio_dev_t* pdev, uint32_t pull)
{
    (void)pdev;
    (void)pull;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_pull(hal_gpio_dev_t* pdev, uint32_t* pull)
{
    (void)pdev;
    (void)pull;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_speed(hal_gpio_dev_t* pdev, uint32_t speed)
{
    (void)pdev;
    (void)speed;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_speed(hal_gpio_dev_t* pdev, uint32_t* speed)
{
    (void)pdev;
    (void)speed;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_output_type(hal_gpio_dev_t* pdev, uint32_t output_type)
{
    (void)pdev;
    (void)output_type;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_output_type(hal_gpio_dev_t* pdev, uint32_t* output_type)
{
    (void)pdev;
    (void)output_type;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_af(hal_gpio_dev_t* pdev, uint32_t af)
{
    (void)pdev;
    (void)af;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_af(hal_gpio_dev_t* pdev, uint32_t* af)
{
    (void)pdev;
    (void)af;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_af_mode(hal_gpio_dev_t* pdev, uint32_t af)
{
    (void)pdev;
    (void)af;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_irq_enable(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_irq_disable(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
