/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: crc.h
 * @brief: CRC 分段续算接口，结果与 lib/mini-ota/tools/m_crc/image_crc.py 的 crc_generic 一致；
 *         引擎通过宏 CRC_MODE 编译期选择，两种实现结果完全一致
 */
#ifndef CRC_H
#define CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * @brief CRC 分段续算状态（reg 为已按 refin 归一化的寄存器值）
 * @note  refout/xor_out 只在 crc_stream_finish 时应用一次，
 *        中间分段直接把 reg 传回 crc_stream_feed 续算
 */
typedef struct
{
    uint32_t reg;      /* 寄存器值（跨段传递的状态） */
    uint32_t poly;     /* 归一化后的多项式（refin 时已反射） */
    uint32_t xor_out;  /* 最终异或输出值 */
    uint8_t  width;    /* CRC 位宽（0 = 无效状态，feed/finish 直接返回 0） */
    uint8_t  refin;    /* 输入是否反射 */
    uint8_t  refout;   /* 输出是否反射 */
} crc_stream_t;

/**
 * @brief 开始分段 CRC 计算（记录参数并归一化初始寄存器）
 * @param s       状态，不可为 NULL
 * @param init    寄存器初始值
 * @param refin   输入是否反射
 * @param refout  最终结果是否反射
 * @param xor_out 最终异或输出值
 * @param poly    生成多项式（MSB 表示）
 * @param width   CRC 位宽（1~32，非法时状态标记为无效）
 */
void crc_stream_start(crc_stream_t *s, uint32_t init, int refin, int refout,
                      uint32_t xor_out, uint32_t poly, uint8_t width);

/**
 * @brief 喂入一段数据，续算寄存器值（可任意多次调用，顺序必须连续）
 */
void crc_stream_feed(crc_stream_t *s, const uint8_t *data, size_t length);

/**
 * @brief 结束分段计算：应用 refout/xor_out 得到最终 CRC
 * @return 最终 CRC 值；状态无效（width 非法）时返回 0
 */
uint32_t crc_stream_finish(crc_stream_t *s);

#ifdef __cplusplus
}
#endif

#endif /* CRC_H */
