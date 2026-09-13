/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @author:  H-000-H
 * @file: crc.c
 * @brief: CRC 校验实现，结果与 lib/mini-ota/tools/m_crc/image_crc.py 的 crc_generic 一致；
 *         引擎由宏 CRC_MODE 编译期选择：
 *         1（默认）查表法——约 1KB 静态表，大数据量快 5~8 倍；
 *         0 逐位法——零额外 RAM，小数据量/资源紧张场景。
 *         可在编译命令行 -DCRC_MODE=0 覆盖
 */
#include <stddef.h>
#include <stdint.h>
#include "crc_config.h"
#include "crc.h"

/* 位反转：将 value 的低 width 位按位反转（对齐 m_crc/image_crc.py 的 _reflect） */
static uint32_t reflect(uint32_t value, uint8_t width)
{
    uint32_t result = 0;

    for (uint8_t i = 0; i < width; i++)
    {
        result = (result << 1) | (value & 1u);
        value >>= 1;
    }

    return result;
}

/**
 * @brief 逐位引擎：返回最终反射/异或前的寄存器值
 * @param data: 输入数据
 * @param length: 输入长度
 * @param init: 初始值
 * @param refin: 输入是否反射
 * @param poly: 多项式
 * @param width: 位宽
 * @param mask: 掩码
 * @return uint32_t: 寄存器值
 */
static uint32_t crc_bitwise_engine(const uint8_t *data, size_t length,
                                   uint32_t reg, int refin,
                                   uint32_t poly, uint8_t width, uint32_t mask)
{
    /* 注意：reg/poly 已由调用方归一化（refin 时取反射形式并掩码），此处直接续算 */

    if (refin)
    {
        while (length-- > 0)
        {
            uint8_t byte = *data++;

            if (width >= 8)
            {
                reg ^= byte;
                for (uint8_t i = 0; i < 8; i++)
                {
                    reg = ((reg & 1u) != 0) ? ((reg >> 1) ^ poly) : (reg >> 1);
                    reg &= mask;
                }
            }
            else
            {
                /* 窄位宽逐位输入，避免高位丢弃 */
                for (uint8_t i = 0; i < 8; i++)
                {
                    reg ^= (byte >> i) & 1u;
                    reg = ((reg & 1u) != 0) ? ((reg >> 1) ^ poly) : (reg >> 1);
                    reg &= mask;
                }
            }
        }
    }
    else
    {
        /* 输入不反射：左移引擎，字节对齐到寄存器高位（reg/poly 已掩码） */
        uint32_t top_bit = 1u << (width - 1);

        while (length-- > 0)
        {
            uint8_t byte = *data++;

            if (width >= 8)
            {
                reg ^= (uint32_t)byte << (width - 8);
                for (uint8_t i = 0; i < 8; i++)
                {
                    reg = ((reg & top_bit) != 0) ? ((reg << 1) ^ poly) : (reg << 1);
                    reg &= mask;
                }
            }
            else
            {
                /* 窄位宽逐位输入，避免负移位 */
                for (int8_t i = 7; i >= 0; i--)
                {
                    reg ^= ((uint32_t)(byte >> i) & 1u) << (width - 1);
                    reg = ((reg & top_bit) != 0) ? ((reg << 1) ^ poly) : (reg << 1);
                    reg &= mask;
                }
            }
        }
    }

    return reg;
}

#if CRC_MODE

/* 查表法缓存：表由 (poly, width, refin) 决定，按需重建（静态分配约 1KB RAM） */
static uint32_t s_crc_table[CRC_TABLE_SIZE];
static uint32_t s_table_poly;
static uint8_t s_table_width;
static uint8_t s_table_refin;
static int s_table_ready = 0;

/**
 * @brief 查表法构建函数：构建查表法所需的 256 项表，poly 为引擎域内多项式（refin 时已反射）
 * @param poly: 多项式
 * @param width: 位宽
 * @param refin: 输入是否反射
 */
static void crc_table_build(uint32_t poly, uint8_t width, int refin)
{
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    uint32_t top_bit = 1u << (width - 1);

    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t r;

        if (refin)
        {
            r = i;
            for (uint8_t j = 0; j < 8; j++)
            {
                r = ((r & 1u) != 0) ? ((r >> 1) ^ poly) : (r >> 1);
            }
        }
        else
        {
            r = i << (width - 8);
            for (uint8_t j = 0; j < 8; j++)
            {
                r = ((r & top_bit) != 0) ? (((r << 1) ^ poly) & mask)
                                         : ((r << 1) & mask);
            }
        }

        s_crc_table[i] = r & mask;
    }

    s_table_poly = poly;
    s_table_width = width;
    s_table_refin = (refin != 0) ? 1u : 0u;
    s_table_ready = 1;
}

