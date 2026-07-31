/**
 * SPDX-License-Identifier: Apache-2.0
 * @file bmp280_regs.h
 */
#ifndef BMP280_REGS_H
#define BMP280_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define BMP280_REG_DIG_T1       0x88U
#define BMP280_REG_SOFT_RESET   0xE0U
#define BMP280_REG_CTRL_MEAS    0xF4U
#define BMP280_REG_PRESS_MSB    0xF7U

#define BMP280_SOFT_RESET_VAL   0xB6U
#define BMP280_CTRL_FORCED_X1   0x27U

#ifdef __cplusplus
}
#endif

#endif /* BMP280_REGS_H */
