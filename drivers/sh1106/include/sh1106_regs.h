/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sh1106_regs.h
 *@brief SH1106 面板寄存器 / 几何 / I2C 控制字节
 *@author H-000-H

 */

#ifndef SH1106_REGS_H
#define SH1106_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** 面板几何常量 */
#define SH1106_WIDTH 128
#define SH1106_HEIGHT 64
#define SH1106_PAGES 8
#define SH1106_FB_SIZE (SH1106_WIDTH * SH1106_PAGES)
/** RAM 列偏移（SH1106 固有 2 像素错位） */
#define SH1106_COL_OFFSET 2

/** I2C 控制字节：后续为命令 */
#define SH1106_I2C_CTRL_CMD 0x00U
/** I2C 控制字节：后续为显示数据 */
#define SH1106_I2C_CTRL_DATA 0x40U

/** 显示开关命令 */
#define SH1106_REG_DISPLAY_OFF 0xAEU
#define SH1106_REG_DISPLAY_ON 0xAFU
/** 对比度命令 */
#define SH1106_REG_SET_CONTRAST 0x81U
/** 页地址命令（0xB0..0xB7） */
#define SH1106_REG_SET_PAGE 0xB0U
/** 列地址低 4bit */
#define SH1106_REG_SET_COL_LO 0x00U
/** 列地址高 4bit */
#define SH1106_REG_SET_COL_HI 0x10U
/** 时钟分频/振荡频率 */
#define SH1106_REG_CLK_DIV 0xD5U
/** 多路复用比 */
#define SH1106_REG_MUX_RATIO 0xA8U
/** 显示偏移 */
#define SH1106_REG_DISP_OFFSET 0xD3U
/** 起始行 */
#define SH1106_REG_START_LINE 0x40U
/** 电荷泵 */
#define SH1106_REG_CHARGE_PUMP 0x8DU
/** 内存寻址模式 */
#define SH1106_REG_MEM_MODE 0x20U
/** 段重映射 */
#define SH1106_REG_SEG_REMAP 0xA1U
/** COM 扫描方向 */
#define SH1106_REG_COM_SCAN_DEC 0xC8U
/** COM 引脚配置 */
#define SH1106_REG_COM_PINS 0xDAU
/** 预充电周期 */
#define SH1106_REG_PRECHARGE 0xD9U
/** VCOMH 电平 */
#define SH1106_REG_VCOM_DETECT 0xDBU
/** 全屏点亮 */
#define SH1106_REG_ENTIRE_ON 0xA4U
/** 正常显示 */
#define SH1106_REG_NORMAL_DISP 0xA6U

/** 初始化序列参数值 */
#define SH1106_VAL_CLK_DIV 0x80U
#define SH1106_VAL_MUX_63 0x3FU
#define SH1106_VAL_OFFSET_0 0x00U
#define SH1106_VAL_CHARGE_ON 0x14U
#define SH1106_VAL_HORIZ_ADDR 0x00U
#define SH1106_VAL_COM_PINS 0x12U
#define SH1106_VAL_CONTRAST 0xCFU
#define SH1106_VAL_PRECHARGE 0xF1U
#define SH1106_VAL_VCOM 0x40U

#ifdef __cplusplus
}
#endif

#endif /* SH1106_REGS_H */
