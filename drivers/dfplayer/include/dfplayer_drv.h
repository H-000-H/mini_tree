/**
 * SPDX-License-Identifier: Apache-2.0
 * @file dfplayer_drv.h
 */
#ifndef DFPLAYER_DRV_H
#define DFPLAYER_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DFPLAYER_CMD_BASE COMPAT_MAGIC(DFPLAYER)
#define DFPLAYER_CMD_PLAY (DFPLAYER_CMD_BASE + 0x01)
#define DFPLAYER_CMD_VOLUME (DFPLAYER_CMD_BASE + 0x02)
#define DFPLAYER_CMD_COUNT 2
struct dfplayer_track { uint16_t track; };
#ifdef __cplusplus
}
#endif
#endif
