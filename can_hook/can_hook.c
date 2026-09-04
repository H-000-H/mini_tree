/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file can_hook.c
 *@brief can hook 实现
 *@author H-000-H

 */

#include "can_hook.h"

#include "device.h"

MINI_WEAK int can_hook_on_open(struct device* pdev)
{
    MINI_IGNORE_RESULT(pdev);
    return MINI_OK;
}

MINI_WEAK int can_hook_on_close(struct device* pdev)
{
    MINI_IGNORE_RESULT(pdev);
    return MINI_OK;
}

MINI_WEAK int can_hook_pre_tx(struct device* pdev, struct can_frame* frame)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(frame);
    return MINI_OK;
}

MINI_WEAK int can_hook_post_tx(struct device* pdev, const struct can_frame* frame, int tx_ret)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(frame);
    return tx_ret;
}

MINI_WEAK int can_hook_filter_match(struct device* pdev, const struct can_frame* frame)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(frame);
    return MINI_OK;
}

MINI_WEAK int can_hook_on_rx(struct device* pdev, struct can_frame* frame)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(frame);
    return MINI_OK;
}

MINI_WEAK int can_hook_on_err(struct device* pdev, int err)
{
    MINI_IGNORE_RESULT(pdev);
    MINI_IGNORE_RESULT(err);
    return MINI_OK;
}
