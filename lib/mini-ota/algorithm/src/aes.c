/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @author:  H-000-H
 * @file: aes.c
 * @brief: AES-CBC 与 AES-GCM 增量（流式）封装（mbedtls 2.28 接口），
 *         所有函数返回 0 表示成功，非 0 为 mbedtls 错误码
 */
#include <stddef.h>
#include "algorithm.h"
#include "mbedtls/aes.h"
#include "mbedtls/gcm.h"

/* GCM 内部校验标签缓冲上限（GCM 标准标签长度 16 字节） */
#define AES_GCM_MAX_TAG_LEN 16u

/* =======================================================================================
 * 增量（流式）实现
 * ======================================================================================= */

/* ---------------- AES-GCM 流式 ---------------- */
int aes_gcm_encrypt_stream_begin(gcm_stream_t *s, const uint8_t *iv, size_t iv_len,
                                 const uint8_t *key, size_t key_len)
{
    int ret;

    if (s == NULL || iv == NULL || key == NULL)
    {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }

    mbedtls_gcm_init(&s->gcm);

    ret = mbedtls_gcm_setkey(&s->gcm, MBEDTLS_CIPHER_ID_AES, key,
                             (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        mbedtls_gcm_free(&s->gcm);
        return ret;
    }

    ret = mbedtls_gcm_starts(&s->gcm, MBEDTLS_GCM_ENCRYPT, iv, iv_len, NULL, 0);
    if (ret != 0)
    {
        mbedtls_gcm_free(&s->gcm);
    }
    return ret;
}

int aes_gcm_decrypt_stream_begin(gcm_stream_t *s, const uint8_t *iv, size_t iv_len,
                                 const uint8_t *key, size_t key_len)
{
    int ret;

    if (s == NULL || iv == NULL || key == NULL)
    {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }

    mbedtls_gcm_init(&s->gcm);

    ret = mbedtls_gcm_setkey(&s->gcm, MBEDTLS_CIPHER_ID_AES, key,
                             (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        mbedtls_gcm_free(&s->gcm);
        return ret;
    }

    ret = mbedtls_gcm_starts(&s->gcm, MBEDTLS_GCM_DECRYPT, iv, iv_len, NULL, 0);
    if (ret != 0)
    {
        mbedtls_gcm_free(&s->gcm);
    }
    return ret;
}

int aes_gcm_stream_feed(gcm_stream_t *s, const uint8_t *in, uint8_t *out, size_t len)
{
    if (s == NULL || in == NULL || out == NULL)
    {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }

    /* mbedtls 2.28 的 gcm_update 支持任意长度（内部处理未满块） */
    return mbedtls_gcm_update(&s->gcm, len, in, out);
}

int aes_gcm_encrypt_stream_finish(gcm_stream_t *s, uint8_t *tag, size_t tag_len)
{
    int ret;

    if (s == NULL || tag == NULL || tag_len == 0 || tag_len > AES_GCM_MAX_TAG_LEN)
    {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }

    ret = mbedtls_gcm_finish(&s->gcm, tag, tag_len);
    mbedtls_gcm_free(&s->gcm);
    return ret;
}

int aes_gcm_decrypt_stream_finish(gcm_stream_t *s, const uint8_t *tag, size_t tag_len)
{
    unsigned char check_tag[AES_GCM_MAX_TAG_LEN];
    unsigned char diff = 0;
    int ret;

    if (s == NULL || tag == NULL || tag_len == 0 || tag_len > AES_GCM_MAX_TAG_LEN)
    {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }

    ret = mbedtls_gcm_finish(&s->gcm, check_tag, tag_len);
    if (ret != 0)
    {
        mbedtls_gcm_free(&s->gcm);
        return ret;
    }

    /* mbedtls 2.28 的 GCM 解密不自动比对标签，需常量时间比较防篡改与侧信道 */
    for (size_t i = 0; i < tag_len; i++)
    {
        diff |= (unsigned char)(check_tag[i] ^ tag[i]);
    }

    mbedtls_gcm_free(&s->gcm);
    return (diff == 0) ? 0 : MBEDTLS_ERR_GCM_AUTH_FAILED;
}

/* ---------------- AES-CBC 流式 ---------------- */
int aes_cbc_encrypt_stream_begin(cbc_stream_t *s, const uint8_t iv[16],
                                 const uint8_t *key, size_t key_len)
{
    int ret;

    if (s == NULL || iv == NULL || key == NULL)
    {
        return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    }

    mbedtls_aes_init(&s->aes);

    ret = mbedtls_aes_setkey_enc(&s->aes, key, (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        mbedtls_aes_free(&s->aes);
        return ret;
    }

    for (size_t i = 0; i < 16u; i++)
    {
        s->iv[i] = iv[i];
    }
    return 0;
}

int aes_cbc_decrypt_stream_begin(cbc_stream_t *s, const uint8_t iv[16],
                                 const uint8_t *key, size_t key_len)
{
    int ret;

    if (s == NULL || iv == NULL || key == NULL)
    {
        return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    }

    mbedtls_aes_init(&s->aes);

    ret = mbedtls_aes_setkey_dec(&s->aes, key, (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        mbedtls_aes_free(&s->aes);
        return ret;
    }

    for (size_t i = 0; i < 16u; i++)
    {
        s->iv[i] = iv[i];
    }
    return 0;
}

int aes_cbc_encrypt_stream_feed(cbc_stream_t *s, const uint8_t *in, uint8_t *out, size_t len)
{
    if (s == NULL || in == NULL || out == NULL ||
        len == 0 || (len % 16u) != 0)
    {
        return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    }

    /* crypt_cbc 会把 s->iv 更新为最后一块密文，本身就支持链式传递 */
    return mbedtls_aes_crypt_cbc(&s->aes, MBEDTLS_AES_ENCRYPT, len,
                                 s->iv, in, out);
}

int aes_cbc_decrypt_stream_feed(cbc_stream_t *s, const uint8_t *in, uint8_t *out, size_t len)
{
    if (s == NULL || in == NULL || out == NULL ||
        len == 0 || (len % 16u) != 0)
    {
        return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    }

    /* 解密方向：crypt_cbc 把 s->iv 更新为最后一块密文，链式状态自动延续 */
    return mbedtls_aes_crypt_cbc(&s->aes, MBEDTLS_AES_DECRYPT, len,
                                 s->iv, in, out);
}

void aes_cbc_stream_free(cbc_stream_t *s)
{
    if (s == NULL)
    {
        return;
    }
    mbedtls_aes_free(&s->aes);
}
