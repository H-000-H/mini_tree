/* SPDX-License-Identifier: Apache-2.0 */
#include "compiler_compat.h"
#include "hal_amp.h"
COMPAT_WEAK void hal_cpu_emergency_stop_all_cores(void) {}
COMPAT_WEAK void hal_cpu_secondary_startup(void) {}
COMPAT_WEAK void hal_cpu_baremetal_entry(void) {}
COMPAT_WEAK int  hal_cpu_get_id(void) { return 0; }
