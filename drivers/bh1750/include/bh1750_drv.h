/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bh1750_drv.h
 */
#ifndef BH1750_DRV_H
#define BH1750_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BH1750_CMD_BASE      COMPAT_MAGIC(BH1750)
#define BH1750_CMD_READ_LUX  (BH1750_CMD_BASE + 0x01)
#define BH1750_CMD_COUNT     1

#ifdef __cplusplus
}
#endif

#endif /* BH1750_DRV_H */
