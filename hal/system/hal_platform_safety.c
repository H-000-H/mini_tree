/* SPDX-License-Identifier: Apache-2.0 */
#include "hal_platform_safety.h"

#include "compiler_compat.h"
#include "status.h"
COMPAT_WEAK int hal_platform_critical_hardware_lock(void) { return VFS_OK; }
COMPAT_WEAK int hal_pwm_force_stop_all(void) { return VFS_OK; }
COMPAT_WEAK void hal_platform_nmi_emergency_stamp(void) {}