/**
 * @brief 查表引擎：返回最终反射/异或前的寄存器值；窄位宽（<8）退回逐位法
 * @param data: 输入数据
 * @param length: 输入长度
 * @param init: 初始值
 * @param refin: 输入是否反射
 * @param poly: 多项式
 * @param width: 位宽
 * @param mask: 掩码
 * @return uint32_t: 寄存器值
 */
static uint32_t crc_table_engine(const uint8_t *data, size_t length,
                                 uint32_t reg, int refin,
                                 uint32_t poly, uint8_t width, uint32_t mask)
{
    /* reg/poly 已由调用方归一化 */

    if (width < 8)
    {
        return crc_bitwise_engine(data, length, reg, refin, poly, width, mask);
    }

    /* 参数变化时重建表 */
    if (!s_table_ready || s_table_width != width ||
        s_table_refin != (uint8_t)(refin != 0) || s_table_poly != poly)
    {
        crc_table_build(poly, width, refin);
    }

    if (refin)
    {
        while (length-- > 0)
        {
            reg = (reg >> 8) ^ s_crc_table[(reg ^ *data++) & 0xFFu];
        }
    }
    else
    {
        while (length-- > 0)
        {
            uint8_t idx = (uint8_t)((reg >> (width - 8)) ^ *data++);

            reg = ((reg << 8) & mask) ^ s_crc_table[idx];
        }
    }

    return reg;
}

#endif /* CRC_MODE */

/**
 * @brief 公共收尾：按 refin/refout 是否一致决定反转，再异或输出值
 * @param reg: 寄存器值
 * @param width: 位宽
 * @param refin: 输入是否反射
 * @param refout: 输出是否反射
 * @param xor_out: 输出异或值
 * @param mask: 掩码
 * @return uint32_t: 最终结果
 */
static uint32_t crc_finalize(uint32_t reg, uint8_t width, int refin, int refout,
                             uint32_t xor_out, uint32_t mask)
{
    if ((refin != 0) != (refout != 0))
    {
        reg = reflect(reg, width);
    }

    return (reg ^ xor_out) & mask;
}

/*-------------------------------------------------------------------------------------------------------*/
/* 分段续算实现                                                                                          */
/*-------------------------------------------------------------------------------------------------------*/
void crc_stream_start(crc_stream_t *init_state, uint32_t init, int refin, int refout,
                      uint32_t xor_out, uint32_t poly, uint8_t width)
{
    uint32_t mask;

    if (init_state == NULL || width == 0 || width > 32)
    {
        init_state->width = 0; /* 标记无效状态，feed/finish 将直接返回 0 */
        return;
    }

    mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    init_state->width   = width;
    init_state->refin   = (refin != 0) ? 1u : 0u;
    init_state->refout  = (refout != 0) ? 1u : 0u;
    init_state->xor_out = xor_out;

    if (refin)
    {
        init_state->reg  = reflect(init, width) & mask;
        init_state->poly = reflect(poly, width) & mask;
    }
    else
    {
        init_state->reg  = init & mask;
        init_state->poly = poly & mask;
    }
}

void crc_stream_feed(crc_stream_t *s, const uint8_t *data, size_t length)
{
    uint32_t mask;

    if (s == NULL || s->width == 0 || s->width > 32)
    {
        return;
    }

    mask = (s->width >= 32) ? 0xFFFFFFFFu : ((1u << s->width) - 1u);

#if CRC_MODE
    s->reg = crc_table_engine(data, length, s->reg, s->refin, s->poly, s->width, mask);
#else
    s->reg = crc_bitwise_engine(data, length, s->reg, s->refin, s->poly, s->width, mask);
#endif
}

uint32_t crc_stream_finish(crc_stream_t *s)
{
    uint32_t mask;

    if (s == NULL || s->width == 0 || s->width > 32)
    {
        return 0;
    }

    mask = (s->width >= 32) ? 0xFFFFFFFFu : ((1u << s->width) - 1u);

    return crc_finalize(s->reg, s->width, s->refin, s->refout, s->xor_out, mask);
}
