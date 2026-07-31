/**
 * SPDX-License-Identifier: Apache-2.0
 * @file nrf24l01_regs.h
 */
#ifndef NRF24L01_REGS_H
#define NRF24L01_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NRF24L01_OP_R_REGISTER    0x00U
#define NRF24L01_OP_W_REGISTER    0x20U
#define NRF24L01_OP_W_TX_PAYLOAD  0xA0U
#define NRF24L01_REG_ADDR_MASK    0x1FU
#define NRF24L01_MAX_PAYLOAD      32U

#ifdef __cplusplus
}
#endif

#endif /* NRF24L01_REGS_H */
