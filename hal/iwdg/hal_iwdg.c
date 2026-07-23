/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "compiler_compat.h"
#include "status.h"
#include "hal_iwdg.h"

COMPAT_WEAK int hal_iwdg_init(struct hal_iwdg_dev* pdev, const struct hal_iwdg_config* cfg)
{
    (void)pdev;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_iwdg_start(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_iwdg_feed(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_iwdg_set_timeout_ms(struct hal_iwdg_dev* pdev, uint32_t timeout_ms)
{
    (void)pdev;
    (void)timeout_ms;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_iwdg_set_long_timeout(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_iwdg_restore_timeout(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}
