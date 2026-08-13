/**
 * SPDX-License-Identifier: Apache-2.0
 * @file ds18b20_regs.h
 * @brief DS18B20 单总线 ROM/功能命令与温度换算常量
 */
#ifndef DS18B20_REGS_H
#define DS18B20_REGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** 跳过 ROM（单点挂载时省去地址匹配） */
#define DS18B20_OW_SKIP_ROM 0xCCU
/** 启动温度转换（默认 12bit 分辨率，最长 750ms） */
#define DS18B20_OW_CONVERT_T 0x44U
/** 读取暂存器（9B：温度 LSB/MSB + 告警 + 配置 + CRC） */
#define DS18B20_OW_READ_SCRATCHPAD 0xBEU
/** 12bit 分辨率最大转换耗时（ms） */
#define DS18B20_CONVERT_MS 750U
/** 每 1℃ 对应的 LSB 数（0.0625℃/LSB） */
#define DS18B20_TEMP_LSB_PER_C 16

#ifdef __cplusplus
}
#endif

#endif /* DS18B20_REGS_H */
