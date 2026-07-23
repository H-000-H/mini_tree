/* SPDX-License-Identifier: Apache-2.0 */
#include "compiler_compat.h"
#include "hal_sdio.h"
COMPAT_WEAK void hal_sdio_init_struct(struct hal_sdio* sdio) { (void)sdio; }
COMPAT_WEAK void hal_sdio_force_stop(void) {}
