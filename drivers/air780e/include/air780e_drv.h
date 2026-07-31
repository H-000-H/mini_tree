/**
 * SPDX-License-Identifier: Apache-2.0
 * @file air780e_drv.h
 */
#ifndef AIR780E_DRV_H
#define AIR780E_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AIR780E_CMD_BASE COMPAT_MAGIC(AIR780E)
#define AIR780E_CMD_AT_SEND (AIR780E_CMD_BASE+0x01)
#define AIR780E_CMD_AT_RECV (AIR780E_CMD_BASE+0x02)
#define AIR780E_CMD_COUNT 2
struct air780e_at { const uint8_t* tx; size_t tx_len; uint8_t* rx; size_t rx_cap; size_t rx_len; };
#ifdef __cplusplus
}
#endif
#endif
