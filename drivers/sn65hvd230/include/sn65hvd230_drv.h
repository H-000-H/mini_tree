/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sn65hvd230_drv.h
 */
#ifndef SN65HVD230_DRV_H
#define SN65HVD230_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SN65HVD230_CMD_BASE COMPAT_MAGIC(SN65HVD230)
#define SN65HVD230_CMD_SET_STANDBY (SN65HVD230_CMD_BASE+0x01)
#define SN65HVD230_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif
