/* SPDX-License-Identifier: Apache-2.0 */
#include "hal_platform_safety.h"

#include "compiler_compat.h"
COMPAT_WEAK void hal_platform_critical_hardware_lock(void) {}
COMPAT_WEAK void hal_pwm_force_stop_all(void) {}
COMPAT_WEAK void hal_platform_nmi_emergency_stamp(void) {}
