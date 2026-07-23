/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "compiler_compat.h"
#include "status.h"
#include "hal_gpio.h"

COMPAT_WEAK int hal_gpio_fast_set_level(hal_gpio_dev_t* pdev, int level)
{
    (void)pdev;
    (void)level;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_fast_get_level(hal_gpio_dev_t* pdev, int *level_out)
{
    (void)pdev;
    (void)level_out;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_fast_toggle(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_init(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_deinit(hal_gpio_dev_t* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_mode(hal_gpio_dev_t* pdev, uint32_t mode)
{
    (void)pdev;
    (void)mode;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_mode(hal_gpio_dev_t* pdev, uint32_t *mode)
{
    (void)pdev;
    (void)mode;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_pull(hal_gpio_dev_t* pdev, uint32_t pull)
{
    (void)pdev;
    (void)pull;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_pull(hal_gpio_dev_t* pdev, uint32_t *pull)
{
    (void)pdev;
    (void)pull;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_speed(hal_gpio_dev_t* pdev, uint32_t speed)
{
    (void)pdev;
    (void)speed;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_speed(hal_gpio_dev_t* pdev, uint32_t *speed)
{
    (void)pdev;
    (void)speed;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_output_type(hal_gpio_dev_t* pdev, uint32_t output_type)
{
    (void)pdev;
    (void)output_type;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_output_type(hal_gpio_dev_t* pdev, uint32_t *output_type)
{
    (void)pdev;
    (void)output_type;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_af(hal_gpio_dev_t* pdev, uint32_t af)
{
    (void)pdev;
    (void)af;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_get_af(hal_gpio_dev_t* pdev, uint32_t *af)
{
    (void)pdev;
    (void)af;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_gpio_set_af_mode(hal_gpio_dev_t* pdev, uint32_t af)
{
    (void)pdev;
    (void)af;
    return VFS_ERR_NOTSUPP;
}
