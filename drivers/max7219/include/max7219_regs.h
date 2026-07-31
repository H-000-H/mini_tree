/**
 * SPDX-License-Identifier: Apache-2.0
 * @file max7219_regs.h
 */
#ifndef MAX7219_REGS_H
#define MAX7219_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX7219_REG_NOOP         0x00U
#define MAX7219_REG_DIGIT0       0x01U
#define MAX7219_REG_DIGIT7       0x08U
#define MAX7219_REG_DECODE       0x09U
#define MAX7219_REG_INTENSITY    0x0AU
#define MAX7219_REG_SCAN_LIMIT   0x0BU
#define MAX7219_REG_SHUTDOWN     0x0CU
#define MAX7219_REG_DISPLAY_TEST 0x0FU

#define MAX7219_DIGITS           8
#define MAX7219_MATRIX_BYTES     8

#ifdef __cplusplus
}
#endif

#endif /* MAX7219_REGS_H */
