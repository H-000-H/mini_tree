/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_flash.c
 *@brief hal flash 实现
 *@author H-000-H

 */

#include "hal_flash.h"

#include "compiler_compat.h"
#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK int hal_flash_read(uint32_t addr, uint8_t* buf, size_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return MINI_ERR_NOTSUPP;
}
COMPAT_WEAK uint32_t hal_flash_get_app_addr(void) { return 0; }
COMPAT_WEAK uint32_t hal_flash_get_app_size(void) { return 0; }
#endif /* ESP_PLATFORM */
