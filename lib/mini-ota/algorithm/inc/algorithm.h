/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: algorithm.h
 * @author: H-000-H
 * @brief: OTA 算法接口（全部为流式/增量式）不写整体整体在单片机上无意义：SHA-256、HMAC-SHA-256、AES-CBC、AES-GCM
 *         返回 0 表示成功，非 0 为 mbedtls 错误码（CRC 的分段续算接口见 crc.h）接入的是mbedtls 4.2.x 版本，
 *         若库版本不同可能需调整(不能接入2.x版本里面的接口全是整体计算的几乎无法直接流式)。
 *         镜像大于 RAM 时分块喂入，避免整包驻留内存 
 */
#ifndef ALGORITHM_H
#define ALGORITHM_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
#include "mbedtls/sha256.h"
#include "mbedtls/gcm.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

/* =======================================================================================
 *              GCM/CBC_SHA 属认证算法，在认证确认通过之前，
 *              解出的明文不得写入最终位置或生效（verify-before-use）
 * ======================================================================================= */

/* ---------- SHA-256 增量计算（包装 mbedtls_sha256_context） ---------- */
typedef struct
{
    mbedtls_sha256_context ctx;
} sha256_stream_t;

/**
 * @brief 开始增量计算
 * @param s: 状态，不可为 NULL
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int sha256_begin(sha256_stream_t *s);

/**
 * @brief 喂入一段数据（可任意多次调用，顺序必须连续）
 * @param s:    状态
 * @param data: 输入数据缓冲区
 * @param len:  输入数据长度（字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int sha256_feed(sha256_stream_t *s, const uint8_t *data, size_t len);

/**
 * @brief 结束增量计算并输出摘要
 * @param s:      状态
 * @param output: 摘要输出缓冲区（32 字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int sha256_end(sha256_stream_t *s, uint8_t output[32]);

/* ---------- HMAC-SHA-256 增量计算（CBC_SHA 模式第一遍验 MAC 用） ---------- */
typedef struct
{
    mbedtls_md_context_t md;
} hmac_sha256_stream_t;

/** @brief 开始 HMAC-SHA-256 流式计算 */
int hmac_sha256_stream_begin(hmac_sha256_stream_t *s, const uint8_t *key, size_t key_len);

/** @brief 喂入一段数据（可任意多次调用，顺序必须连续） */
int hmac_sha256_stream_feed(hmac_sha256_stream_t *s, const uint8_t *data, size_t len);

/** @brief 结束并输出摘要（32 字节） */
int hmac_sha256_stream_end(hmac_sha256_stream_t *s, uint8_t output[32]);

/* ---------- AES-GCM 流式（feed 任意长度均可） ---------- */
typedef struct
{
    mbedtls_gcm_context gcm;
} gcm_stream_t;

/**
 * @brief 开始 GCM 流式加密
 * @return 0 成功；失败时内部已释放，状态不可再用
 */
int aes_gcm_encrypt_stream_begin(gcm_stream_t *s, const uint8_t *iv, size_t iv_len,
                                 const uint8_t *key, size_t key_len);

/** @brief 开始 GCM 流式解密（tag 在 decrypt_stream_finish 时比对） */
int aes_gcm_decrypt_stream_begin(gcm_stream_t *s, const uint8_t *iv, size_t iv_len,
                                 const uint8_t *key, size_t key_len);

/** @brief 喂入一段数据（加/解密方向由 begin 决定），输出等长 */
int aes_gcm_stream_feed(gcm_stream_t *s, const uint8_t *in, uint8_t *out, size_t len);

/** @brief 结束流式加密，输出认证标签 */
int aes_gcm_encrypt_stream_finish(gcm_stream_t *s, uint8_t *tag, size_t tag_len);

/** @brief 结束流式解密：计算标签并与期望值比对，不匹配返回 MBEDTLS_ERR_GCM_AUTH_FAILED */
int aes_gcm_decrypt_stream_finish(gcm_stream_t *s, const uint8_t *tag, size_t tag_len);

/* ---------- AES-CBC 流式（每段 len 须为 16 的倍数且非 0） ---------- */
typedef struct
{
    mbedtls_aes_context aes;
    uint8_t iv[16];    /* 链式 IV：加密=上一块输出，解密=上一块输入，feed 内自动维护 */
} cbc_stream_t;

/** @brief 开始 CBC 流式加密（内部 setkey_enc，IV 会存副本） */
int aes_cbc_encrypt_stream_begin(cbc_stream_t *s, const uint8_t iv[16],
                                 const uint8_t *key, size_t key_len);

/** @brief 开始 CBC 流式解密（内部 setkey_dec，IV 会存副本） */
int aes_cbc_decrypt_stream_begin(cbc_stream_t *s, const uint8_t iv[16],
                                 const uint8_t *key, size_t key_len);

/** @brief 喂入一段数据（len 须为 16 的倍数且非 0） */
int aes_cbc_encrypt_stream_feed(cbc_stream_t *s, const uint8_t *in, uint8_t *out, size_t len);
int aes_cbc_decrypt_stream_feed(cbc_stream_t *s, const uint8_t *in, uint8_t *out, size_t len);

/** @brief 释放 CBC 流式状态（无 finish，PKCS 去填充由调用方负责） */
void aes_cbc_stream_free(cbc_stream_t *s);

#ifdef __cplusplus
}
#endif
#endif /* ALGORITHM_H */
