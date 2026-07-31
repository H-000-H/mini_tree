/**
 * SPDX-License-Identifier: Apache-2.0
 * @file rs485_modbus_drv.h
 */
#ifndef RS485_MODBUS_DRV_H
#define RS485_MODBUS_DRV_H
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RS485_MODBUS_CMD_BASE COMPAT_MAGIC(RS485_MODBUS)
#define RS485_MODBUS_CMD_READ_HOLDING (RS485_MODBUS_CMD_BASE+0x01)
#define RS485_MODBUS_CMD_WRITE_SINGLE (RS485_MODBUS_CMD_BASE+0x02)
#define RS485_MODBUS_CMD_COUNT 2
struct modbus_read { uint8_t slave; uint16_t addr; uint16_t value; };
struct modbus_write { uint8_t slave; uint16_t addr; uint16_t value; };
#ifdef __cplusplus
}
#endif
#endif
