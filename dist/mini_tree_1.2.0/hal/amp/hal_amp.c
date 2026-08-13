/* SPDX-License-Identifier: Apache-2.0 */
#include "hal_amp.h"

#include "compiler_compat.h"
#if defined(ESP_PLATFORM)
/* ESP-IDF 构建: 本文件编译为空 — hal_* 由板级组件 (如 hal_esp32s3) 提供 strong
 * 实现, 缺失直接链接报错, 杜绝静默 -ENOSYS。非 ESP 构建保留 weak stub 兜底。 */
#else
COMPAT_WEAK void hal_cpu_emergency_stop_all_cores(void) {}
COMPAT_WEAK void hal_cpu_secondary_startup(void) {}
COMPAT_WEAK void hal_cpu_baremetal_entry(void) {}
COMPAT_WEAK int hal_cpu_get_id(void) { return 0; }
#endif /* ESP_PLATFORM */
