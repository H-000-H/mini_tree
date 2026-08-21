/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file max7219_regs.h
 *@brief MAX7219 芯片寄存器 / 几何常量
 *@author H-000-H

 */

#ifndef MAX7219_REGS_H
#define MAX7219_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** 空操作寄存器（多片级联用） */
#define MAX7219_REG_NOOP 0x00U
/** 第 1 位（digit）寄存器 */
#define MAX7219_REG_DIGIT0 0x01U
/** 第 8 位（digit）寄存器 */
#define MAX7219_REG_DIGIT7 0x08U
/** 解码模式寄存器 */
#define MAX7219_REG_DECODE 0x09U
/** 亮度寄存器 */
#define MAX7219_REG_INTENSITY 0x0AU
/** 扫描限制寄存器 */
#define MAX7219_REG_SCAN_LIMIT 0x0BU
/** 关断模式寄存器 */
#define MAX7219_REG_SHUTDOWN 0x0CU
/** 显示测试寄存器 */
#define MAX7219_REG_DISPLAY_TEST 0x0FU

/** 每片 8 位 */
#define MAX7219_DIGITS 8
/** 8×8 点阵帧字节数 */
#define MAX7219_MATRIX_BYTES 8

#ifdef __cplusplus
}
#endif

#endif /* MAX7219_REGS_H */
