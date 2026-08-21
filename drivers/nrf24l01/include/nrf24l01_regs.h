/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file nrf24l01_regs.h
 *@brief NRF24L01 SPI 操作码 / 常量
 *@author H-000-H

 */

#ifndef NRF24L01_REGS_H
#define NRF24L01_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** 读寄存器操作码 */
#define NRF24L01_OP_R_REGISTER 0x00U
/** 写寄存器操作码 */
#define NRF24L01_OP_W_REGISTER 0x20U
/** 写 TX 载荷操作码 */
#define NRF24L01_OP_W_TX_PAYLOAD 0xA0U
/** 寄存器地址掩码（5bit） */
#define NRF24L01_REG_ADDR_MASK 0x1FU
/** 单包最大载荷字节数 */
#define NRF24L01_MAX_PAYLOAD 32U

#ifdef __cplusplus
}
#endif

#endif /* NRF24L01_REGS_H */
