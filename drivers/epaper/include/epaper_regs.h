/**
 * SPDX-License-Identifier: Apache-2.0
 * @file epaper_regs.h
 * @brief 电子纸驱动默认几何与时序常量
 */
#ifndef EPAPER_REGS_H
#define EPAPER_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/* 默认几何（具体板级可由 DTS 覆盖后经 GET_INFO 读回；此处作第三方库默认值） */
#define EPAPER_DEFAULT_WIDTH 200 /**< 默认宽（像素） */
#define EPAPER_DEFAULT_HEIGHT 200 /**< 默认高（像素） */
#define EPAPER_DEFAULT_BPP 1 /**< 默认每像素比特数 */
#define EPAPER_BUSY_TIMEOUT_MS 2000U /**< BUSY 等待超时（ms） */
#define EPAPER_RESET_HOLD_MS 10U /**< 复位脉冲保持时间（ms） */

#ifdef __cplusplus
}
#endif

#endif /* EPAPER_REGS_H */
