/**
 * SPDX-License-Identifier: Apache-2.0
 * @file rc522_regs.h
 * @brief RC522 寄存器 / 操作码 / 命令常量
 */
#ifndef RC522_REGS_H
#define RC522_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** 命令寄存器 */
#define RC522_REG_COMMAND 0x01U
/** 中断使能寄存器 */
#define RC522_REG_COMIEN 0x02U
/** 中断状态寄存器 */
#define RC522_REG_COMIRQ 0x04U
/** 错误标志寄存器 */
#define RC522_REG_ERROR 0x06U
/** FIFO 数据寄存器 */
#define RC522_REG_FIFO_DATA 0x09U
/** FIFO 等级寄存器 */
#define RC522_REG_FIFO_LEVEL 0x0AU
/** 控制寄存器 */
#define RC522_REG_CONTROL 0x0CU
/** 位帧控制寄存器 */
#define RC522_REG_BIT_FRAMING 0x0DU
/** 模式寄存器 */
#define RC522_REG_MODE 0x11U
/** TX 控制寄存器 */
#define RC522_REG_TX_CONTROL 0x14U
/** TX 信号 ASK 调制寄存器 */
#define RC522_REG_TX_ASK 0x15U
/** 定时器模式寄存器 */
#define RC522_REG_TMODE 0x2AU
/** 定时器预分频寄存器 */
#define RC522_REG_TPRESCALER 0x2BU
/** 定时器重装载低字节 */
#define RC522_REG_TRELOAD_L 0x2CU
/** 定时器重装载高字节 */
#define RC522_REG_TRELOAD_H 0x2DU

/** 空闲命令 */
#define RC522_OP_IDLE 0x00U
/** 收发命令 */
#define RC522_OP_TRANSCEIVE 0x0CU
/** MIFARE 认证命令 */
#define RC522_OP_MF_AUTHENT 0x0EU
/** 软复位命令 */
#define RC522_OP_SOFT_RESET 0x0FU

/** PICC REQA 命令 */
#define RC522_PICC_REQA 0x26U
/** PICC 防冲突命令（级联 1） */
#define RC522_PICC_ANTICOLL1 0x93U
/** PICC SELECT NVB 字节 */
#define RC522_PICC_SELECTNVB 0x20U

/** SPI 地址掩码（7bit 地址） */
#define RC522_SPI_ADDR_MASK 0x7EU
/** SPI 读标志位 */
#define RC522_SPI_READ_FLAG 0x80U

/** 中断：全使能 */
#define RC522_IRQ_IEN 0x80U
/** 中断：定时器 */
#define RC522_IRQ_TIMER 0x01U
/** 中断：错误掩码 */
#define RC522_IRQ_ERR_MASK 0x1BU
/** 认证中断使能 */
#define RC522_IRQ_AUTH_EN 0x12U
/** 认证等待状态 */
#define RC522_IRQ_AUTH_WAIT 0x10U
/** 收发中断使能 */
#define RC522_IRQ_TXRX_EN 0x77U
/** 收发等待状态 */
#define RC522_IRQ_TXRX_WAIT 0x30U

/** 位帧控制：清空 FIFO */
#define RC522_BIT_FLUSH_FIFO 0x80U
/** 位帧控制：启动发送 */
#define RC522_BIT_START_SEND 0x80U
/** 位帧控制：接收位对齐 */
#define RC522_BIT_RX_ALIGN 0x07U
/** 位帧控制：发送末位比特数 */
#define RC522_BIT_TX_LASTBITS7 0x07U
/** FIFO 最大深度 */
#define RC522_FIFO_MAX 16U

/** 初始化参数值 */
#define RC522_INIT_TMODE 0x8DU
#define RC522_INIT_TPRESCALER 0x3EU
#define RC522_INIT_TRELOAD_H 0x1EU
#define RC522_INIT_TRELOAD_L 0x00U
#define RC522_INIT_TX_ASK 0x40U
#define RC522_INIT_MODE 0x3DU
/** 天线开启掩码 */
#define RC522_ANTENNA_ON_MASK 0x03U

#ifdef __cplusplus
}
#endif

#endif /* RC522_REGS_H */
