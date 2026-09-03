/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file epaper_regs.h
 *@brief 电子纸驱动内部常量（时序/缓冲，避免 .c 内散落魔术字）
 *@author H-000-H
 *@details
 *   注意：面板几何（width/height）由 DTS 提供，不再在此定义默认值。
 */

#ifndef EPAPER_REGS_H
#define EPAPER_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

#define EPAPER_BUSY_TIMEOUT_MS 2000U /**< BUSY 等待超时缺省值（ms，DTS busy-timeout-ms 可覆盖） */
#define EPAPER_RESET_HOLD_MS 10U     /**< 复位脉冲保持时间（ms） */

#ifdef __cplusplus
}
#endif

#endif /* EPAPER_REGS_H */
