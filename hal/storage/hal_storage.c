/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_storage.c
 *@brief hal storage 实现
 *@author H-000-H

 */

#include "hal_storage.h"

#include "compiler_compat.h"
#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK bool hal_storage_init(void) { return false; }
COMPAT_WEAK bool hal_storage_read_flag(uint8_t* flag)
{
    (void)flag;
    return false;
}
COMPAT_WEAK bool hal_storage_write_flag(uint8_t flag)
{
    (void)flag;
    return false;
}
COMPAT_WEAK bool hal_storage_read_blob(uint8_t slot, uint8_t* buf, size_t* len)
{
    (void)slot;
    (void)buf;
    (void)len;
    return false;
}
COMPAT_WEAK bool hal_storage_write_blob(uint8_t slot, const uint8_t* buf, size_t len)
{
    (void)slot;
    (void)buf;
    (void)len;
    return false;
}
COMPAT_WEAK bool hal_storage_erase_all(void) { return false; }
#endif /* ESP_PLATFORM */
