/**
 * SPDX-License-Identifier: Apache-2.0
 * @file dfplayer_regs.h
 */
#ifndef DFPLAYER_REGS_H
#define DFPLAYER_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define DFPLAYER_FRAME_START    0x7EU
#define DFPLAYER_FRAME_VER      0xFFU
#define DFPLAYER_FRAME_LEN      0x06U
#define DFPLAYER_FRAME_FEEDBACK 0x00U
#define DFPLAYER_FRAME_END      0xEFU

#define DFPLAYER_OP_PLAY_TRACK  0x03U
#define DFPLAYER_OP_SET_VOL     0x06U

#ifdef __cplusplus
}
#endif

#endif /* DFPLAYER_REGS_H */
