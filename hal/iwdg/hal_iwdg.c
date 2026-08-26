/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_iwdg.c
 *@brief hal iwdg 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_iwdg.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
MINI_WEAK int hal_iwdg_init(struct hal_iwdg_dev* pdev, const struct hal_iwdg_config* cfg)
{
    (void)pdev;
    (void)cfg;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_iwdg_start(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_iwdg_feed(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_iwdg_set_timeout_ms(struct hal_iwdg_dev* pdev, uint32_t timeout_ms)
{
    (void)pdev;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_iwdg_set_long_timeout(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

MINI_WEAK int hal_iwdg_restore_timeout(struct hal_iwdg_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
