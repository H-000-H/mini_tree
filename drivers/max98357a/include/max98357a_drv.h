/**
 * SPDX-License-Identifier: Apache-2.0
 * @file max98357a_drv.h
 * @brief MAX98357A 功放 SDN 控制 — 应用层接口
 * @note open → ioctl(SET_ENABLE) → close；PCM 走 I2S 总线，本驱动仅控 SDN GPIO
 */
#ifndef MAX98357A_DRV_H
#define MAX98357A_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX98357A_CMD_BASE         COMPAT_MAGIC(MAX98357A)
#define MAX98357A_CMD_SET_ENABLE   (MAX98357A_CMD_BASE + 0x01)  /**< arg: int* 0=关 1=开 */
#define MAX98357A_CMD_COUNT        1

#ifdef __cplusplus
}
#endif

#endif /* MAX98357A_DRV_H */
