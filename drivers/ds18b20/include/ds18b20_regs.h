/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ds18b20_regs.h
 */
#ifndef DS18B20_REGS_H
#define DS18B20_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define DS18B20_OW_SKIP_ROM         0xCCU
#define DS18B20_OW_CONVERT_T        0x44U
#define DS18B20_OW_READ_SCRATCHPAD  0xBEU
#define DS18B20_CONVERT_MS          750U
#define DS18B20_TEMP_LSB_PER_C      16

#ifdef __cplusplus
}
#endif

#endif /* DS18B20_REGS_H */
