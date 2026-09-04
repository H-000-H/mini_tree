/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_platform_safety.c
 *@brief hal platform safety 实现
 *@author H-000-H

 */

#include "hal_platform_safety.h"

#include "compiler_compat.h"
#include "status.h"
#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
MINI_WEAK int  hal_platform_critical_hardware_lock(void) { return MINI_OK; }
MINI_WEAK int  hal_pwm_force_stop_all(void) { return MINI_OK; }
MINI_WEAK void hal_platform_nmi_emergency_stamp(void) {}
#endif /* ESP_PLATFORM */
