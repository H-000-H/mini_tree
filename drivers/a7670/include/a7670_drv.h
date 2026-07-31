/**
 * SPDX-License-Identifier: Apache-2.0
 * @file a7670_drv.h
 */
#ifndef A7670_DRV_H
#define A7670_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define A7670_CMD_BASE COMPAT_MAGIC(A7670)
#define A7670_CMD_AT_SEND (A7670_CMD_BASE + 0x01)
#define A7670_CMD_AT_RECV (A7670_CMD_BASE + 0x02)
#define A7670_CMD_COUNT 2
struct a7670_at_buf { const uint8_t* tx; size_t tx_len; uint8_t* rx; size_t rx_cap; size_t rx_len; };
#ifdef __cplusplus
}
#endif
#endif
