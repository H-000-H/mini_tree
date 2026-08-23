/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_rtc.c
 *@brief hal rtc 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_rtc.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK int hal_rtc_init(struct hal_rtc_dev* pdev, const struct hal_rtc_config* cfg)
{
    (void)pdev;
    (void)cfg;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_deinit(struct hal_rtc_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_open(struct hal_rtc_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_close(struct hal_rtc_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_set_time(struct hal_rtc_dev* pdev, const struct hal_rtc_time* time)
{
    (void)pdev;
    (void)time;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_get_time(struct hal_rtc_dev* pdev, struct hal_rtc_time* time)
{
    (void)pdev;
    (void)time;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_set_alarm(struct hal_rtc_dev* pdev, const struct hal_rtc_time* alarm,
                                  hal_rtc_alarm_cb_t cb, void* user)
{
    (void)pdev;
    (void)alarm;
    (void)cb;
    (void)user;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_cancel_alarm(struct hal_rtc_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_set_wakeup_timer(struct hal_rtc_dev* pdev, uint32_t seconds)
{
    (void)pdev;
    (void)seconds;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_cancel_wakeup_timer(struct hal_rtc_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_rtc_force_stop(void) { return MINI_OK; }
#endif /* ESP_PLATFORM */
