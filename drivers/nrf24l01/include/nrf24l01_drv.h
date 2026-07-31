/**
 * SPDX-License-Identifier: Apache-2.0
 * @file nrf24l01_drv.h
 */
#ifndef NRF24L01_DRV_H
#define NRF24L01_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NRF24L01_CMD_BASE COMPAT_MAGIC(NRF24L01)
#define NRF24L01_CMD_WRITE_REG (NRF24L01_CMD_BASE + 0x01)
#define NRF24L01_CMD_READ_REG  (NRF24L01_CMD_BASE + 0x02)
#define NRF24L01_CMD_SEND      (NRF24L01_CMD_BASE + 0x03)
#define NRF24L01_CMD_COUNT 3
struct nrf24l01_reg { uint8_t reg; uint8_t val; };
struct nrf24l01_payload { uint8_t* data; size_t len; };
#ifdef __cplusplus
}
#endif
#endif
