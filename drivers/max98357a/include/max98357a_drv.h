/**
 * @file max98357a_drv.h
 * @author H-000-H
 * @brief MAX98357A 功放 SDN 控制 — 应用层接口
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef MAX98357A_DRV_H
#define MAX98357A_DRV_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** ioctl 命令基址（MINI_MAGIC 魔数，防跨模块冲突） */
#define MAX98357A_CMD_BASE MINI_MAGIC(MAX98357A)
/** 功放使能控制（arg: int* 0=关 1=开） */
#define MAX98357A_CMD_SET_ENABLE (MAX98357A_CMD_BASE + 0x01)
/** 命令总数 */
#define MAX98357A_CMD_COUNT 1

#ifdef __cplusplus
}
#endif

#endif /* MAX98357A_DRV_H */
