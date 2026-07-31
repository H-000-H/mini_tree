/**
 * SPDX-License-Identifier: Apache-2.0
 * @file max7219_drv.h
 */
#ifndef MAX7219_DRV_H
#define MAX7219_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "max7219_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX7219_CMD_BASE      COMPAT_MAGIC(MAX7219)
#define MAX7219_CMD_INIT      (MAX7219_CMD_BASE + 0x01)
#define MAX7219_CMD_SET_DIGIT (MAX7219_CMD_BASE + 0x02)
#define MAX7219_CMD_CLEAR     (MAX7219_CMD_BASE + 0x03)
#define MAX7219_CMD_FLUSH_FB  (MAX7219_CMD_BASE + 0x04)
#define MAX7219_CMD_COUNT     4

struct max7219_digit
{
    uint8_t digit; /* 1..8 */
    uint8_t value;
};

/** 8 行点阵，每字节一行（MSB=左） */
struct max7219_fb
{
    const uint8_t* rows; /* 长度 MAX7219_MATRIX_BYTES */
    size_t         len;
};

#ifdef __cplusplus
}
#endif

#endif /* MAX7219_DRV_H */
