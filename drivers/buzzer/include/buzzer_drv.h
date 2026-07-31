/**
 * SPDX-License-Identifier: Apache-2.0
 * @file buzzer_drv.h
 */
#ifndef BUZZER_DRV_H
#define BUZZER_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BUZZER_CMD_BASE COMPAT_MAGIC(BUZZER)
#define BUZZER_CMD_BEEP (BUZZER_CMD_BASE+0x01)
#define BUZZER_CMD_COUNT 1
#ifdef __cplusplus
}
#endif
#endif
