/**
 * SPDX-License-Identifier: Apache-2.0
 * @file rc522_drv.h
 */
#ifndef RC522_DRV_H
#define RC522_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC522_CMD_BASE COMPAT_MAGIC(RC522)
#define RC522_CMD_INIT (RC522_CMD_BASE + 0x01)
#define RC522_CMD_READ_UID (RC522_CMD_BASE + 0x02)
#define RC522_CMD_COUNT 2
struct rc522_uid { uint8_t uid[10]; uint8_t len; };
#ifdef __cplusplus
}
#endif
#endif
