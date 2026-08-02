/* SPDX-License-Identifier: Apache-2.0 */
#include "hal_sdio.h"

#include "compiler_compat.h"
COMPAT_WEAK void hal_sdio_init_struct(struct hal_sdio* sdio) { (void)sdio; }
COMPAT_WEAK void hal_sdio_force_stop(void) {}
