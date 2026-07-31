/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bme280_regs.h
 */
#ifndef BME280_REGS_H
#define BME280_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define BME280_REG_DIG_T1       0x88U
#define BME280_REG_DIG_H1       0xA1U
#define BME280_REG_DIG_H2       0xE1U
#define BME280_REG_SOFT_RESET   0xE0U
#define BME280_REG_CTRL_HUM     0xF2U
#define BME280_REG_CTRL_MEAS    0xF4U
#define BME280_REG_PRESS_MSB    0xF7U

#define BME280_SOFT_RESET_VAL   0xB6U
#define BME280_CTRL_HUM_OSRS1   0x01U
#define BME280_CTRL_FORCED_X1   0x27U

#ifdef __cplusplus
}
#endif

#endif /* BME280_REGS_H */
