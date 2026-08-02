/**
 * SPDX-License-Identifier: Apache-2.0
 * @file dfplayer_drv.h
 * @brief DFPlayer MP3 模块驱动 ioctl 命令与曲目参数
 *
 * 挂在 UART 总线 client 下的 VFS 设备驱动；
 * 业务经 device_open/ioctl/close 访问。
 */
#ifndef DFPLAYER_DRV_H
#define DFPLAYER_DRV_H
#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/** ioctl 命令基址（COMPAT_MAGIC 魔数，防跨模块冲突） */
#define DFPLAYER_CMD_BASE COMPAT_MAGIC(DFPLAYER)
/** 播放曲目（arg: struct dfplayer_track*） */
#define DFPLAYER_CMD_PLAY (DFPLAYER_CMD_BASE + 0x01)
/** 设置音量（arg: int*，0..30） */
#define DFPLAYER_CMD_VOLUME (DFPLAYER_CMD_BASE + 0x02)
/** 命令总数 */
#define DFPLAYER_CMD_COUNT 2

    /** @brief 曲目号参数 */
    struct dfplayer_track
    {
        uint16_t track; /**< 曲目号（1..2999） */
    };
#ifdef __cplusplus
}
#endif
#endif /* DFPLAYER_DRV_H */
