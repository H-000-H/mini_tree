/* SPDX-License-Identifier: Apache-2.0 */
#include "hal_sdio.h"

#include "compiler_compat.h"
#include "status.h"
COMPAT_WEAK int hal_sdio_init_struct(struct hal_sdio* sdio)
{
    COMPAT_UNUSED_PARAM(sdio);
    return VFS_OK;
}
COMPAT_WEAK int hal_sdio_force_stop(void) { return VFS_OK; }
