/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_can.c
 *@brief hal can 实现
 *@author H-000-H
 *@details
 *   Weak empty HAL stub — board overrides.
 */

#include "hal_can.h"

#include "compiler_compat.h"
#include "status.h"

#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK int hal_can_bus_host_init(struct hal_can_bus_host* host, int hw_idx,
                                      const struct hal_can_bus_config* cfg)
{
    (void)host;
    (void)hw_idx;
    (void)cfg;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_bus_host_deinit(struct hal_can_bus_host* host)
{
    (void)host;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_hw_open(struct hal_can_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_hw_close(struct hal_can_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_init(struct hal_can_dev* pdev, struct hal_can_bus_host* host)
{
    (void)pdev;
    (void)host;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_dev_deinit(struct hal_can_dev* pdev)
{
    (void)pdev;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_transmit(struct hal_can_dev* pdev, const struct can_frame* frame,
                                 uint32_t timeout_ms)
{
    (void)pdev;
    (void)frame;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_receive(struct hal_can_dev* pdev, struct can_frame* frame, uint32_t fifo,
                                uint32_t timeout_ms)
{
    (void)pdev;
    (void)frame;
    (void)fifo;
    (void)timeout_ms;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_filter_config(struct hal_can_bus_host* host,
                                      const struct hal_can_filter_config* filter)
{
    (void)host;
    (void)filter;
    return MINI_ERR_NOTSUPP;
}

COMPAT_WEAK int hal_can_get_state(struct hal_can_bus_host* host, uint32_t* out_state)
{
    (void)host;
    (void)out_state;
    return MINI_ERR_NOTSUPP;
}
#endif /* ESP_PLATFORM */
