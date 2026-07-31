/**
 * SPDX-License-Identifier: Apache-2.0
 * @file rc522_regs.h
 */
#ifndef RC522_REGS_H
#define RC522_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define RC522_REG_COMMAND       0x01U
#define RC522_REG_COMIEN        0x02U
#define RC522_REG_COMIRQ        0x04U
#define RC522_REG_ERROR         0x06U
#define RC522_REG_FIFO_DATA     0x09U
#define RC522_REG_FIFO_LEVEL    0x0AU
#define RC522_REG_CONTROL       0x0CU
#define RC522_REG_BIT_FRAMING   0x0DU
#define RC522_REG_MODE          0x11U
#define RC522_REG_TX_CONTROL    0x14U
#define RC522_REG_TX_ASK        0x15U
#define RC522_REG_TMODE         0x2AU
#define RC522_REG_TPRESCALER    0x2BU
#define RC522_REG_TRELOAD_L     0x2CU
#define RC522_REG_TRELOAD_H     0x2DU

#define RC522_OP_IDLE           0x00U
#define RC522_OP_TRANSCEIVE     0x0CU
#define RC522_OP_MF_AUTHENT     0x0EU
#define RC522_OP_SOFT_RESET     0x0FU

#define RC522_PICC_REQA         0x26U
#define RC522_PICC_ANTICOLL1    0x93U
#define RC522_PICC_SELECTNVB    0x20U

#define RC522_SPI_ADDR_MASK     0x7EU
#define RC522_SPI_READ_FLAG     0x80U

#define RC522_IRQ_IEN           0x80U
#define RC522_IRQ_TIMER         0x01U
#define RC522_IRQ_ERR_MASK      0x1BU
#define RC522_IRQ_AUTH_EN       0x12U
#define RC522_IRQ_AUTH_WAIT     0x10U
#define RC522_IRQ_TXRX_EN       0x77U
#define RC522_IRQ_TXRX_WAIT     0x30U

#define RC522_BIT_FLUSH_FIFO    0x80U
#define RC522_BIT_START_SEND    0x80U
#define RC522_BIT_RX_ALIGN      0x07U
#define RC522_BIT_TX_LASTBITS7  0x07U
#define RC522_FIFO_MAX          16U

#define RC522_INIT_TMODE        0x8DU
#define RC522_INIT_TPRESCALER   0x3EU
#define RC522_INIT_TRELOAD_H    0x1EU
#define RC522_INIT_TRELOAD_L    0x00U
#define RC522_INIT_TX_ASK       0x40U
#define RC522_INIT_MODE         0x3DU
#define RC522_ANTENNA_ON_MASK   0x03U

#ifdef __cplusplus
}
#endif

#endif /* RC522_REGS_H */
