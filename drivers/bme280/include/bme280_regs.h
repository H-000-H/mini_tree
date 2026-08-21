/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file bme280_regs.h
 *@brief BME280 芯片寄存器 / 控制值定义
 *@author H-000-H

 */

#ifndef BME280_REGS_H
#define BME280_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** 校准参数寄存器（温度 T1/T2/T3，每参数 2B 小端） */
#define BME280_REG_DIG_T1 0x88U
/** 湿度校准 H1（1B） */
#define BME280_REG_DIG_H1 0xA1U
/** 湿度校准 H2 起始（H2..H6 依次排列） */
#define BME280_REG_DIG_H2 0xE1U
/** 软复位命令寄存器 */
#define BME280_REG_SOFT_RESET 0xE0U
/** 湿度采样配置寄存器 */
#define BME280_REG_CTRL_HUM 0xF2U
/** 量测控制寄存器（温度/气压采样率 + 模式） */
#define BME280_REG_CTRL_MEAS 0xF4U
/** 气压 MSB 寄存器（24bit 起始，P:0xF7..0xF9, T:0xFA..0xFC） */
#define BME280_REG_PRESS_MSB 0xF7U

/** 软复位写入值 */
#define BME280_SOFT_RESET_VAL 0xB6U
/** 湿度过采样 ×1 */
#define BME280_CTRL_HUM_OSRS1 0x01U
/** 强制模式 + 温度/气压过采样 ×1 */
#define BME280_CTRL_FORCED_X1 0x27U

#ifdef __cplusplus
}
#endif

#endif /* BME280_REGS_H */
