/**
 * SPDX-License-Identifier: Apache-2.0
 * @file pn532_drv.h
 */
#ifndef PN532_DRV_H
#define PN532_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PN532_CMD_BASE COMPAT_MAGIC(PN532)
#define PN532_CMD_GET_FIRMWARE (PN532_CMD_BASE + 0x01)
#define PN532_CMD_COUNT 1
struct pn532_fw { uint8_t ic; uint8_t ver; uint8_t rev; uint8_t support; };
#ifdef __cplusplus
}
#endif
#endif
