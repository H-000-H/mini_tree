/* SPDX-License-Identifier: Apache-2.0 */
#include "can_hook.h"

#include "device.h"

COMPAT_WEAK int can_hook_on_open(struct device* pdev)
{
    COMPAT_IGNORE_RESULT(pdev);
    return VFS_OK;
}

COMPAT_WEAK int can_hook_on_close(struct device* pdev)
{
    COMPAT_IGNORE_RESULT(pdev);
    return VFS_OK;
}

COMPAT_WEAK int can_hook_pre_tx(struct device* pdev, struct can_frame* frame)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(frame);
    return VFS_OK;
}

COMPAT_WEAK int can_hook_post_tx(struct device* pdev, const struct can_frame* frame, int tx_ret)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(frame);
    return tx_ret;
}

COMPAT_WEAK int can_hook_filter_match(struct device* pdev, const struct can_frame* frame)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(frame);
    return VFS_OK;
}

COMPAT_WEAK int can_hook_on_rx(struct device* pdev, struct can_frame* frame)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(frame);
    return VFS_OK;
}

COMPAT_WEAK int can_hook_on_err(struct device* pdev, int err)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(err);
    return VFS_OK;
}
