/**
 * SPDX-License-Identifier: Apache-2.0
 * @file epaper_regs.h
 */
#ifndef EPAPER_REGS_H
#define EPAPER_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 默认几何（具体板级可由 DTS 覆盖后经 GET_INFO 读回；此处作第三方库默认值） */
#define EPAPER_DEFAULT_WIDTH   200
#define EPAPER_DEFAULT_HEIGHT  200
#define EPAPER_DEFAULT_BPP     1
#define EPAPER_BUSY_TIMEOUT_MS 2000U
#define EPAPER_RESET_HOLD_MS   10U

#ifdef __cplusplus
}
#endif

#endif /* EPAPER_REGS_H */
