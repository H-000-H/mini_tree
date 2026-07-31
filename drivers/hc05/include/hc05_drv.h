/**
 * SPDX-License-Identifier: Apache-2.0
 * @file hc05_drv.h
 */
#ifndef HC05_DRV_H
#define HC05_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HC05_CMD_BASE COMPAT_MAGIC(HC05)
#define HC05_CMD_AT_SEND (HC05_CMD_BASE+0x01)
#define HC05_CMD_COUNT 1
struct hc05_at { const uint8_t* tx; size_t tx_len; };
#ifdef __cplusplus
}
#endif
#endif
