/**
 * @file hal_sdio.c
 * @author H-000-H
 * @brief hal sdio 实现
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include "hal_sdio.h"

#include "compiler_compat.h"
#include "status.h"
#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 MINI_ERR_NOTSUPP。非 ESP 构建保留 weak stub 兜底。 */
#else
MINI_WEAK mt_err_t hal_sdio_init_struct(struct hal_sdio* sdio)
{
    MINI_UNUSED_PARAM(sdio);
    return MINI_OK;
}
MINI_WEAK mt_err_t hal_sdio_force_stop(void) { return MINI_OK; }
#endif /* ESP_PLATFORM */
