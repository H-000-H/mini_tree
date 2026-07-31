/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sx1278_drv.h
 */
#ifndef SX1278_DRV_H
#define SX1278_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SX1278_CMD_BASE COMPAT_MAGIC(SX1278)
#define SX1278_CMD_RESET (SX1278_CMD_BASE+0x01)
#define SX1278_CMD_SET_FREQ (SX1278_CMD_BASE+0x02)
#define SX1278_CMD_SEND (SX1278_CMD_BASE+0x03)
#define SX1278_CMD_RECV (SX1278_CMD_BASE+0x04)
#define SX1278_CMD_COUNT 4
/** SEND: data=TX；RECV: data=RX 缓冲（可写） */
struct sx1278_payload
{
    uint8_t* data;
    size_t   len;
};
#ifdef __cplusplus
}
#endif
#endif
