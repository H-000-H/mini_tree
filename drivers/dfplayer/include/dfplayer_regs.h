/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file dfplayer_regs.h
 *@brief DFPlayer 串口帧格式 / 操作码常量
 *@author H-000-H

 */

#ifndef DFPLAYER_REGS_H
#define DFPLAYER_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** 帧起始字节 */
#define DFPLAYER_FRAME_START 0x7EU
/** 协议版本字节 */
#define DFPLAYER_FRAME_VER 0xFFU
/** 帧长度字节（不含校验） */
#define DFPLAYER_FRAME_LEN 0x06U
/** 无反馈标志 */
#define DFPLAYER_FRAME_FEEDBACK 0x00U
/** 帧结束字节 */
#define DFPLAYER_FRAME_END 0xEFU

/** 播放指定曲目操作码 */
#define DFPLAYER_OP_PLAY_TRACK 0x03U
/** 设置音量操作码 */
#define DFPLAYER_OP_SET_VOL 0x06U

#ifdef __cplusplus
}
#endif

#endif /* DFPLAYER_REGS_H */
