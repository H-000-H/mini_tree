/**
 * SPDX-License-Identifier: Apache-2.0
 * @file w25qxx_drv.h
 */
#ifndef W25QXX_DRV_H
#define W25QXX_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define W25QXX_CMD_BASE COMPAT_MAGIC(W25QXX)
#define W25QXX_CMD_READ_JEDEC_ID (W25QXX_CMD_BASE+0x01)
#define W25QXX_CMD_COUNT 1
struct w25qxx_jedec { uint8_t id[3]; };
#ifdef __cplusplus
}
#endif
#endif
