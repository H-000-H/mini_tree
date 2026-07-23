/* SPDX-License-Identifier: Apache-2.0 */
#include "compiler_compat.h"
#include "hal_storage.h"
COMPAT_WEAK bool hal_storage_init(void) { return false; }
COMPAT_WEAK bool hal_storage_read_flag(uint8_t* flag) { (void)flag; return false; }
COMPAT_WEAK bool hal_storage_write_flag(uint8_t flag) { (void)flag; return false; }
COMPAT_WEAK bool hal_storage_read_blob(uint8_t slot, uint8_t* buf, size_t* len) { (void)slot;(void)buf;(void)len; return false; }
COMPAT_WEAK bool hal_storage_write_blob(uint8_t slot, const uint8_t* buf, size_t len) { (void)slot;(void)buf;(void)len; return false; }
COMPAT_WEAK bool hal_storage_erase_all(void) { return false; }
