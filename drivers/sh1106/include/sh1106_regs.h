/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sh1106_regs.h
 */
#ifndef SH1106_REGS_H
#define SH1106_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SH1106_WIDTH            128
#define SH1106_HEIGHT           64
#define SH1106_PAGES            8
#define SH1106_FB_SIZE          (SH1106_WIDTH * SH1106_PAGES)
#define SH1106_COL_OFFSET       2

#define SH1106_I2C_CTRL_CMD     0x00U
#define SH1106_I2C_CTRL_DATA    0x40U

#define SH1106_REG_DISPLAY_OFF  0xAEU
#define SH1106_REG_DISPLAY_ON   0xAFU
#define SH1106_REG_SET_CONTRAST 0x81U
#define SH1106_REG_SET_PAGE     0xB0U
#define SH1106_REG_SET_COL_LO   0x00U
#define SH1106_REG_SET_COL_HI   0x10U
#define SH1106_REG_CLK_DIV      0xD5U
#define SH1106_REG_MUX_RATIO    0xA8U
#define SH1106_REG_DISP_OFFSET  0xD3U
#define SH1106_REG_START_LINE   0x40U
#define SH1106_REG_CHARGE_PUMP  0x8DU
#define SH1106_REG_MEM_MODE     0x20U
#define SH1106_REG_SEG_REMAP    0xA1U
#define SH1106_REG_COM_SCAN_DEC 0xC8U
#define SH1106_REG_COM_PINS     0xDAU
#define SH1106_REG_PRECHARGE    0xD9U
#define SH1106_REG_VCOM_DETECT  0xDBU
#define SH1106_REG_ENTIRE_ON    0xA4U
#define SH1106_REG_NORMAL_DISP  0xA6U

#define SH1106_VAL_CLK_DIV      0x80U
#define SH1106_VAL_MUX_63       0x3FU
#define SH1106_VAL_OFFSET_0     0x00U
#define SH1106_VAL_CHARGE_ON    0x14U
#define SH1106_VAL_HORIZ_ADDR   0x00U
#define SH1106_VAL_COM_PINS     0x12U
#define SH1106_VAL_CONTRAST     0xCFU
#define SH1106_VAL_PRECHARGE    0xF1U
#define SH1106_VAL_VCOM         0x40U

#ifdef __cplusplus
}
#endif

#endif /* SH1106_REGS_H */
