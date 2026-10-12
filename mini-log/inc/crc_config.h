/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file crc_config.h
 * @brief CRC 模块配置: 引擎选择 + 生成多项式模型 (与 mini-ota 的 boot_config.h 对应部分等价)
 * @note 每个开关都支持 -DCONFIG_CRC_XXX=... 或直接 #define CRC_XXX ... 覆盖
 */
#ifndef CRC_CONFIG_H
#define CRC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 引擎选择 ---------------- */
#define CRC_MODE_BITWISE 0  /**< 逐位法: 零额外 RAM, 小数据量/资源紧张 */
#define CRC_MODE_TABLE   1  /**< 查表法: 约 1KB 静态表, 大数据量快 5~8 倍 */

#if defined(CONFIG_CRC_MODE)
#define CRC_MODE CONFIG_CRC_MODE
#elif defined(CRC_MODE)
/* 已由外部定义, 保持不变 */
#else
#define CRC_MODE CRC_MODE_TABLE
#endif

/* ---------------- 查表法表长 ---------------- */
#if defined(CONFIG_CRC_TABLE_SIZE)
#define CRC_TABLE_SIZE CONFIG_CRC_TABLE_SIZE
#elif defined(CRC_TABLE_SIZE)
/* 已由外部定义, 保持不变 */
#else
#define CRC_TABLE_SIZE 256
#endif

#if defined(__cplusplus) || defined(_MSC_VER)
/* MSVC 的 C 模式默认不带 _Static_assert(需 /std:c11), PC 端直编时跳过 */
#else
_Static_assert(CRC_TABLE_SIZE == 256, "CRC_TABLE_SIZE must be 256");
#endif

/* ---------------- 生成多项式模型 (默认标准 CRC-32) ---------------- */
#if defined(CONFIG_CRC_INIT)
#define CRC_MODEL_INIT CONFIG_CRC_INIT
#elif defined(CRC_MODEL_INIT)
#else
#define CRC_MODEL_INIT 0xFFFFFFFFu
#endif

#if defined(CONFIG_CRC_REFIN)
#define CRC_MODEL_REFIN CONFIG_CRC_REFIN
#elif defined(CRC_MODEL_REFIN)
#else
#define CRC_MODEL_REFIN 1
#endif

#if defined(CONFIG_CRC_REFOUT)
#define CRC_MODEL_REFOUT CONFIG_CRC_REFOUT
#elif defined(CRC_MODEL_REFOUT)
#else
#define CRC_MODEL_REFOUT 1
#endif

#if defined(CONFIG_CRC_XOR_OUT)
#define CRC_MODEL_XOR_OUT CONFIG_CRC_XOR_OUT
#elif defined(CRC_MODEL_XOR_OUT)
#else
#define CRC_MODEL_XOR_OUT 0xFFFFFFFFu
#endif

#if defined(CONFIG_CRC_POLY)
#define CRC_MODEL_POLY CONFIG_CRC_POLY
#elif defined(CRC_MODEL_POLY)
#else
#define CRC_MODEL_POLY 0x04C11DB7u
#endif

#if defined(CONFIG_CRC_WIDTH)
#define CRC_MODEL_WIDTH CONFIG_CRC_WIDTH
#elif defined(CRC_MODEL_WIDTH)
#else
#define CRC_MODEL_WIDTH 32u
#endif

#ifdef __cplusplus
}
#endif

#endif /* CRC_CONFIG_H */
