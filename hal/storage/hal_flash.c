/* SPDX-License-Identifier: Apache-2.0 */
#include "hal_flash.h"

#include "compiler_compat.h"
COMPAT_WEAK bool hal_flash_read(uint32_t addr, uint8_t* buf, size_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return false;
}
COMPAT_WEAK uint32_t hal_flash_get_app_addr(void) { return 0; }
COMPAT_WEAK uint32_t hal_flash_get_app_size(void) { return 0; }
