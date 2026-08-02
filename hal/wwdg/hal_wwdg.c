/* SPDX-License-Identifier: Apache-2.0 */
/* Weak empty HAL stub — board overrides. */
#include "hal_wwdg.h"

#include "compiler_compat.h"
#include "status.h"

COMPAT_WEAK int hal_wwdg_init(struct hal_wwdg_dev* pdev, const struct hal_wwdg_config* cfg)
{
    (void)pdev;
    (void)cfg;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_wwdg_start(struct hal_wwdg_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_wwdg_feed(struct hal_wwdg_dev* pdev)
{
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}
